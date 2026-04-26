from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import time

import httpx
import psutil
import typer
from rich.columns import Columns
from rich.console import Console
from rich.panel import Panel
from rich.rule import Rule
from rich.table import Table
from rich.text import Text

from valora import __version__
from valora.core.config import get_config_path, load_config, save_config, state
from valora.core.gpu import detect_vulkan_support, get_gpu_name, get_total_vram_mb
from valora.core.models import ModelInfo, estimate_context_overhead_mb, format_size_mb, is_model_usable, pick_best_model, recommend_models_for_device, resolve_auto_runtime_settings, scan_models
from valora.core.providers import PROVIDERS, ProviderSpec, get_provider
from valora.utils.download import download_with_progress
from valora.utils.huggingface import HfGgufFile, HfRepoManifest, build_auth_headers, fetch_repo_manifest, normalize_repo_id

console = Console()
LOCAL_KV_CACHE_CHOICES = ("f16", "q8_0", "q4_0", "q4_1")
CHAT_COMMAND_HELP: dict[str, str] = {
    "/help": "Show available slash commands",
    "/models": "Search available local and saved cloud models",
    "/allow-dir": "Allow one extra directory for file tools",
    "/web": "Search the web with SerpApi",
    "/read": "Read a local text file",
    "/write": "Write a file with /write path | content",
    "/edit": "Edit a file with /edit path | find | replace",
    "/mkdir": "Create a folder in the current chat directory",
    "/delete": "Delete a file or folder in the current chat directory",
    "/provider": "Switch backend",
    "/model": "Set the active model",
    "/system": "Set or clear the system prompt",
    "/ctx": "Change local context size",
    "/gpu-layers": "Set GPU layers or use -1 for auto",
    "/threads": "Set local CPU threads",
    "/kv-k": "Set KV cache K type",
    "/kv-v": "Set KV cache V type",
    "/auto": "Restore automatic local defaults",
    "/clear": "Clear the transcript",
    "/api": "Show the API setup table",
    "/save": "Save the current chat default",
    "/exit": "Leave chat",
}
LOCAL_SETUP_COMMAND_HELP: dict[str, str] = {
    "/help": "Show available slash commands",
    "/models": "Search local models",
    "/model": "Switch the local model",
    "/ctx": "Change local context size",
    "/gpu-layers": "Set GPU layers or use -1 for auto",
    "/threads": "Set local CPU threads",
    "/kv-k": "Set KV cache K type",
    "/kv-v": "Set KV cache V type",
    "/auto": "Restore automatic local defaults",
    "/save": "Save the current local defaults",
    "/start": "Launch chat in a new terminal",
    "/exit": "Leave setup",
}
KEY_ENTER = "ENTER"
KEY_BACKSPACE = "BACKSPACE"
KEY_CTRL_C = "CTRL_C"
KEY_CTRL_D = "CTRL_D"
KEY_ESC = "ESC"
SPINNER_FRAMES = ("◐", "◓", "◑", "◒")


def _mask_secret(value: str) -> str:
    cleaned = value.strip()
    if not cleaned:
        return "Not configured"
    if len(cleaned) <= 6:
        return "*" * len(cleaned)
    return f"{cleaned[:3]}...{cleaned[-3:]}"


def _normalize_path(path: Path) -> Path:
    return path.expanduser().resolve(strict=False)


def _is_relative_to(path: Path, base: Path) -> bool:
    try:
        path.relative_to(base)
        return True
    except ValueError:
        return False


def _protected_roots() -> list[Path]:
    roots: list[Path] = []
    candidates = {
        os.environ.get("SystemRoot", r"C:\Windows"),
        r"C:\Windows",
        r"C:\Windows\System32",
        os.environ.get("ProgramFiles", r"C:\Program Files"),
        os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
    }
    for candidate in candidates:
        cleaned = str(candidate).strip()
        if cleaned:
            roots.append(_normalize_path(Path(cleaned)))
    return roots


def _resolve_scoped_path(path_arg: str, session_root: Path, allowed_dirs: set[Path], mode: str) -> Path:
    candidate = Path(path_arg).expanduser()
    if not candidate.is_absolute():
        candidate = session_root / candidate
    resolved = _normalize_path(candidate)

    for protected in _protected_roots():
        if _is_relative_to(resolved, protected) or resolved == protected:
            raise ValueError(f"{mode.capitalize()} access is blocked for protected system paths like `{protected}`.")

    for allowed in allowed_dirs:
        if _is_relative_to(resolved, allowed) or resolved == allowed:
            return resolved

    raise ValueError(
        f"{mode.capitalize()} access is limited to `{session_root}`. "
        f"Use `/allow-dir {resolved.parent}` first if you want to grant access to another folder."
    )


def _resolve_main_dir_path(path_arg: str, session_root: Path, mode: str) -> Path:
    resolved = _resolve_scoped_path(path_arg, session_root, {session_root}, mode)
    if resolved == session_root:
        raise ValueError(f"You cannot {mode} the main chat directory itself.")
    return resolved


def _get_ctx_len() -> int:
    try:
        return max(1024, int(state.get("ctx", "4096")))
    except (TypeError, ValueError):
        return 4096


def _preferred_models_dir() -> Path:
    configured = str(state.get("folder", "")).strip()
    if configured:
        return Path(configured)
    return Path.home() / "models"


def _provider_profiles() -> dict[str, dict[str, str]]:
    raw = state.get("provider_profiles", {})
    if not isinstance(raw, dict):
        return {}
    normalized: dict[str, dict[str, str]] = {}
    for slug, profile in raw.items():
        if not isinstance(profile, dict):
            continue
        normalized[str(slug)] = {str(key): str(value) for key, value in profile.items()}
    state["provider_profiles"] = normalized
    return normalized


def _get_provider_profile(slug: str) -> dict[str, str]:
    return _provider_profiles().get(slug, {}).copy()


def _save_provider_profile(slug: str, profile: dict[str, str]) -> None:
    profiles = _provider_profiles()
    cleaned = {key: value for key, value in profile.items() if str(value).strip()}
    if cleaned:
        profiles[slug] = cleaned
    elif slug in profiles:
        del profiles[slug]
    state["provider_profiles"] = profiles


def _default_provider_base_url(spec: ProviderSpec) -> str:
    if spec.slug == "valora-local":
        port = str(state.get("port", "8080")).strip() or "8080"
        return f"http://127.0.0.1:{port}/v1"
    return spec.default_base_url


def _provider_api_key(slug: str) -> str:
    if slug == "huggingface":
        return str(state.get("hf_api_token", "")).strip()
    if slug == "serpapi":
        return str(state.get("mcp_serpapi_api_key", "")).strip()
    return _get_provider_profile(slug).get("api_key", "").strip()


def _provider_base_url(slug: str) -> str:
    return _get_provider_profile(slug).get("base_url", "").strip() or _default_provider_base_url(PROVIDERS[slug])


def _provider_model(slug: str) -> str:
    profile_model = _get_provider_profile(slug).get("model", "").strip()
    if profile_model:
        return profile_model
    if state.get("chat_provider") == slug:
        return str(state.get("chat_model", "")).strip()
    return ""


def _provider_is_configured(spec: ProviderSpec) -> bool:
    profile = _get_provider_profile(spec.slug)
    has_saved_profile = any(str(value).strip() for value in profile.values())

    if spec.slug == "huggingface":
        return bool(str(state.get("hf_api_token", "")).strip())
    if spec.slug == "serpapi":
        return bool(str(state.get("mcp_serpapi_api_key", "")).strip())
    if spec.auth_kind == "none":
        return has_saved_profile or bool(_provider_model(spec.slug).strip()) or state.get("chat_provider") == spec.slug
    return has_saved_profile or bool(_provider_api_key(spec.slug)) or bool(_provider_model(spec.slug).strip())


def _local_models() -> list[ModelInfo]:
    folder = str(state.get("folder", "")).strip()
    if not folder:
        return []
    return scan_models(folder)


def _pick_local_model(requested_model: str = "") -> ModelInfo | None:
    models = _local_models()
    if not models:
        return None

    requested = requested_model.strip().lower()
    if requested:
        for model in models:
            if requested in {model.name.lower(), str(model.path).lower(), model.path.stem.lower()}:
                return model
        for model in models:
            if requested in model.name.lower():
                return model

    preferred_name = str(state.get("selected_model", "")).strip().lower()
    if preferred_name:
        for model in models:
            if preferred_name in {model.name.lower(), str(model.path).lower(), model.path.stem.lower()}:
                return model

    best_index = pick_best_model(models, get_total_vram_mb())
    if best_index < 0:
        return models[0]
    return models[best_index]


def _local_chat_ready() -> tuple[bool, str]:
    llama_cli = str(state.get("llama_cli", "")).strip()
    if not llama_cli or not Path(llama_cli).is_file():
        return False, "llama.cpp is not set up yet. Run `valora setup --llama.cpp`."
    if not _local_models():
        return False, "No local GGUF models were found. Run `valora get <huggingface-repo>` or `valora setup --models`."
    return True, ""


def _suggest_local_runtime_profile(model: ModelInfo) -> dict[str, str]:
    memory = psutil.virtual_memory()
    available_ram_mb = int(memory.available / (1024 * 1024))
    resolved_gpu_layers, resolved_threads = resolve_auto_runtime_settings(
        model,
        "auto",
        "auto",
    )

    if available_ram_mb >= 16384 and model.size_mb <= 4096:
        ctx = "8192"
    else:
        ctx = "4096"

    return {
        "ctx": ctx,
        "gpu_layers": "-1",
        "threads": resolved_threads or "4",
        "kv_cache_k": "f16",
        "kv_cache_v": "f16",
    }


