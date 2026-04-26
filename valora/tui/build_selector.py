from __future__ import annotations

import typer
from rich.console import Console
from rich.table import Table

from valora.core.llama_releases import LlamaAsset, choose_default_asset

console = Console()


def select_build(assets: list[LlamaAsset]) -> LlamaAsset | None:
    if not assets:
        return None

    table = Table(title="llama.cpp Builds")
    table.add_column("#", justify="right")
    table.add_column("Name")
    table.add_column("Size", justify="right")
    table.add_column("Type", justify="center")
    table.add_column("OS", justify="center")

    default_index = choose_default_asset(assets)
    for index, asset in enumerate(assets, start=1):
        asset_type = "[green][GPU - Vulkan][/green]" if asset.is_vulkan else "[dim][CPU][/dim]"
        style = "cyan" if index - 1 == default_index else ""
        table.add_row(str(index), asset.name, f"{asset.size_mb} MB", asset_type, asset.os_name, style=style)

    console.print(table)
    prompt_default = str(default_index + 1)
    choice = typer.prompt("Select build number", default=prompt_default)
    try:
        selected = int(choice) - 1
    except ValueError:
        return None
    if selected < 0 or selected >= len(assets):
        return None
    return assets[selected]
