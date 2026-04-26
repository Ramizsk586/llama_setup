from __future__ import annotations

import psutil
import typer
from rich.console import Console
from rich.table import Table

from valora import __version__
from valora.core.config import get_config_path, load_config, save_config, state
from valora.core.gpu import detect_vulkan_support, get_gpu_name, get_total_vram_mb
from valora.core.models import (
    format_size_mb,
    is_model_usable,
    recommend_models_for_device,
    scan_models,
)

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

    console.print(
        "\n[dim]This fallback catalog favors recent/popular families such as Qwen, Gemma, LiquidAI, "
        "Llama, Phi, and Mistral.[/dim]"
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
        console.print(f"  Config file:    {get_config_path()}\n")

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
        console.print(
            f"\nTotal: {len(models)} models | Available VRAM: {vram_mb:,} MB | Default context: {ctx_len}"
        )

    @app.command("status")
    def status_command() -> None:
        load_config()
        running = state["model_loaded"]
        if running:
            console.print(
                f"[green]Running[/green] {state['selected_model'] or 'unknown model'} on port {state['port']}"
            )
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
        state["mcp_server_url"] = (
            state["mcp_server_template"].format(api_key=resolved_key) if resolved_key else ""
        )
        save_config()

        if not resolved_key:
            console.print("[yellow]No API key provided. MCP integration remains disabled.[/yellow]")
            raise typer.Exit(code=0)

        console.print("[green]MCP integration configured.[/green]")
        console.print(f"Server URL: {state['mcp_server_url']}")
