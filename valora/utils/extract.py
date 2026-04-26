from __future__ import annotations

import os
import stat
import tarfile
import zipfile
from pathlib import Path

from rich.console import Console
from rich.progress import BarColumn, Progress, TextColumn

console = Console()


def _strip_root(parts: tuple[str, ...], root_prefix: str | None) -> Path:
    cleaned = [part for part in parts if part not in {"", "."}]
    if root_prefix and cleaned and cleaned[0] == root_prefix:
        cleaned = cleaned[1:]
    return Path(*cleaned) if cleaned else Path()


def _detect_single_root(names: list[str]) -> str | None:
    roots = {Path(name).parts[0] for name in names if Path(name).parts}
    if len(roots) == 1:
        return next(iter(roots))
    return None


def _ensure_executable(path: Path) -> None:
    if os.name == "nt":
        return
    try:
        current = path.stat().st_mode
        path.chmod(current | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    except OSError:
        return


def extract_archive(archive_path: Path, dest_dir: Path) -> None:
    dest_dir.mkdir(parents=True, exist_ok=True)

    if archive_path.suffix.lower() == ".zip":
        with zipfile.ZipFile(archive_path) as archive:
            names = archive.namelist()
            root_prefix = _detect_single_root(names)
            with Progress(TextColumn("{task.description}"), BarColumn(), console=console) as progress:
                task = progress.add_task("Extracting archive", total=len(names))
                for member in archive.infolist():
                    target = _strip_root(Path(member.filename).parts, root_prefix)
                    if not target:
                        progress.advance(task)
                        continue
                    final_path = dest_dir / target
                    if member.is_dir():
                        final_path.mkdir(parents=True, exist_ok=True)
                    else:
                        final_path.parent.mkdir(parents=True, exist_ok=True)
                        with archive.open(member) as source, final_path.open("wb") as output:
                            output.write(source.read())
                        _ensure_executable(final_path)
                    progress.advance(task)
        return

    if archive_path.name.lower().endswith((".tar.gz", ".tgz")):
        with tarfile.open(archive_path, "r:gz") as archive:
            members = archive.getmembers()
            names = [member.name for member in members]
            root_prefix = _detect_single_root(names)
            with Progress(TextColumn("{task.description}"), BarColumn(), console=console) as progress:
                task = progress.add_task("Extracting archive", total=len(members))
                for member in members:
                    target = _strip_root(Path(member.name).parts, root_prefix)
                    if not target:
                        progress.advance(task)
                        continue
                    final_path = dest_dir / target
                    if member.isdir():
                        final_path.mkdir(parents=True, exist_ok=True)
                    else:
                        final_path.parent.mkdir(parents=True, exist_ok=True)
                        extracted = archive.extractfile(member)
                        if extracted is not None:
                            with final_path.open("wb") as output:
                                output.write(extracted.read())
                            _ensure_executable(final_path)
                    progress.advance(task)
        return

    raise ValueError(f"Unsupported archive format: {archive_path.name}")