def _resolved_local_runtime_profile(model: ModelInfo, overrides: dict[str, str] | None = None) -> dict[str, str]:
    profile = _suggest_local_runtime_profile(model)
    source = {
        "ctx": str(state.get("ctx", "")).strip(),
        "gpu_layers": str(state.get("gpu_layers", "")).strip(),
        "threads": str(state.get("threads", "")).strip(),
        "kv_cache_k": str(state.get("kv_cache_k", "")).strip(),
        "kv_cache_v": str(state.get("kv_cache_v", "")).strip(),
    }
    if overrides:
        source.update({key: str(value).strip() for key, value in overrides.items()})

    if source.get("ctx") and source["ctx"].lower() != "auto":
        profile["ctx"] = source["ctx"]

    gpu_input = source.get("gpu_layers", "auto") or "auto"
    threads_input = source.get("threads", "auto") or "auto"
    resolved_gpu_layers, resolved_threads = resolve_auto_runtime_settings(model, "auto", threads_input)
    if gpu_input.lower() in {"auto", "-1"}:
        profile["gpu_layers"] = "-1"
    else:
        profile["gpu_layers"] = gpu_input
    profile["threads"] = resolved_threads or profile["threads"]

    if source.get("kv_cache_k"):
        profile["kv_cache_k"] = source["kv_cache_k"]
    if source.get("kv_cache_v"):
        profile["kv_cache_v"] = source["kv_cache_v"]

    return profile


def _sync_special_provider_state(slug: str, profile: dict[str, str]) -> None:
    if slug == "huggingface":
        state["hf_api_token"] = profile.get("api_key", "").strip()
    elif slug == "serpapi":
        resolved_key = profile.get("api_key", "").strip()
        state["mcp_serpapi_api_key"] = resolved_key
        state["mcp_enabled"] = bool(resolved_key)
        template = str(state.get("mcp_server_template", "https://mcp.serpapi.com/{api_key}/mcp"))
        state["mcp_server_url"] = template.format(api_key=resolved_key) if resolved_key else ""


def _set_chat_default(provider_slug: str, model: str) -> None:
    state["chat_provider"] = provider_slug
    if model.strip():
        state["chat_model"] = model.strip()


def _print_recommended_models(total_ram_mb: int, vram_mb: int, ctx_len: int) -> None:
    recommendations = recommend_models_for_device(total_ram_mb, vram_mb, ctx_len)
    if not recommendations:
        console.print("[yellow]No recommended models fit the current device estimate.[/yellow]")
        return

    console.print("[bold]Recommended models for this device[/bold]")
    console.print(
        f"[dim]Filtered for approximate local fit with default context length {ctx_len} "
        f"using system RAM {total_ram_mb:,} MB and VRAM {vram_mb:,} MB.[/dim]\n"
    )

    families = sorted({model.family for model, _runtime in recommendations})
    for family in families:
        table = Table(title=family, show_header=True)
        table.add_column("Model")
        table.add_column("Category", justify="center")
        table.add_column("Params", justify="center")
        table.add_column("Approx Q4", justify="right")
        table.add_column("Best On", justify="center")
        table.add_column("Notes")

        for model, runtime in recommendations:
            if model.family != family:
                continue
            table.add_row(
                model.name,
                model.category,
                model.params,
                format_size_mb(model.approx_q4_mb),
                runtime,
                model.tags,
            )

        console.print(table)


def _runtime_fit(size_mb: int, total_ram_mb: int, vram_mb: int, ctx_len: int) -> tuple[bool, bool, str]:
    requirement_mb = size_mb + estimate_context_overhead_mb(ctx_len)
    fits_gpu = vram_mb > 0 and requirement_mb <= int(vram_mb * 0.85)
    fits_ram = requirement_mb <= int(total_ram_mb * 0.9)

    if fits_gpu:
        return True, True, "Recommended"
    if fits_ram:
        return False, True, "CPU/RAM"
    return False, False, "May not fit"


def _pick_default_repo_file(files: list[HfGgufFile], total_ram_mb: int, vram_mb: int, ctx_len: int) -> int:
    best_index = 0
    best_score = (-1, -1)
    for index, file in enumerate(files):
        fits_gpu, fits_ram, _status = _runtime_fit(file.size_mb, total_ram_mb, vram_mb, ctx_len)
        score = (2 if fits_gpu else 1 if fits_ram else 0, file.size_mb)
        if score > best_score:
            best_score = score
            best_index = index
    return best_index


def _find_companion_projector(selected: HfGgufFile, files: list[HfGgufFile]) -> HfGgufFile | None:
    if selected.model_type != "Vision":
        return None
    stem = Path(selected.name).stem.lower()
    root = stem.split(".")[0]
    for candidate in files:
        lowered = candidate.name.lower()
        if "mmproj" not in lowered:
            continue
        if root in lowered or "vision" in lowered or "vl" in lowered:
            return candidate
    return None


def _resolve_manifest_file_type(manifest: HfRepoManifest, file: HfGgufFile) -> str:
    lowered = file.name.lower()
    if "mmproj" in lowered:
        return "Projector"
    if manifest.repo_type == "Vision":
        return "Vision"
    if "embed" in lowered or "bge-" in lowered or "nomic-embed" in lowered:
        return "Embedding"
    return "Chat"


def _download_repo_file(file: HfGgufFile, target_dir: Path, token: str) -> Path:
    target_dir.mkdir(parents=True, exist_ok=True)
    destination = target_dir / file.name
    download_with_progress(file.download_url, destination, headers=build_auth_headers(token))
    return destination


def _print_api_tables() -> None:
    chat_provider = str(state.get("chat_provider", "")).strip()
    chat_model = str(state.get("chat_model", "")).strip()
    if chat_provider:
        console.print(f"[bold]Default chat:[/bold] {chat_provider} {f'({chat_model})' if chat_model else ''}\n")

    categories = ["Cloud LLM", "Local LLM", "Utility API"]
    for category in categories:
        table = Table(title=category, show_header=True)
        table.add_column("Provider")
        table.add_column("Status", justify="center")
        table.add_column("Chat", justify="center")
        table.add_column("Base URL")
        table.add_column("Model")
        table.add_column("Auth")
        table.add_column("Notes")

        for spec in PROVIDERS.values():
            if spec.category != category:
                continue
            api_key = _provider_api_key(spec.slug)
            base_url = _provider_base_url(spec.slug)
            model = _provider_model(spec.slug)
            configured = _provider_is_configured(spec)
            if configured:
                status = "[green]Configured[/green]"
            elif spec.auth_kind == "none":
                status = "[dim]Available[/dim]"
            else:
                status = "[yellow]Not set[/yellow]"
            chat_flag = "Default" if chat_provider == spec.slug else "Yes" if spec.supports_chat else "No"
            auth_label = {
                "api_key": _mask_secret(api_key),
                "token": _mask_secret(api_key),
                "none": "None",
            }[spec.auth_kind]

            table.add_row(
                spec.label,
                status,
                chat_flag,
                base_url or "-",
                model or "-",
                auth_label,
                spec.notes,
            )

        console.print(table)

    console.print("\n[bold]Setup commands[/bold]")
    console.print("  valora api --provider openai --api-key YOUR_KEY --model gpt-4.1-mini --use-for-chat")
    console.print("  valora api --provider groq --api-key YOUR_KEY --model llama-3.3-70b-versatile --use-for-chat")
    console.print("  valora api --provider gemini --api-key YOUR_KEY --model gemini-2.5-flash --use-for-chat")
    console.print("  valora api --provider ollama --model llama3.2 --use-for-chat")
    console.print("  valora api --provider lmstudio --model your-loaded-model --use-for-chat")
    console.print("  valora api --provider vllm --model your-model-name --use-for-chat")
    console.print("  valora api --provider huggingface --api-key YOUR_HF_TOKEN")
    console.print("  valora api --provider serpapi --api-key YOUR_SERPAPI_KEY")


def _resolve_chat_runtime(provider_slug: str, model_override: str, base_url_override: str) -> tuple[ProviderSpec, str, str, str]:
    spec = get_provider(provider_slug)
    if spec is None:
        console.print(f"[bold red]Error:[/bold red] Unknown provider '{provider_slug}'.")
        raise typer.Exit(code=2)
    if not spec.supports_chat:
        console.print(f"[bold red]Error:[/bold red] Provider '{provider_slug}' is not a chat backend.")
        raise typer.Exit(code=2)

    model = model_override.strip() or _provider_model(spec.slug) or str(state.get("chat_model", "")).strip()
    if not model:
        console.print(f"[bold red]Error:[/bold red] No model is configured for '{provider_slug}'.")
        console.print("Set one with: [bold]valora api --provider ... --model ... --use-for-chat[/bold]")
        raise typer.Exit(code=2)

    base_url = base_url_override.strip() or _provider_base_url(spec.slug)
    if spec.protocol != "gemini" and not base_url:
        console.print(f"[bold red]Error:[/bold red] No base URL is configured for '{provider_slug}'.")
        raise typer.Exit(code=2)

    api_key = _provider_api_key(spec.slug)
    if spec.auth_kind in {"api_key", "token"} and not api_key:
        console.print(f"[bold red]Error:[/bold red] No API key/token is configured for '{provider_slug}'.")
        raise typer.Exit(code=2)

    return spec, model, base_url, api_key


def _run_local_llama_cli(prompt: str, system_prompt: str, requested_model: str, runtime_overrides: dict[str, str] | None = None) -> str:
    llama_cli = str(state.get("llama_cli", "")).strip()
    if not llama_cli or not Path(llama_cli).is_file():
        raise ValueError("Local llama.cpp runtime is not configured. Run `valora setup --llama.cpp`.")

    model = _pick_local_model(requested_model)
    if model is None:
        raise ValueError("No local GGUF model is available. Download one with `valora get <huggingface-repo>`.")

    runtime_profile = _resolved_local_runtime_profile(model, runtime_overrides)
    resolved_gpu_layers = runtime_profile["gpu_layers"]
    resolved_threads = runtime_profile["threads"]
    ctx_len = runtime_profile["ctx"]
    final_prompt = prompt.strip()
    if system_prompt.strip():
        final_prompt = f"System: {system_prompt.strip()}\n\nUser: {prompt.strip()}\nAssistant:"

    command = [
        llama_cli,
        "-m",
        str(model.path),
        "-c",
        ctx_len,
        "-n",
        "512",
        "-p",
        final_prompt,
    ]
    if resolved_threads.strip():
        command.extend(["-t", resolved_threads.strip()])
    normalized_gpu_layers = resolved_gpu_layers.strip().lower()
    if normalized_gpu_layers == "auto":
        normalized_gpu_layers = "-1"
    if normalized_gpu_layers:
        command.extend(["-ngl", normalized_gpu_layers])
    if runtime_profile["kv_cache_k"].strip():
        command.extend(["--cache-type-k", runtime_profile["kv_cache_k"].strip()])
    if runtime_profile["kv_cache_v"].strip():
        command.extend(["--cache-type-v", runtime_profile["kv_cache_v"].strip()])
    if model.projector:
        command.extend(["--mmproj", model.projector])

    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=300,
        check=False,
    )
    if result.returncode != 0:
        stderr = result.stderr.strip() or result.stdout.strip() or "Unknown llama.cpp error."
        raise ValueError(stderr)

    output = result.stdout.strip()
    if not output:
        raise ValueError("Local model returned an empty response.")
    return output


