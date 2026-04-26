from __future__ import annotations

from pathlib import Path

import httpx
import psutil
import typer
from rich.columns import Columns
from rich.console import Console
from rich.panel import Panel
from rich.table import Table
from rich.text import Text

from valora import __version__
from valora.core.config import get_config_path, load_config, save_config, state
from valora.core.gpu import detect_vulkan_support, get_gpu_name, get_total_vram_mb
from valora.core.models import estimate_context_overhead_mb, format_size_mb, is_model_usable, recommend_models_for_device, scan_models
from valora.core.providers import PROVIDERS, ProviderSpec, get_provider
from valora.utils.download import download_with_progress
from valora.utils.huggingface import HfGgufFile, HfRepoManifest, build_auth_headers, fetch_repo_manifest, normalize_repo_id

console = Console()


def _mask_secret(value: str) -> str:
    cleaned = value.strip()
    if not cleaned:
        return "Not configured"
    if len(cleaned) <= 6:
        return "*" * len(cleaned)
    return f"{cleaned[:3]}...{cleaned[-3:]}"


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
    spec, model, base_url, api_key = _resolve_chat_runtime(provider_slug, model_override, base_url_override)
    if spec.protocol == "gemini":
        return _chat_gemini(base_url, api_key, model, prompt, system_prompt)
    return _chat_openai_compatible(base_url, api_key, model, prompt, system_prompt)


def _chat_transcript_panel(transcript: list[tuple[str, str]]) -> Panel:
    body = Text()
    if not transcript:
        body.append("Welcome back!\n\n", style="bold white")
        body.append("Start typing to chat with your selected model.\n", style="white")
        body.append("Use /help to see slash commands.\n", style="dim")
    else:
        for role, content in transcript[-10:]:
            style = "bold cyan" if role == "You" else "bold green"
            body.append(f"{role}\n", style=style)
            body.append(f"{content.strip()}\n\n", style="white")
    return Panel(body, title="Valora Chat", border_style="red")


def _chat_home_panel(provider_slug: str, model_name: str, system_prompt: str, status: str) -> Columns:
    provider_label = provider_slug or "Not configured"
    left = Text()
    left.append("VALORA\n", style="bold red")
    left.append("Local + cloud model chat\n\n", style="white")
    left.append(f"Provider: {provider_label}\n", style="white")
    left.append(f"Model: {model_name or 'Not configured'}\n", style="white")
    left.append(f"Context: {_get_ctx_len()}\n", style="white")
    left.append(f"Folder: {_preferred_models_dir()}\n", style="dim")
    if system_prompt.strip():
        left.append("\nSystem prompt active\n", style="yellow")

    right = Text()
    right.append("Slash Commands\n", style="bold red")
    right.append("/help", style="bold white")
    right.append("  show commands\n", style="white")
    right.append("/provider NAME", style="bold white")
    right.append("  switch backend\n", style="white")
    right.append("/model NAME", style="bold white")
    right.append("  switch model\n", style="white")
    right.append("/system TEXT", style="bold white")
    right.append("  set system prompt\n", style="white")
    right.append("/clear", style="bold white")
    right.append("  clear transcript\n", style="white")
    right.append("/api", style="bold white")
    right.append("  show API table\n", style="white")
    right.append("/save", style="bold white")
    right.append("  save current chat default\n", style="white")
    right.append("/exit", style="bold white")
    right.append("  leave chat\n", style="white")
    if status.strip():
        right.append("\nStatus\n", style="bold red")
        right.append(status, style="white")

    return Columns(
        [
            Panel(left, border_style="red", title="Session"),
            Panel(right, border_style="red", title="Controls"),
        ],
        expand=True,
    )


def _render_chat_ui(
    provider_slug: str,
    model_name: str,
    system_prompt: str,
    transcript: list[tuple[str, str]],
    status: str,
) -> None:
    console.clear()
    console.print(f"[bold red]Valora Chat[/bold red]  [dim]Provider:[/dim] {provider_slug}  [dim]Model:[/dim] {model_name or '-'}")
    console.print(_chat_home_panel(provider_slug, model_name, system_prompt, status))
    console.print(_chat_transcript_panel(transcript))
    console.print("[dim]Type a message or use /help. /exit closes chat.[/dim]")


def _chat_help_text() -> str:
    return (
        "/help, /provider NAME, /model NAME, /system TEXT, /clear, /api, /save, /exit"
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
    ) -> None:
        load_config()
        provider_slug = provider.strip() or str(state.get("chat_provider", "")).strip()
        if not provider_slug:
            console.print("[bold red]Error:[/bold red] No default chat provider is configured.")
            console.print("Set one with: [bold]valora api --provider ... --model ... --use-for-chat[/bold]")
            raise typer.Exit(code=2)

        if prompt.strip():
            user_message = prompt
            try:
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
        session_base_url = base_url.strip()
        session_system = system.strip()
        transcript: list[tuple[str, str]] = []
        status_message = "Ready."

        while True:
            _render_chat_ui(session_provider, session_model, session_system, transcript, status_message)
            user_message = typer.prompt(">").strip()
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
                if command == "clear":
                    transcript.clear()
                    status_message = "Transcript cleared."
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
                    session_base_url = ""
                    status_message = f"Using provider {session_provider}."
                    continue
                if command == "model":
                    if not arg:
                        status_message = f"Current model: {session_model or 'Not configured'}"
                        continue
                    session_model = arg
                    status_message = f"Using model {session_model}."
                    continue
                if command == "system":
                    session_system = "" if arg.lower() in {"off", "clear", "none"} else arg
                    status_message = "System prompt cleared." if not session_system else "System prompt updated."
                    continue
                if command == "save":
                    state["chat_provider"] = session_provider
                    state["chat_model"] = session_model
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
