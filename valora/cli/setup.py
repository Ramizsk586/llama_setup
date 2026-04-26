from __future__ import annotations

import platform
from pathlib import Path

import typer
from rich.console import Console

from valora.core.config import load_config, save_config, state
from valora.core.llama_releases import derive_build_pattern, fetch_llama_cpp_releases
from valora.core.models import scan_models
from valora.tui.build_selector import select_build
from valora.utils.download import download_with_progress
from valora.utils.extract import extract_archive

console = Console()


def _binary_name(base: str) -> str:
    return f"{base}.exe" if platform.system() == "Windows" else base


def _find_binary(root: Path, binary_name: str) -> Path | None:
    for candidate in root.rglob(binary_name):
        if candidate.is_file():
            return candidate
    return None


def _setup_llama_cpp() -> None:
    console.print("[bold]llama.cpp Setup[/bold]")
    console.print(f"[dim]Detected platform: {platform.system()}[/dim]")

    assets = fetch_llama_cpp_releases()
    if not assets:
        console.print("[bold red]Error:[/bold red] No compatible llama.cpp release assets were found.")
        raise typer.Exit(code=2)

    selected = select_build(assets)
    if selected is None:
        console.print("[yellow]Setup cancelled.[/yellow]")
        raise typer.Exit(code=1)

    install_dir = Path.cwd() / "llama-cpp"
    archive_path = install_dir.parent / selected.name
    download_with_progress(selected.url, archive_path)
    extract_archive(archive_path, install_dir)

    server_bin = _find_binary(install_dir, _binary_name("llama-server"))
    cli_bin = _find_binary(install_dir, _binary_name("llama-cli"))
    if server_bin is None or cli_bin is None:
        console.print("[bold red]Error:[/bold red] Extracted release is missing llama-server or llama-cli.")
        raise typer.Exit(code=2)

    state["llama_cpp_path"] = str(install_dir)
    state["server"] = str(server_bin)
    state["llama_cli"] = str(cli_bin)
    state["llama_cpp_build"] = derive_build_pattern(selected.name)
    save_config()

    archive_path.unlink(missing_ok=True)
    console.print("[green]Setup completed successfully![/green]")


def _setup_models() -> None:
    default_path = Path.cwd() / "models"
    folder_input = typer.prompt("Models directory", default=str(default_path))
    folder = Path(folder_input).expanduser()
    folder.mkdir(parents=True, exist_ok=True)
    models = scan_models(str(folder))
    state["folder"] = str(folder)
    save_config()
    console.print(f"[green]Models directory configured.[/green] Found {len(models)} GGUF file(s).")


def _setup_mcp() -> None:
    console.print("[bold]MCP Server Setup[/bold]")
    console.print("[dim]Configure SerpApi MCP for future chat/TUI tool calling.[/dim]")

    template = "https://mcp.serpapi.com/{api_key}/mcp"
    api_key = typer.prompt("SerpApi API key", default=state.get("mcp_serpapi_api_key", ""), hide_input=True)

    state["mcp_enabled"] = bool(str(api_key).strip())
    state["mcp_serpapi_api_key"] = str(api_key).strip()
    state["mcp_server_template"] = template
    state["mcp_server_url"] = template.format(api_key=state["mcp_serpapi_api_key"]) if state["mcp_enabled"] else ""
    save_config()

    if state["mcp_enabled"]:
        console.print("[green]SerpApi MCP configured.[/green]")
    else:
        console.print("[yellow]SerpApi MCP disabled because no API key was provided.[/yellow]")


def register_setup_commands(app: typer.Typer) -> None:
    @app.command("setup")
    def setup_command(
        llama_cpp: bool = typer.Option(False, "--llama.cpp", help="Set up embedded llama.cpp binaries."),
        models: bool = typer.Option(False, "--models", help="Configure the models directory."),
        mcp: bool = typer.Option(False, "--mcp", help="Configure the SerpApi MCP server."),
        all_items: bool = typer.Option(False, "--all", help="Run all setup flows."),
    ) -> None:
        load_config()

        run_llama_cpp = llama_cpp or all_items
        run_models = models or all_items
        run_mcp = mcp or all_items
        if not any((run_llama_cpp, run_models, run_mcp)):
            run_llama_cpp = True
            run_models = True

        if run_llama_cpp:
            _setup_llama_cpp()
        if run_models:
            _setup_models()
        if run_mcp:
            _setup_mcp()