def _pick_fallback_chat_provider() -> tuple[str, str]:
    ready, reason = _local_chat_ready()
    if ready:
        return "valora-local", ""
    return "", reason


def _chat_openai_compatible(base_url: str, api_key: str, model: str, prompt: str, system_prompt: str) -> str:
    url = f"{base_url.rstrip('/')}/chat/completions"
    headers = {"Content-Type": "application/json"}
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"
    payload = {
        "model": model,
        "messages": (
            [{"role": "system", "content": system_prompt}] if system_prompt.strip() else []
        ) + [{"role": "user", "content": prompt}],
    }
    response = httpx.post(url, headers=headers, json=payload, timeout=120.0)
    response.raise_for_status()
    data = response.json()
    choices = data.get("choices", [])
    if not choices:
        raise ValueError("No chat choices were returned.")
    message = choices[0].get("message", {})
    content = message.get("content", "")
    if isinstance(content, list):
        return "\n".join(str(part.get("text", "")) for part in content if isinstance(part, dict)).strip()
    return str(content).strip()


def _chat_gemini(base_url: str, api_key: str, model: str, prompt: str, system_prompt: str) -> str:
    url = f"{base_url.rstrip('/')}/models/{model}:generateContent?key={api_key}"
    payload = {
        "contents": [{"role": "user", "parts": [{"text": prompt}]}],
    }
    if system_prompt.strip():
        payload["systemInstruction"] = {"parts": [{"text": system_prompt}]}
    response = httpx.post(url, json=payload, timeout=120.0)
    response.raise_for_status()
    data = response.json()
    candidates = data.get("candidates", [])
    if not candidates:
        raise ValueError("No Gemini candidates were returned.")
    parts = candidates[0].get("content", {}).get("parts", [])
    return "\n".join(str(part.get("text", "")) for part in parts if isinstance(part, dict)).strip()


def _run_chat(provider_slug: str, model_override: str, base_url_override: str, prompt: str, system_prompt: str) -> str:
    if provider_slug == "valora-local":
        return _run_local_llama_cli(prompt, system_prompt, model_override)
    spec, model, base_url, api_key = _resolve_chat_runtime(provider_slug, model_override, base_url_override)
    if spec.protocol == "gemini":
        return _chat_gemini(base_url, api_key, model, prompt, system_prompt)
    return _chat_openai_compatible(base_url, api_key, model, prompt, system_prompt)


def _hero_panel(left: Text, right: Text, title: str) -> Panel:
    return Panel(Columns([left, right], expand=True), title=title, border_style="red")


def _command_matches(user_input: str, command_help: dict[str, str]) -> list[tuple[str, str]]:
    if not user_input.startswith("/"):
        return []
    typed = user_input.lower()
    matches = [
        (command, description)
        for command, description in command_help.items()
        if command.startswith(typed)
    ]
    if matches:
        return matches[:6]
    return list(command_help.items())[:6]


def _filter_choice_matches(query: str, choices: list[tuple[str, str]]) -> list[tuple[str, str]]:
    lowered = query.strip().lower()
    if not lowered:
        return choices[:8]
    exact: list[tuple[str, str]] = []
    fuzzy: list[tuple[str, str]] = []
    for label, description in choices:
        haystack = f"{label} {description}".lower()
        if label.lower() == lowered:
            exact.append((label, description))
        elif lowered in haystack:
            fuzzy.append((label, description))
    return (exact + fuzzy)[:8]


def _print_input_area(
    user_input: str,
    command_help: dict[str, str],
    floating_items: list[tuple[str, str]] | None = None,
    prompt_prefix: str = "❯ ",
) -> None:
    console.print(Rule(style="white"))
    matches = floating_items if floating_items is not None else _command_matches(user_input, command_help)
    console.print(f"{prompt_prefix}{user_input}", style="bold white", end="")
    console.print()
    if matches:
        console.print(Rule(style="white"))
        for command, description in matches:
            console.print(f"[bold bright_blue]{command}[/bold bright_blue]  {description}")


def _ansi_write(text: str) -> None:
    console.file.write(text)
    console.file.flush()


def _begin_overlay_region() -> None:
    _ansi_write("\x1b[s")


def _refresh_overlay(
    user_input: str,
    command_help: dict[str, str],
    floating_items: list[tuple[str, str]] | None = None,
    prompt_prefix: str = "❯ ",
) -> None:
    _ansi_write("\x1b[u\x1b[J")
    _print_input_area(
        user_input,
        command_help,
        floating_items=floating_items,
        prompt_prefix=prompt_prefix,
    )


def _interactive_prompt(render_static, command_help: dict[str, str], prompt_prefix: str = "❯ ") -> str:
    buffer = ""
    render_static()
    console.print()
    _begin_overlay_region()
    while True:
        _refresh_overlay(buffer, command_help, _command_matches(buffer, command_help), prompt_prefix=prompt_prefix)
        key = _read_key()
        if key == KEY_ENTER:
            return buffer.strip()
        if key in {KEY_CTRL_C, KEY_CTRL_D}:
            raise typer.Exit(code=0)
        if key == KEY_BACKSPACE:
            buffer = buffer[:-1]
            continue
        if key == KEY_ESC:
            continue
        if len(key) == 1 and key.isprintable():
            buffer += key


def _interactive_choice_prompt(render_static, choices: list[tuple[str, str]], prompt_prefix: str = "❯ /models ") -> tuple[str, str] | None:
    buffer = ""
    render_static()
    console.print()
    _begin_overlay_region()
    while True:
        matches = _filter_choice_matches(buffer, choices)
        _refresh_overlay(buffer, {}, matches, prompt_prefix=prompt_prefix)
        key = _read_key()
        if key == KEY_ENTER:
            if not matches:
                return None
            lowered = buffer.strip().lower()
            for label, description in matches:
                if label.lower() == lowered:
                    return label, description
            return matches[0]
        if key in {KEY_CTRL_C, KEY_CTRL_D}:
            raise typer.Exit(code=0)
        if key == KEY_BACKSPACE:
            buffer = buffer[:-1]
            continue
        if key == KEY_ESC:
            return None
        if len(key) == 1 and key.isprintable():
            buffer += key


def _shorten_line(text: str, limit: int = 92) -> str:
    cleaned = " ".join(text.strip().split())
    if len(cleaned) <= limit:
        return cleaned
    return cleaned[: limit - 1].rstrip() + "…"


def _new_tool_event(kind: str, label: str, detail: str = "") -> dict[str, str]:
    return {
        "kind": kind,
        "label": label,
        "detail": detail,
        "status": "running",
        "spinner": SPINNER_FRAMES[0],
    }


def _render_tool_events(tool_events: list[dict[str, str]]) -> None:
    if not tool_events:
        return
    for event in tool_events[-6:]:
        status = event.get("status", "done")
        if status == "running":
            prefix = f"[bold yellow]{event.get('spinner', SPINNER_FRAMES[0])}[/bold yellow]"
        elif status == "done":
            prefix = "[bold green]●[/bold green]"
        else:
            prefix = "[bold red]●[/bold red]"
        console.print(f"{prefix} {event.get('label', '')}")
        detail = str(event.get("detail", "")).strip()
        if detail:
            for line in detail.splitlines()[:4]:
                console.print(f"  [dim]{_shorten_line(line)}[/dim]")
        console.print()


def _run_tool_with_animation(
    tool_events: list[dict[str, str]],
    label: str,
    detail: str,
    action,
    render_frame,
):
    event = _new_tool_event("tool", label, detail)
    tool_events.append(event)
    result_box: dict[str, object] = {"value": None, "error": None}

    def worker() -> None:
        try:
            result_box["value"] = action()
        except Exception as exc:  # noqa: BLE001
            result_box["error"] = exc

    thread = threading.Thread(target=worker, daemon=True)
    thread.start()
    frame_index = 0
    while thread.is_alive():
        event["spinner"] = SPINNER_FRAMES[frame_index % len(SPINNER_FRAMES)]
        render_frame(tool_events)
        time.sleep(0.12)
        frame_index += 1
    thread.join()

    error = result_box["error"]
    if error is not None:
        event["status"] = "error"
        event["detail"] = str(error)
        render_frame(tool_events)
        raise error

    value = result_box["value"]
    if isinstance(value, tuple) and len(value) == 2:
        summary, transcript_note = value
    else:
        summary = str(value) if value is not None else ""
        transcript_note = summary
    event["status"] = "done"
    event["detail"] = summary
    render_frame(tool_events)
    return summary, str(transcript_note).strip()


def _parse_bar_args(raw: str, expected_parts: int) -> list[str] | None:
    parts = [part.strip() for part in raw.split("|")]
    if len(parts) != expected_parts or not all(parts):
        return None
    return parts


def _read_key() -> str:
    if os.name == "nt":
        import msvcrt

        key = msvcrt.getwch()
        if key in {"\r", "\n"}:
            return KEY_ENTER
        if key == "\x08":
            return KEY_BACKSPACE
        if key == "\x03":
            return KEY_CTRL_C
        if key == "\x04":
            return KEY_CTRL_D
        if key == "\x1b":
            return KEY_ESC
        if key in {"\x00", "\xe0"}:
            extended = msvcrt.getwch()
            return f"EXT:{ord(extended)}"
        return key

    import termios
    import tty

    fd = sys.stdin.fileno()
    original = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        key = sys.stdin.read(1)
        if key in {"\r", "\n"}:
            return KEY_ENTER
        if key in {"\x7f", "\b"}:
            return KEY_BACKSPACE
        if key == "\x03":
            return KEY_CTRL_C
        if key == "\x04":
            return KEY_CTRL_D
        if key == "\x1b":
            return KEY_ESC
        return key
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, original)


