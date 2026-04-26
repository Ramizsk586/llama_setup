from __future__ import annotations

import shutil
from pathlib import Path

import typer
from rich.console import Console

from valora.core.config import load_config, save_config, state
from valora.core.llama_releases import fetch_llama_cpp_releases
from valora.cli.setup import _binary_name, _find_binary
from valora.tui.build_selector import select_build
from valora.utils.download import download_with_progress
from valora.utils.extract import extract_archive

console = Console()


def _clear_directory(target: Path) -> None:
    if not target.exists():
        return
    for child in target.iterdir():
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()


def register_update_commands(app: typer.Typer) -> None:
    @app.command("update")
    def update_command(
        llama_cpp: bool = typer.Option(False, "--llama.cpp", help="Update embedded llama.cpp binaries."),
    ) -> None:
        load_config()
        if not llama_cpp:
            console.print("[yellow]Nothing to update. Use --llama.cpp.[/yellow]")
            raise typer.Exit(code=1)

        install_dir_value = state.get("llama_cpp_path", "")
        if not install_dir_value:
            console.print("[bold red]Error:[/bold red] Valora is not configured. Run 'valora setup --llama.cpp' first.")
            raise typer.Exit(code=1)

        build_pattern = str(state.get("llama_cpp_build", ""))
        assets = fetch_llama_cpp_releases(build_pattern=build_pattern.lower())
        if not assets:
            console.print("[bold red]Error:[/bold red] No compatible update assets were found for the saved build pattern.")
            raise typer.Exit(code=2)

        selected = select_build(assets)
        if selected is None:
            console.print("[yellow]Update cancelled.[/yellow]")
            raise typer.Exit(code=1)

        confirmed = typer.confirm("Delete the existing llama-cpp install directory contents?", default=False)
        if not confirmed:
            console.print("[yellow]Update cancelled.[/yellow]")
            raise typer.Exit(code=1)

        install_dir = Path(install_dir_value)
        archive_path = install_dir.parent / selected.name
        _clear_directory(install_dir)
        download_with_progress(selected.url, archive_path)
        extract_archive(archive_path, install_dir)

        server_bin = _find_binary(install_dir, _binary_name("llama-server"))
        cli_bin = _find_binary(install_dir, _binary_name("llama-cli"))
        if server_bin is None or cli_bin is None:
            console.print("[bold red]Error:[/bold red] Extracted release is missing llama-server or llama-cli.")
            raise typer.Exit(code=2)

        state["server"] = str(server_bin)
        state["llama_cli"] = str(cli_bin)
        state["llama_cpp_build"] = selected.name
        save_config()
        archive_path.unlink(missing_ok=True)
        console.print("[green]Update completed successfully![/green]")
