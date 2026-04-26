from __future__ import annotations

from pathlib import Path

import httpx
from rich.console import Console
from rich.progress import BarColumn, DownloadColumn, Progress, TaskID, TextColumn, TimeRemainingColumn, TransferSpeedColumn

console = Console()


def download_with_progress(url: str, dest_path: Path) -> None:
    dest_path.parent.mkdir(parents=True, exist_ok=True)

    progress = Progress(
        TextColumn("[bold blue]{task.fields[filename]}", justify="left"),
        BarColumn(),
        DownloadColumn(),
        TransferSpeedColumn(),
        TimeRemainingColumn(),
        console=console,
    )

    try:
        with httpx.stream("GET", url, follow_redirects=True, timeout=60.0) as response:
            response.raise_for_status()
            total = int(response.headers.get("Content-Length", 0))
            with progress:
                task_id: TaskID = progress.add_task(
                    "download",
                    filename=dest_path.name,
                    total=total if total > 0 else None,
                )
                with dest_path.open("wb") as handle:
                    for chunk in response.iter_bytes():
                        if not chunk:
                            continue
                        handle.write(chunk)
                        progress.update(task_id, advance=len(chunk))
    except Exception:
        if dest_path.exists():
            dest_path.unlink(missing_ok=True)
        raise