def _tool_web_search(query: str) -> tuple[str, str]:
    api_key = str(state.get("mcp_serpapi_api_key", "")).strip()
    if not api_key:
        raise ValueError("Configure SerpApi first with `valora api --provider serpapi --api-key YOUR_SERPAPI_KEY`.")
    response = httpx.get(
        "https://serpapi.com/search.json",
        params={"engine": "google", "q": query, "api_key": api_key, "num": 5},
        timeout=30.0,
    )
    response.raise_for_status()
    data = response.json()
    results = data.get("organic_results", [])[:5]
    if not results:
        return ("No web results found.", "Web search returned no results.")

    lines: list[str] = []
    markdown_lines: list[str] = [f"# Web search: {query}", ""]
    for index, item in enumerate(results, start=1):
        title = str(item.get("title", "Untitled result")).strip()
        link = str(item.get("link", "")).strip()
        snippet = str(item.get("snippet", "")).strip()
        lines.append(f'{index}. {title}')
        if link:
            lines.append(link)
        if snippet:
            lines.append(snippet)
        markdown_lines.append(f"## {index}. {title}")
        if link:
            markdown_lines.append(link)
        if snippet:
            markdown_lines.append(snippet)
        markdown_lines.append("")
    return ("\n".join(lines[:9]), "\n".join(markdown_lines).strip())


def _tool_read_file(path_arg: str, session_root: Path, allowed_dirs: set[Path]) -> tuple[str, str]:
    path = _resolve_scoped_path(path_arg, session_root, allowed_dirs, "read")
    if not path.is_file():
        raise ValueError(f"File not found: {path}")
    content = path.read_text(encoding="utf-8", errors="replace")
    preview = content[:2000].strip()
    summary = f"Read {path.name}\n{preview}"
    return summary, f"Read `{path}`\n\n{preview}"


def _tool_write_file(path_arg: str, content: str, session_root: Path, allowed_dirs: set[Path]) -> tuple[str, str]:
    path = _resolve_scoped_path(path_arg, session_root, allowed_dirs, "write")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    line_count = len(content.splitlines()) or 1
    summary = f"Wrote {line_count} line(s) to {path.name}"
    return summary, f"Wrote `{path}` with {line_count} line(s)."


def _tool_edit_file(path_arg: str, find_text: str, replace_text: str, session_root: Path, allowed_dirs: set[Path]) -> tuple[str, str]:
    path = _resolve_scoped_path(path_arg, session_root, allowed_dirs, "edit")
    if not path.is_file():
        raise ValueError(f"File not found: {path}")
    content = path.read_text(encoding="utf-8", errors="replace")
    if find_text not in content:
        raise ValueError(f"Text not found in {path.name}.")
    updated = content.replace(find_text, replace_text)
    count = content.count(find_text)
    path.write_text(updated, encoding="utf-8")
    summary = f"Edited {path.name}\nReplaced {count} occurrence(s)."
    return summary, f"Edited `{path}` and replaced {count} occurrence(s)."


def _tool_make_dir(path_arg: str, session_root: Path) -> tuple[str, str]:
    path = _resolve_main_dir_path(path_arg, session_root, "create")
    path.mkdir(parents=True, exist_ok=True)
    summary = f"Created folder {path.name}"
    return summary, f"Created folder `{path}`."


def _tool_delete_path(path_arg: str, session_root: Path) -> tuple[str, str]:
    path = _resolve_main_dir_path(path_arg, session_root, "delete")
    if not path.exists():
        raise ValueError(f"Path not found: {path}")
    if path.is_dir():
        shutil.rmtree(path)
        summary = f"Deleted folder {path.name}"
        return summary, f"Deleted folder `{path}`."
    path.unlink()
    summary = f"Deleted file {path.name}"
    return summary, f"Deleted file `{path}`."


def _render_transcript(transcript: list[tuple[str, str]]) -> None:
    if not transcript:
        return

    for role, content in transcript[-10:]:
        style = "bold cyan" if role == "You" else "bold green" if role == "Assistant" else "bold yellow"
        console.print(role, style=style)
        console.print(content.strip())
        console.print()


def _build_local_setup_panel(model_name: str, runtime_profile: dict[str, str], status: str) -> Panel:
    left = Text()
    left.append("Welcome back!\n\n", style="bold white")
    left.append(f"{model_name or 'No model selected'}\n", style="white")
    left.append(f"ctx {runtime_profile['ctx']}  ", style="dim")
    gpu_label = runtime_profile["gpu_layers"]
    if gpu_label == "-1":
        gpu_label = "auto"
    left.append(f"gpu {gpu_label}  ", style="dim")
    left.append(f"thr {runtime_profile['threads']}\n", style="dim")
    left.append(f"k {runtime_profile['kv_cache_k']}  v {runtime_profile['kv_cache_v']}\n", style="dim")
    left.append(f"{_preferred_models_dir()}", style="dim")

    right = Text()
    right.append("Tips for getting started\n", style="bold red")
    right.append("Tune the local runtime, then run /start\n", style="white")
    right.append("Use /model to switch local models\n", style="white")
    right.append("Use /auto to restore best defaults\n", style="white")
    right.append("Supported KV: f16, q8_0, q4_0, q4_1\n", style="dim")
    if status.strip():
        right.append("\nRecent activity\n", style="bold red")
        right.append(status, style="white")

    return _hero_panel(left, right, f"Valora Chat v{__version__}")


def _render_local_setup_ui(
    model_name: str,
    runtime_profile: dict[str, str],
    status: str,
    user_input: str = "",
    floating_items: list[tuple[str, str]] | None = None,
    prompt_prefix: str = "> ",
    render_input: bool = True,
) -> None:
    console.clear()
    console.print(_build_local_setup_panel(model_name, runtime_profile, status))
    if render_input:
        console.print()
        _print_input_area(user_input, LOCAL_SETUP_COMMAND_HELP, floating_items=floating_items, prompt_prefix=prompt_prefix)


def _render_chat_ui(
    provider_slug: str,
    model_name: str,
    system_prompt: str,
    transcript: list[tuple[str, str]],
    status: str,
    local_runtime_profile: dict[str, str] | None = None,
    user_input: str = "",
    floating_items: list[tuple[str, str]] | None = None,
    prompt_prefix: str = "> ",
    tool_events: list[dict[str, str]] | None = None,
    render_input: bool = True,
) -> None:
    console.clear()
    left = Text()
    left.append("Welcome back!\n\n", style="bold white")
    left.append(f"{model_name or 'No model selected'}\n", style="white")
    left.append(f"{provider_slug}", style="dim")
    if provider_slug == "valora-local" and local_runtime_profile is not None:
        left.append(
            f"\nctx {local_runtime_profile['ctx']}  gpu {local_runtime_profile['gpu_layers']}  thr {local_runtime_profile['threads']}",
            style="dim",
        )

    right = Text()
    right.append("Tips for getting started\n", style="bold red")
    right.append("Use /help to view slash commands\n", style="white")
    right.append("Use /model to switch models\n", style="white")
    right.append("Use /provider to change backend\n", style="white")
    if status.strip():
        right.append("\nRecent activity\n", style="bold red")
        right.append(status, style="white")

    console.print(_hero_panel(left, right, f"Valora Chat v{__version__}"))
    console.print()
    console.print(Rule(style="white"))
    _render_tool_events(tool_events or [])
    _render_transcript(transcript)
    if render_input:
        _print_input_area(user_input, CHAT_COMMAND_HELP, floating_items=floating_items, prompt_prefix=prompt_prefix)


def _chat_help_text() -> str:
    return (
        "/help, /models, /allow-dir PATH, /web QUERY, /read PATH, /write PATH | CONTENT, /edit PATH | FIND | REPLACE, /mkdir PATH, /delete PATH, /provider NAME, /model NAME, /system TEXT, /ctx N, /gpu-layers N|-1|auto, /threads N|auto, /kv-k f16|q8_0|q4_0|q4_1, /kv-v f16|q8_0|q4_0|q4_1, /auto, /clear, /api, /save, /exit"
    )


def _local_model_short_name(model: ModelInfo) -> str:
    return model.path.stem


def _saved_cloud_model_choices() -> list[tuple[str, str, str]]:
    choices: list[tuple[str, str, str]] = []
    for spec in PROVIDERS.values():
        if not spec.supports_chat or spec.slug == "valora-local":
            continue
        configured_model = _provider_model(spec.slug)
        if not configured_model.strip():
            continue
        label = configured_model.strip()
        description = f"{spec.slug} cloud"
        choices.append((label, description, spec.slug))
    return choices


def _chat_model_picker_choices() -> tuple[list[tuple[str, str]], dict[str, tuple[str, str]]]:
    choices: list[tuple[str, str]] = []
    mapping: dict[str, tuple[str, str]] = {}

    for model in _local_models():
        short_name = _local_model_short_name(model)
        description = "local gguf"
        choices.append((short_name, description))
        mapping[short_name.lower()] = ("valora-local", model.name)

    for label, description, provider_slug in _saved_cloud_model_choices():
        dedupe_label = label
        if dedupe_label.lower() in mapping:
            dedupe_label = f"{label} [{provider_slug}]"
        choices.append((dedupe_label, description))
        mapping[dedupe_label.lower()] = (provider_slug, label)

    return choices, mapping


def _build_chat_launch_command(
    provider_slug: str,
    model_name: str,
    system_prompt: str,
    local_runtime_overrides: dict[str, str],
) -> list[str]:
    if getattr(sys, "frozen", False):
        command = [sys.executable, "chat"]
    else:
        command = [sys.executable, "-m", "valora", "chat"]

    command.extend(
        [
            "--provider",
            provider_slug,
            "--model",
            model_name,
            "--launch-chat",
        ]
    )
    if system_prompt.strip():
        command.extend(["--system-override", system_prompt.strip()])
    for option_name, key in (
        ("--ctx-override", "ctx"),
        ("--gpu-layers-override", "gpu_layers"),
        ("--threads-override", "threads"),
        ("--kv-k-override", "kv_cache_k"),
        ("--kv-v-override", "kv_cache_v"),
    ):
        value = local_runtime_overrides.get(key, "").strip()
        if value:
            command.extend([option_name, value])
    return command


