from __future__ import annotations

import platform
from dataclasses import dataclass

import httpx


LATEST_RELEASE_URL = "https://api.github.com/repos/ggerganov/llama.cpp/releases/latest"
VALID_ARCHIVE_SUFFIXES = (".zip", ".tar.gz", ".tgz")


@dataclass(slots=True)
class LlamaAsset:
    name: str
    url: str
    size_bytes: int
    os_name: str
    is_vulkan: bool
    is_cpu: bool

    @property
    def size_mb(self) -> int:
        return int(self.size_bytes / (1024 * 1024))


def detect_os_tag() -> str:
    system = platform.system().lower()
    if system == "windows":
        return "win"
    if system == "darwin":
        return "macos"
    return "linux"


def _is_binary_archive(name: str) -> bool:
    lowered = name.lower()
    return "-bin-" in lowered and lowered.endswith(VALID_ARCHIVE_SUFFIXES)


def _build_asset(raw: dict) -> LlamaAsset:
    name = str(raw.get("name", ""))
    lowered = name.lower()
    os_name = "Windows" if "win" in lowered else "macOS" if "macos" in lowered else "Linux"
    return LlamaAsset(
        name=name,
        url=str(raw.get("browser_download_url", "")),
        size_bytes=int(raw.get("size", 0)),
        os_name=os_name,
        is_vulkan=("vulkan" in lowered or "cuda" in lowered),
        is_cpu=not ("vulkan" in lowered or "cuda" in lowered),
    )


def derive_build_pattern(asset_name: str) -> str:
    lowered = asset_name.lower()
    for suffix in VALID_ARCHIVE_SUFFIXES:
        if lowered.endswith(suffix):
            lowered = lowered[: -len(suffix)]
            break

    parts = lowered.split("-")
    if len(parts) <= 3:
        return lowered

    filtered = [part for index, part in enumerate(parts) if index != 1]
    return "-".join(filtered)


def fetch_llama_cpp_releases(build_pattern: str = "") -> list[LlamaAsset]:
    os_tag = detect_os_tag()
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "valora/1.0.0",
    }
    with httpx.Client(timeout=20.0, follow_redirects=True, headers=headers) as client:
        response = client.get(LATEST_RELEASE_URL)
        response.raise_for_status()
        payload = response.json()

    assets = payload.get("assets", [])
    filtered: list[LlamaAsset] = []
    for raw_asset in assets:
        name = str(raw_asset.get("name", ""))
        lowered = name.lower()
        if os_tag not in lowered:
            continue
        if not _is_binary_archive(name):
            continue
        if build_pattern and build_pattern.lower() not in derive_build_pattern(lowered):
            continue
        filtered.append(_build_asset(raw_asset))

    filtered.sort(key=lambda item: (not item.is_vulkan, item.name.lower()))
    return filtered


def choose_default_asset(assets: list[LlamaAsset]) -> int:
    if not assets:
        return -1
    for index, asset in enumerate(assets):
        if asset.is_vulkan:
            return index
    return 0
