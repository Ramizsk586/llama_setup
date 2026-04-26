from typer import Typer

from valora.cli.setup import register_setup_commands
from valora.cli.update import register_update_commands
from valora.cli.other import register_other_commands

app = Typer(
    add_completion=False,
    help="Local AI Model Manager - embedded llama.cpp with Vulkan",
    rich_markup_mode="rich",
)

register_setup_commands(app)
register_update_commands(app)
register_other_commands(app)