def _start_chat_in_new_terminal(
    provider_slug: str,
    model_name: str,
    system_prompt: str,
    local_runtime_overrides: dict[str, str],
) -> None:
    command = _build_chat_launch_command(provider_slug, model_name, system_prompt, local_runtime_overrides)
    command_line = subprocess.list2cmdline(command)
    powershell_command = f"& {command_line}"
    child_env = os.environ.copy()
    child_env["PYINSTALLER_RESET_ENVIRONMENT"] = "1"
    child_env.pop("PYTHONHOME", None)
    child_env.pop("PYTHONPATH", None)
    subprocess.Popen(
        [
            "powershell.exe",
            "-NoExit",
            "-Command",
            powershell_command,
        ],
        cwd=os.getcwd(),
        env=child_env,
        creationflags=getattr(subprocess, "CREATE_NEW_CONSOLE", 0),
    )


def register_other_commands(app: typer.Typer) -> None:
    @app.command("version")
    def version_command() -> None:
        console.print(__version__)

    @app.command("info")
    def info_command() -> None:
        load_config()
        memory = psutil.virtual_memory()
        available_ram_mb = int(memory.available / (1024 * 1024))
        total_ram_mb = int(memory.total / (1024 * 1024))
        used_ram_mb = total_ram_mb - available_ram_mb
        gpu_name = get_gpu_name()
        vram_mb = get_total_vram_mb()
        vulkan = detect_vulkan_support()

        console.print("[bold]=== System Information ===[/bold]\n")
        console.print("[bold]Memory:[/bold]")
        console.print(f"  Available RAM:  {available_ram_mb:,} MB")
        console.print(f"  Total RAM:      {total_ram_mb:,} MB")
        console.print(f"  Used RAM:       {used_ram_mb:,} MB\n")

        console.print("[bold]GPU:[/bold]")
        console.print(f"  Name:   {gpu_name}")
        console.print(f"  VRAM:   {vram_mb:,} MB")
        console.print(f"  Vulkan: {'Supported [green]Yes[/green]' if vulkan else 'Not detected'}\n")

        console.print("[bold]Config:[/bold]")
        console.print(f"  Models folder:  {state['folder'] or 'Not configured'}")
        console.print(f"  llama.cpp path: {state['llama_cpp_path'] or 'Not configured'}")
        console.print(f"  Server:         {state['server'] or 'Not configured'}")
        console.print(f"  Config file:    {get_config_path()}")
        console.print(f"  Chat provider:  {state.get('chat_provider') or 'Not configured'}")
        console.print(f"  Chat model:     {state.get('chat_model') or 'Not configured'}\n")

        console.print("[bold]API:[/bold]")
        console.print(f"  HF token:       {_mask_secret(state['hf_api_token'])}\n")

        console.print("[bold]MCP:[/bold]")
        console.print(f"  Enabled:        {'Yes' if state['mcp_enabled'] else 'No'}")
        console.print(f"  API key:        {_mask_secret(state['mcp_serpapi_api_key'])}")
        console.print(f"  Server URL:     {state['mcp_server_url'] or 'Not configured'}\n")

        console.print("[bold]Server:[/bold]")
        running = "Running" if state["model_loaded"] else "Stopped"
        model = state["selected_model"] or "No model loaded"
        console.print(f"  Status:  [{running}] {model} on port {state['port']}")

    @app.command("models")
    def models_command() -> None:
        load_config()
        folder = state["folder"]
        memory = psutil.virtual_memory()
        total_ram_mb = int(memory.total / (1024 * 1024))
        ctx_len = _get_ctx_len()
        vram_mb = get_total_vram_mb()

        if not folder:
            console.print("[yellow]No models directory is configured yet.[/yellow]\n")
            _print_recommended_models(total_ram_mb, vram_mb, ctx_len)
            raise typer.Exit(code=0)

        models = scan_models(folder)
        if not models:
            console.print(f"[yellow]No GGUF models were found in {folder}.[/yellow]\n")
            _print_recommended_models(total_ram_mb, vram_mb, ctx_len)
            raise typer.Exit(code=0)

        table = Table(show_header=True)
        table.add_column("#", justify="right")
        table.add_column("Model")
        table.add_column("Size", justify="right")
        table.add_column("Quant", justify="center")
        table.add_column("Type", justify="center")
        table.add_column("Usable", justify="center")

        for index, model in enumerate(models, start=1):
            usable = is_model_usable(model, vram_mb)
            usable_symbol = "[green]Yes[/green]" if usable else "[red]No[/red]"
            style = "red" if not usable else ""
            table.add_row(
                str(index),
                model.name,
                format_size_mb(model.size_mb),
                model.quant,
                model.model_type,
                usable_symbol,
                style=style,
            )

        console.print(table)
        console.print(f"\nTotal: {len(models)} models | Available VRAM: {vram_mb:,} MB | Default context: {ctx_len}")

    @app.command("get")
    @app.command("download")
    def get_command(repo: str = typer.Argument(..., help="Hugging Face repo id or URL with GGUF files.")) -> None:
        load_config()
        repo_id = normalize_repo_id(repo)
        token = str(state.get("hf_api_token", "")).strip()
        target_dir = _preferred_models_dir()
        memory = psutil.virtual_memory()
        total_ram_mb = int(memory.total / (1024 * 1024))
        ctx_len = _get_ctx_len()
        vram_mb = get_total_vram_mb()

        try:
            manifest = fetch_repo_manifest(repo_id, token=token)
        except httpx.HTTPStatusError as exc:
            status_code = exc.response.status_code
            if status_code in {401, 403}:
                console.print("[bold red]Error:[/bold red] This repo needs authentication or your token was rejected.")
                console.print("Save a token with: [bold]valora api --provider huggingface --api-key YOUR_TOKEN[/bold]")
            elif status_code == 404:
                console.print(f"[bold red]Error:[/bold red] Repo not found: {repo_id}")
            else:
                console.print(f"[bold red]Error:[/bold red] Hugging Face returned HTTP {status_code} for {repo_id}.")
            raise typer.Exit(code=2)
        except httpx.HTTPError as exc:
            console.print(f"[bold red]Error:[/bold red] Failed to reach Hugging Face: {exc}")
            raise typer.Exit(code=2)

        files = manifest.gguf_files
        if not files:
            console.print(f"[yellow]No GGUF files were found in {repo_id}.[/yellow]")
            raise typer.Exit(code=1)

        for file in files:
            file.model_type = _resolve_manifest_file_type(manifest, file)

        selectable_files = [file for file in files if file.model_type != "Projector"]
        if not selectable_files:
            console.print(f"[yellow]No downloadable GGUF model files were found in {repo_id}.[/yellow]")
            raise typer.Exit(code=1)

        default_index = _pick_default_repo_file(selectable_files, total_ram_mb, vram_mb, ctx_len)

        console.print(f"[bold]{repo_id}[/bold]")
        console.print(
            f"[dim]Target folder: {target_dir} | Context: {ctx_len} | RAM: {total_ram_mb:,} MB | VRAM: {vram_mb:,} MB | Repo type: {manifest.repo_type}[/dim]\n"
        )

        table = Table(show_header=True)
        table.add_column("#", justify="right")
        table.add_column("File")
        table.add_column("Quant", justify="center")
        table.add_column("Size", justify="right")
        table.add_column("Type", justify="center")
        table.add_column("Fit", justify="center")
        table.add_column("Note")

        for index, file in enumerate(selectable_files, start=1):
            fits_gpu, fits_ram, status = _runtime_fit(file.size_mb, total_ram_mb, vram_mb, ctx_len)
            if fits_gpu:
                fit_label = "[green]GPU[/green]"
                note = "recommended"
                style = ""
            elif fits_ram:
                fit_label = "[yellow]CPU/RAM[/yellow]"
                note = "fits, slower"
                style = ""
            else:
                fit_label = "[red]No[/red]"
                note = "likely too large"
                style = "red"

            if index - 1 == default_index and status != "May not fit":
                note = f"{note}, default"

            table.add_row(
                str(index),
                file.name,
                file.quant,
                format_size_mb(file.size_mb),
                file.model_type,
                fit_label,
                note,
                style=style,
            )

        console.print(table)

        choice = typer.prompt("Select file number to download", default=str(default_index + 1)).strip()
        try:
            selected_index = int(choice) - 1
            selected = selectable_files[selected_index]
        except (ValueError, IndexError):
            console.print("[bold red]Error:[/bold red] Invalid selection.")
            raise typer.Exit(code=2)

        _fits_gpu, fits_ram, _status = _runtime_fit(selected.size_mb, total_ram_mb, vram_mb, ctx_len)
        if not fits_ram and not typer.confirm("This file may not fit well on this device. Download anyway?", default=False):
            console.print("[yellow]Download cancelled.[/yellow]")
            raise typer.Exit(code=0)

        if not state.get("folder"):
            state["folder"] = str(target_dir)
            save_config()

        console.print(f"\nDownloading [bold]{selected.name}[/bold] ...")
        try:
            saved_path = _download_repo_file(selected, target_dir, token)
        except httpx.HTTPStatusError as exc:
            console.print(f"[bold red]Error:[/bold red] Download failed with HTTP {exc.response.status_code}.")
            raise typer.Exit(code=2)
        except httpx.HTTPError as exc:
            console.print(f"[bold red]Error:[/bold red] Download failed: {exc}")
            raise typer.Exit(code=2)

        console.print(f"[green]Saved:[/green] {saved_path}")

        projector = _find_companion_projector(selected, files)
        if projector:
            console.print(f"Downloading [bold]{projector.name}[/bold] ...")
            try:
                projector_path = _download_repo_file(projector, target_dir, token)
            except httpx.HTTPStatusError as exc:
                console.print(f"[bold red]Error:[/bold red] Projector download failed with HTTP {exc.response.status_code}.")
                raise typer.Exit(code=2)
            except httpx.HTTPError as exc:
                console.print(f"[bold red]Error:[/bold red] Projector download failed: {exc}")
                raise typer.Exit(code=2)
            console.print(f"[green]Saved:[/green] {projector_path}")

    @app.command("chat")
    def chat_command(
        prompt: str = typer.Argument("", help="Message to send. If omitted, Valora opens a simple chat prompt."),
        provider: str = typer.Option("", "--provider", help="Configured provider slug such as openai, groq, ollama, lmstudio, vllm, gemini."),
        model: str = typer.Option("", "--model", help="Override the configured model for this chat request."),
        base_url: str = typer.Option("", "--base-url", help="Override the configured base URL for this chat request."),
        system: str = typer.Option("", "--system", help="Optional system prompt."),
        launch_chat: bool = typer.Option(False, "--launch-chat", hidden=True),
        ctx_override: str = typer.Option("", "--ctx-override", hidden=True),
        gpu_layers_override: str = typer.Option("", "--gpu-layers-override", hidden=True),
        threads_override: str = typer.Option("", "--threads-override", hidden=True),
        kv_k_override: str = typer.Option("", "--kv-k-override", hidden=True),
        kv_v_override: str = typer.Option("", "--kv-v-override", hidden=True),
        system_override: str = typer.Option("", "--system-override", hidden=True),
    ) -> None:
        load_config()
        provider_slug = provider.strip() or str(state.get("chat_provider", "")).strip()
        if not provider_slug:
            fallback_provider, fallback_reason = _pick_fallback_chat_provider()
            if fallback_provider:
                provider_slug = fallback_provider
            else:
                console.print("[bold red]Error:[/bold red] No default chat provider is configured.")
                console.print(fallback_reason)
                console.print(
                    "Or configure a cloud/local API with: [bold]valora api --provider ... --model ... --use-for-chat[/bold]"
                )
                raise typer.Exit(code=2)

        if system_override.strip():
            system = system_override.strip()

        hidden_local_overrides = {
            "ctx": ctx_override.strip(),
            "gpu_layers": gpu_layers_override.strip(),
            "threads": threads_override.strip(),
            "kv_cache_k": kv_k_override.strip(),
            "kv_cache_v": kv_v_override.strip(),
        }

        if prompt.strip():
            user_message = prompt
            try:
                if provider_slug == "valora-local":
                    reply = _run_local_llama_cli(user_message, system, model, hidden_local_overrides)
                else:
                    reply = _run_chat(provider_slug, model, base_url, user_message, system)
            except httpx.HTTPError as exc:
                console.print(f"[bold red]Error:[/bold red] Chat request failed: {exc}")
                raise typer.Exit(code=2)
            except ValueError as exc:
                console.print(f"[bold red]Error:[/bold red] {exc}")
                raise typer.Exit(code=2)
            console.print(reply)
            raise typer.Exit(code=0)

        session_provider = provider_slug
        session_model = model.strip() or _provider_model(session_provider) or str(state.get("chat_model", "")).strip()
        session_root = _normalize_path(Path.cwd())
        allowed_dirs: set[Path] = {session_root}
        if session_provider == "valora-local" and not session_model:
            local_model = _pick_local_model()
            session_model = local_model.name if local_model else ""
        session_base_url = base_url.strip()
        session_system = system.strip()
        saved_local_default = (
            str(state.get("chat_provider", "")).strip() == "valora-local"
            and bool(str(state.get("chat_model", "")).strip())
        )
        local_runtime_overrides = {
            "ctx": hidden_local_overrides["ctx"] or str(state.get("ctx", "4096")).strip(),
            "gpu_layers": hidden_local_overrides["gpu_layers"] or str(state.get("gpu_layers", "auto")).strip(),
            "threads": hidden_local_overrides["threads"] or str(state.get("threads", "auto")).strip(),
            "kv_cache_k": hidden_local_overrides["kv_cache_k"] or str(state.get("kv_cache_k", "f16")).strip(),
            "kv_cache_v": hidden_local_overrides["kv_cache_v"] or str(state.get("kv_cache_v", "f16")).strip(),
        }

        if session_provider == "valora-local" and not launch_chat and not saved_local_default:
            local_model = _pick_local_model(session_model)
            if local_model is None:
                console.print("[bold red]Error:[/bold red] No local GGUF model is available.")
                console.print("Download one with: [bold]valora get <huggingface-repo>[/bold]")
                raise typer.Exit(code=2)

            status_message = "Tune local settings, then use /start to launch chat."
            while True:
                runtime_profile = _resolved_local_runtime_profile(local_model, local_runtime_overrides)
                user_message = _interactive_prompt(
                    lambda: _render_local_setup_ui(
                        local_model.name,
                        runtime_profile,
                        status_message,
                        render_input=False,
                    ),
                    LOCAL_SETUP_COMMAND_HELP,
                    prompt_prefix="❯ ",
                )
                if not user_message:
                    continue
                if user_message.lower() == ".start":
                    user_message = "/start"
                if user_message.lower() in {"exit", "quit", "/exit", "/quit"}:
                    raise typer.Exit(code=0)
                if not user_message.startswith("/"):
                    status_message = "Use /start to launch chat after reviewing the configuration."
                    continue

                command, _, arg = user_message[1:].partition(" ")
                command = command.strip().lower()
                arg = arg.strip()

                if command in {"help", "?"}:
                    status_message = "/ctx N, /gpu-layers N|-1|auto, /threads N|auto, /kv-k f16|q8_0|q4_0|q4_1, /kv-v f16|q8_0|q4_0|q4_1, /auto, /model NAME, /save, /start, /exit"
                    continue
                if command == "models":
                    choices, mapping = _chat_model_picker_choices()
                    local_choices = [(label, description) for label, description in choices if description == "local gguf"]
                    if not local_choices:
                        status_message = "No local models are available."
                        continue
                    picked = _interactive_choice_prompt(
                        lambda: _render_local_setup_ui(
                            local_model.name,
                            runtime_profile,
                            "Search local models and press Enter.",
                            render_input=False,
                        ),
                        local_choices,
                        prompt_prefix="❯ /models ",
                    )
                    if picked is None:
                        status_message = "Model search cancelled."
                        continue
                    target = mapping.get(picked[0].lower())
                    if target is None:
                        status_message = "Model selection failed."
                        continue
                    picked_model = _pick_local_model(target[1])
                    if picked_model is None:
                        status_message = f"Local model not found: {picked[0]}"
                        continue
                    local_model = picked_model
                    session_model = picked_model.name
                    state["selected_model"] = picked_model.name
                    state["model_loaded"] = True
                    save_config()
                    status_message = f"Using local model {picked[0]}."
                    continue
                if command == "auto":
                    local_runtime_overrides = {
                        "ctx": "auto",
                        "gpu_layers": "auto",
                        "threads": "auto",
                        "kv_cache_k": "f16",
                        "kv_cache_v": "f16",
                    }
                    status_message = "Restored the best automatic local configuration."
                    continue
                if command == "ctx":
                    status_message = f"Current context: {runtime_profile['ctx']}" if not arg else f"Context set to {arg}."
                    if arg:
                        local_runtime_overrides["ctx"] = arg
                    continue
                if command == "gpu-layers":
                    status_message = f"Current GPU layers: {runtime_profile['gpu_layers']}" if not arg else f"GPU layers set to {arg}."
                    if arg:
                        local_runtime_overrides["gpu_layers"] = arg
                    continue
                if command == "threads":
                    status_message = f"Current threads: {runtime_profile['threads']}" if not arg else f"Threads set to {arg}."
                    if arg:
                        local_runtime_overrides["threads"] = arg
                    continue
                if command == "kv-k":
                    if not arg:
                        status_message = f"Current KV cache K: {runtime_profile['kv_cache_k']}"
                        continue
                    lowered = arg.lower()
                    if lowered not in LOCAL_KV_CACHE_CHOICES:
                        status_message = "KV cache K must be one of: " + ", ".join(LOCAL_KV_CACHE_CHOICES)
                        continue
                    local_runtime_overrides["kv_cache_k"] = lowered
                    status_message = f"KV cache K set to {lowered}."
                    continue
                if command == "kv-v":
                    if not arg:
                        status_message = f"Current KV cache V: {runtime_profile['kv_cache_v']}"
                        continue
                    lowered = arg.lower()
                    if lowered not in LOCAL_KV_CACHE_CHOICES:
                        status_message = "KV cache V must be one of: " + ", ".join(LOCAL_KV_CACHE_CHOICES)
                        continue
                    local_runtime_overrides["kv_cache_v"] = lowered
                    status_message = f"KV cache V set to {lowered}."
                    continue
                if command == "model":
                    if not arg:
                        status_message = f"Current model: {local_model.name}"
                        continue
                    picked = _pick_local_model(arg)
                    if picked is None:
                        status_message = f"Local model not found: {arg}"
                        continue
                    local_model = picked
                    session_model = picked.name
                    state["selected_model"] = picked.name
                    state["model_loaded"] = True
                    save_config()
                    status_message = f"Using local model {_local_model_short_name(picked)}."
                    continue
                if command == "save":
                    state["chat_provider"] = "valora-local"
                    state["chat_model"] = local_model.name
                    state["ctx"] = local_runtime_overrides.get("ctx", state["ctx"])
                    state["gpu_layers"] = local_runtime_overrides.get("gpu_layers", state["gpu_layers"])
                    state["threads"] = local_runtime_overrides.get("threads", state["threads"])
                    state["kv_cache_k"] = local_runtime_overrides.get("kv_cache_k", state["kv_cache_k"])
                    state["kv_cache_v"] = local_runtime_overrides.get("kv_cache_v", state["kv_cache_v"])
                    save_config()
                    status_message = "Saved local chat settings."
                    continue
                if command == "start":
                    state["selected_model"] = local_model.name
                    save_config()
                    _start_chat_in_new_terminal("valora-local", local_model.name, session_system, local_runtime_overrides)
                    console.print("[green]Launched local chat in a new terminal.[/green]")
                    raise typer.Exit(code=0)

                status_message = f"Unknown command: /{command}"

        transcript: list[tuple[str, str]] = []
        tool_events: list[dict[str, str]] = []
        status_message = "Using local llama.cpp model." if session_provider == "valora-local" else "Ready."

        while True:
            local_runtime_profile = None
            if session_provider == "valora-local":
                local_model = _pick_local_model(session_model)
                if local_model is not None:
                    local_runtime_profile = _resolved_local_runtime_profile(local_model, local_runtime_overrides)
            user_message = _interactive_prompt(
                lambda: _render_chat_ui(
                    session_provider,
                    session_model,
                    session_system,
                    transcript,
                    status_message,
                    local_runtime_profile,
                    tool_events=tool_events,
                    render_input=False,
                ),
                CHAT_COMMAND_HELP,
                prompt_prefix="❯ ",
            )
            if user_message.lower() in {"exit", "quit"}:
                break
            if not user_message:
                continue
            if user_message.startswith("/"):
                command, _, arg = user_message[1:].partition(" ")
                command = command.strip().lower()
                arg = arg.strip()

                if command in {"exit", "quit"}:
                    break
                if command in {"help", "?"}:
                    status_message = _chat_help_text()
                    continue
                if command == "allow-dir":
                    if not arg:
                        extras = [str(path) for path in sorted(allowed_dirs) if path != session_root]
                        status_message = (
                            f"Current tool scope: {session_root}"
                            if not extras
                            else f"Current tool scope: {session_root} | Extra: {', '.join(extras)}"
                        )
                        continue
                    raw_candidate = Path(arg).expanduser()
                    if not raw_candidate.is_absolute():
                        raw_candidate = session_root / raw_candidate
                    candidate = _normalize_path(raw_candidate)
                    for protected in _protected_roots():
                        if _is_relative_to(candidate, protected) or candidate == protected:
                            status_message = f"Access is blocked for protected system paths like {protected}."
                            break
                    else:
                        allowed_dirs.add(candidate)
                        status_message = f"Approved extra directory: {candidate}"
                    continue
                if command == "web":
                    if not arg:
                        status_message = "Usage: /web your search query"
                        continue
                    try:
                        _summary, transcript_note = _run_tool_with_animation(
                            tool_events,
                            f'Web Search("{arg}")',
                            "Searching the web with SerpApi",
                            lambda: _tool_web_search(arg),
                            lambda current_events: _render_chat_ui(
                                session_provider,
                                session_model,
                                session_system,
                                transcript,
                                "Running web search...",
                                local_runtime_profile,
                                "",
                                tool_events=current_events,
                            ),
                        )
                    except (httpx.HTTPError, ValueError) as exc:
                        transcript.append(("Tool", f"Web Search failed: {exc}"))
                        status_message = "Web search failed."
                        continue
                    transcript.append(("Tool", transcript_note))
                    status_message = "Web search completed."
                    continue
                if command == "read":
                    if not arg:
                        status_message = "Usage: /read path/to/file"
                        continue
                    try:
                        _summary, transcript_note = _run_tool_with_animation(
                            tool_events,
                            f"Read({arg})",
                            "Reading local file",
                            lambda: _tool_read_file(arg, session_root, allowed_dirs),
                            lambda current_events: _render_chat_ui(
                                session_provider,
                                session_model,
                                session_system,
                                transcript,
                                "Reading file...",
                                local_runtime_profile,
                                "",
                                tool_events=current_events,
                            ),
                        )
                    except ValueError as exc:
                        transcript.append(("Tool", f"Read failed: {exc}"))
                        status_message = "Read failed."
                        continue
                    transcript.append(("Tool", transcript_note))
                    status_message = "Read completed."
                    continue
                if command == "write":
                    parts = _parse_bar_args(arg, 2)
                    if parts is None:
                        status_message = "Usage: /write path/to/file | content"
                        continue
                    target_path, content = parts
                    try:
                        _summary, transcript_note = _run_tool_with_animation(
                            tool_events,
                            f"Write({target_path})",
                            "Creating or replacing file contents",
                            lambda: _tool_write_file(target_path, content, session_root, allowed_dirs),
                            lambda current_events: _render_chat_ui(
                                session_provider,
                                session_model,
                                session_system,
                                transcript,
                                "Writing file...",
                                local_runtime_profile,
                                "",
                                tool_events=current_events,
                            ),
                        )
                    except ValueError as exc:
                        transcript.append(("Tool", f"Write failed: {exc}"))
                        status_message = "Write failed."
                        continue
                    transcript.append(("Tool", transcript_note))
                    status_message = "Write completed."
                    continue
                if command == "edit":
                    parts = _parse_bar_args(arg, 3)
                    if parts is None:
                        status_message = "Usage: /edit path/to/file | find text | replace text"
                        continue
                    target_path, find_text, replace_text = parts
                    try:
                        _summary, transcript_note = _run_tool_with_animation(
                            tool_events,
                            f"Edit({target_path})",
                            "Applying text replacement",
                            lambda: _tool_edit_file(target_path, find_text, replace_text, session_root, allowed_dirs),
                            lambda current_events: _render_chat_ui(
                                session_provider,
                                session_model,
                                session_system,
                                transcript,
                                "Editing file...",
                                local_runtime_profile,
                                "",
                                tool_events=current_events,
                            ),
                        )
                    except ValueError as exc:
                        transcript.append(("Tool", f"Edit failed: {exc}"))
                        status_message = "Edit failed."
                        continue
                    transcript.append(("Tool", transcript_note))
                    status_message = "Edit completed."
                    continue
                if command == "mkdir":
                    if not arg:
                        status_message = "Usage: /mkdir folder/path"
                        continue
                    try:
                        _summary, transcript_note = _run_tool_with_animation(
                            tool_events,
                            f"Create Folder({arg})",
                            "Creating folder in the main chat directory",
                            lambda: _tool_make_dir(arg, session_root),
                            lambda current_events: _render_chat_ui(
                                session_provider,
                                session_model,
                                session_system,
                                transcript,
                                "Creating folder...",
                                local_runtime_profile,
                                "",
                                tool_events=current_events,
                            ),
                        )
                    except ValueError as exc:
                        transcript.append(("Tool", f"Create folder failed: {exc}"))
                        status_message = "Create folder failed."
                        continue
                    transcript.append(("Tool", transcript_note))
                    status_message = "Folder created."
                    continue
                if command in {"delete", "del", "rm", "remove"}:
                    if not arg:
                        status_message = "Usage: /delete file-or-folder"
                        continue
                    try:
                        _summary, transcript_note = _run_tool_with_animation(
                            tool_events,
                            f"Delete({arg})",
                            "Deleting from the main chat directory",
                            lambda: _tool_delete_path(arg, session_root),
                            lambda current_events: _render_chat_ui(
                                session_provider,
                                session_model,
                                session_system,
                                transcript,
                                "Deleting path...",
                                local_runtime_profile,
                                "",
                                tool_events=current_events,
                            ),
                        )
                    except ValueError as exc:
                        transcript.append(("Tool", f"Delete failed: {exc}"))
                        status_message = "Delete failed."
                        continue
                    transcript.append(("Tool", transcript_note))
                    status_message = "Delete completed."
                    continue
                if command == "models":
                    choices, mapping = _chat_model_picker_choices()
                    if not choices:
                        status_message = "No saved local or cloud models are available."
                        continue
                    picked = _interactive_choice_prompt(
                        lambda: _render_chat_ui(
                            session_provider,
                            session_model,
                            session_system,
                            transcript,
                            "Search models and press Enter.",
                            local_runtime_profile,
                            tool_events=tool_events,
                            render_input=False,
                        ),
                        choices,
                        prompt_prefix="❯ /models ",
                    )
                    if picked is None:
                        status_message = "Model search cancelled."
                        continue
                    target = mapping.get(picked[0].lower())
                    if target is None:
                        status_message = "Model selection failed."
                        continue
                    next_provider, next_model = target
                    if next_provider == "valora-local":
                        previous_model = session_model
                        session_provider = "valora-local"
                        session_model = next_model
                        state["selected_model"] = next_model
                        state["model_loaded"] = True
                        save_config()
                        previous_short = previous_model
                        if previous_model:
                            existing = _pick_local_model(previous_model)
                            previous_short = _local_model_short_name(existing) if existing else previous_model
                        status_message = f"Switched local model from {previous_short or 'none'} to {picked[0]}. Next reply will use the new GPU load."
                    else:
                        session_provider = next_provider
                        session_model = next_model
                        session_base_url = _provider_base_url(next_provider)
                        state["model_loaded"] = False
                        save_config()
                        status_message = f"Using saved cloud model {picked[0]} via {next_provider}."
                    continue
                if command == "clear":
                    transcript.clear()
                    status_message = "Transcript cleared."
                    continue
                if command == "auto":
                    local_runtime_overrides = {
                        "ctx": "auto",
                        "gpu_layers": "auto",
                        "threads": "auto",
                        "kv_cache_k": "f16",
                        "kv_cache_v": "f16",
                    }
                    status_message = "Restored the best automatic local configuration."
                    continue
                if command == "provider":
                    if not arg:
                        status_message = "Providers: " + ", ".join(
                            spec.slug for spec in PROVIDERS.values() if spec.supports_chat
                        )
                        continue
                    spec = get_provider(arg)
                    if spec is None or not spec.supports_chat:
                        status_message = f"Unknown chat provider: {arg}"
                        continue
                    session_provider = spec.slug
                    session_model = _provider_model(session_provider) or session_model
                    if session_provider == "valora-local" and not session_model:
                        local_model = _pick_local_model()
                        session_model = local_model.name if local_model else ""
                        if local_model is not None:
                            state["selected_model"] = local_model.name
                            state["model_loaded"] = True
                    elif session_provider != "valora-local":
                        state["model_loaded"] = False
                    session_base_url = ""
                    save_config()
                    status_message = f"Using provider {session_provider}."
                    continue
                if command == "model":
                    if not arg:
                        status_message = f"Current model: {session_model or 'Not configured'}"
                        continue
                    if session_provider == "valora-local":
                        picked = _pick_local_model(arg)
                        if picked is None:
                            status_message = f"Local model not found: {arg}"
                            continue
                        session_model = picked.name
                        state["selected_model"] = picked.name
                        state["model_loaded"] = True
                        save_config()
                        status_message = f"Switched local model to {_local_model_short_name(picked)}."
                        continue
                    session_model = arg
                    status_message = f"Using model {session_model}."
                    continue
                if command == "ctx":
                    if not arg:
                        status_message = f"Current context: {local_runtime_overrides.get('ctx', 'auto')}"
                        continue
                    local_runtime_overrides["ctx"] = arg
                    status_message = f"Context set to {arg}."
                    continue
                if command == "gpu-layers":
                    if not arg:
                        status_message = f"Current GPU layers: {local_runtime_overrides.get('gpu_layers', 'auto')}"
                        continue
                    local_runtime_overrides["gpu_layers"] = arg
                    status_message = f"GPU layers set to {arg}."
                    continue
                if command == "threads":
                    if not arg:
                        status_message = f"Current threads: {local_runtime_overrides.get('threads', 'auto')}"
                        continue
                    local_runtime_overrides["threads"] = arg
                    status_message = f"Threads set to {arg}."
                    continue
                if command == "kv-k":
                    if not arg:
                        status_message = f"Current KV cache K: {local_runtime_overrides.get('kv_cache_k', 'f16')}"
                        continue
                    local_runtime_overrides["kv_cache_k"] = arg
                    status_message = f"KV cache K set to {arg}."
                    continue
                if command == "kv-v":
                    if not arg:
                        status_message = f"Current KV cache V: {local_runtime_overrides.get('kv_cache_v', 'f16')}"
                        continue
                    local_runtime_overrides["kv_cache_v"] = arg
                    status_message = f"KV cache V set to {arg}."
                    continue
                if command == "system":
                    session_system = "" if arg.lower() in {"off", "clear", "none"} else arg
                    status_message = "System prompt cleared." if not session_system else "System prompt updated."
                    continue
                if command == "save":
                    state["chat_provider"] = session_provider
                    state["chat_model"] = session_model
                    state["ctx"] = local_runtime_overrides.get("ctx", state["ctx"])
                    state["gpu_layers"] = local_runtime_overrides.get("gpu_layers", state["gpu_layers"])
                    state["threads"] = local_runtime_overrides.get("threads", state["threads"])
                    state["kv_cache_k"] = local_runtime_overrides.get("kv_cache_k", state["kv_cache_k"])
                    state["kv_cache_v"] = local_runtime_overrides.get("kv_cache_v", state["kv_cache_v"])
                    save_config()
                    status_message = f"Saved {session_provider} / {session_model} as the default chat backend."
                    continue
                if command == "api":
                    console.clear()
                    _print_api_tables()
                    typer.prompt("Press Enter to return", default="", show_default=False)
                    status_message = "Returned from API table."
                    continue
                status_message = f"Unknown command: /{command}"
                continue

            transcript.append(("You", user_message))
            try:
                if session_provider == "valora-local":
                    reply = _run_local_llama_cli(user_message, session_system, session_model, local_runtime_overrides)
                else:
                    reply = _run_chat(session_provider, session_model, session_base_url, user_message, session_system)
            except httpx.HTTPError as exc:
                transcript.append(("System", f"Chat request failed: {exc}"))
                status_message = "Request failed."
                continue
            except ValueError as exc:
                transcript.append(("System", str(exc)))
                status_message = "Model returned an invalid response."
                continue
            transcript.append(("Assistant", reply))
            status_message = f"Replied with {session_provider}."

    @app.command("status")
    def status_command() -> None:
        load_config()
        running = state["model_loaded"]
        if running:
            console.print(f"[green]Running[/green] {state['selected_model'] or 'unknown model'} on port {state['port']}")
        else:
            console.print("[yellow]No local server is marked as running in config.[/yellow]")

    @app.command("stop")
    @app.command("kill")
    def stop_command() -> None:
        load_config()
        if not state["model_loaded"]:
            console.print("[yellow]No local server is marked as running.[/yellow]")
            raise typer.Exit(code=0)

        state["model_loaded"] = False
        state["selected_model"] = ""
        save_config()
        console.print("[green]Server state cleared.[/green]")

    @app.command("config-path")
    def config_path_command() -> None:
        console.print(str(get_config_path()))

    @app.command("api")
    def api_command(
        provider: str = typer.Option("", "--provider", help="Provider slug to configure, such as openai, groq, gemini, ollama, lmstudio, vllm, huggingface, serpapi."),
        api_key: str = typer.Option("", "--api-key", help="API key or token to save for the selected provider."),
        base_url: str = typer.Option("", "--base-url", help="Base URL override for the selected provider."),
        model: str = typer.Option("", "--model", help="Default model for the selected provider."),
        use_for_chat: bool = typer.Option(False, "--use-for-chat", help="Make this provider the default Valora chat backend."),
        clear_provider: bool = typer.Option(False, "--clear-provider", help="Remove the selected provider configuration."),
    ) -> None:
        load_config()
        provider_slug = provider.strip().lower()

        if not provider_slug:
            _print_api_tables()
            console.print(
                "\n[dim]Example: valora api --provider groq --api-key YOUR_KEY --model llama-3.3-70b-versatile --use-for-chat[/dim]"
            )
            raise typer.Exit(code=0)

        spec = get_provider(provider_slug)
        if spec is None:
            console.print(f"[bold red]Error:[/bold red] Unknown provider '{provider_slug}'.")
            raise typer.Exit(code=2)

        if clear_provider:
            _save_provider_profile(provider_slug, {})
            if provider_slug == "huggingface":
                state["hf_api_token"] = ""
            if provider_slug == "serpapi":
                state["mcp_serpapi_api_key"] = ""
                state["mcp_enabled"] = False
                state["mcp_server_url"] = ""
            if state.get("chat_provider") == provider_slug:
                state["chat_provider"] = ""
                state["chat_model"] = ""
            save_config()
            console.print(f"[green]{spec.label} configuration cleared.[/green]")
            raise typer.Exit(code=0)

        profile = _get_provider_profile(provider_slug)
        if api_key.strip():
            profile["api_key"] = api_key.strip()
        if base_url.strip():
            profile["base_url"] = base_url.strip()
        if model.strip():
            profile["model"] = model.strip()

        if not any((api_key.strip(), base_url.strip(), model.strip(), use_for_chat)):
            console.print(f"[bold]{spec.label}[/bold]")
            console.print(f"  Category: {spec.category}")
            console.print(f"  Base URL: {_provider_base_url(provider_slug) or 'Not configured'}")
            console.print(f"  Model:    {_provider_model(provider_slug) or 'Not configured'}")
            console.print(f"  Auth:     {_mask_secret(_provider_api_key(provider_slug)) if spec.auth_kind != 'none' else 'None'}")
            console.print(f"  Chat:     {'Default' if state.get('chat_provider') == provider_slug else ('Available' if spec.supports_chat else 'No')}")
            if provider_slug == "huggingface":
                console.print("  Save:     valora api --provider huggingface --api-key YOUR_HF_TOKEN")
            elif provider_slug == "serpapi":
                console.print("  Save:     valora api --provider serpapi --api-key YOUR_SERPAPI_KEY")
            elif spec.supports_chat:
                example_model = _provider_model(provider_slug) or "YOUR_MODEL"
                console.print(
                    f"  Save:     valora api --provider {provider_slug} --model {example_model}"
                    + (" --api-key YOUR_KEY" if spec.auth_kind != "none" else "")
                    + " --use-for-chat"
                )
            raise typer.Exit(code=0)

        if spec.slug == "huggingface" and "api_key" in profile:
            state["hf_api_token"] = profile["api_key"]
        if spec.slug == "serpapi" and "api_key" in profile:
            state["mcp_serpapi_api_key"] = profile["api_key"]
            state["mcp_enabled"] = bool(profile["api_key"])
            template = str(state.get("mcp_server_template", "https://mcp.serpapi.com/{api_key}/mcp"))
            state["mcp_server_url"] = template.format(api_key=profile["api_key"]) if profile["api_key"] else ""

        _save_provider_profile(provider_slug, profile)
        _sync_special_provider_state(provider_slug, profile)

        if use_for_chat:
            if not spec.supports_chat:
                console.print(f"[bold red]Error:[/bold red] {spec.label} is not a chat backend.")
                raise typer.Exit(code=2)
            _set_chat_default(provider_slug, profile.get("model", "") or model)

        if spec.supports_chat and use_for_chat:
            state["cloud_provider"] = provider_slug
            state["cloud_api_key"] = profile.get("api_key", "")
            state["cloud_model"] = profile.get("model", "")

        save_config()
        console.print(f"[green]{spec.label} configuration saved.[/green]")
        if use_for_chat:
            console.print(f"Default chat provider: {provider_slug}")

    @app.command("mcp")
    def mcp_command(
        api_key: str = typer.Option("", "--api-key", help="SerpApi API key for the MCP server."),
        disable: bool = typer.Option(False, "--disable", help="Disable MCP integration."),
    ) -> None:
        load_config()

        if disable:
            state["mcp_enabled"] = False
            state["mcp_serpapi_api_key"] = ""
            state["mcp_server_url"] = ""
            _save_provider_profile("serpapi", {})
            save_config()
            console.print("[green]MCP integration disabled.[/green]")
            raise typer.Exit(code=0)

        resolved_key = api_key.strip() or typer.prompt(
            "SerpApi API key",
            default=state.get("mcp_serpapi_api_key", ""),
            hide_input=True,
        ).strip()
        state["mcp_server_template"] = "https://mcp.serpapi.com/{api_key}/mcp"
        state["mcp_serpapi_api_key"] = resolved_key
        state["mcp_enabled"] = bool(resolved_key)
        state["mcp_server_url"] = state["mcp_server_template"].format(api_key=resolved_key) if resolved_key else ""
        _save_provider_profile("serpapi", {"api_key": resolved_key, "base_url": state["mcp_server_url"]})
        save_config()

        if not resolved_key:
            console.print("[yellow]No API key provided. MCP integration remains disabled.[/yellow]")
            raise typer.Exit(code=0)

        console.print("[green]MCP integration configured.[/green]")
        console.print(f"Server URL: {state['mcp_server_url']}")
