from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from urllib.parse import quote

import httpx

from valora.core.models import detect_quantization, is_vision_model

HF_API_BASE = "https://huggingface.co/api/models"
HF_WEB_BASE = "https://huggingface.co"


@dataclass(slots=True)
class HfGgufFile:
    repo_id: str
    path: str
    size_bytes: int
    quant: str
    model_type: str

    @property
    def name(self) -> str:
        return Path(self.path).name

    @property
    def size_mb(self) -> int:
        return int(self.size_bytes / (1024 * 1024))

    @property
    def download_url(self) -> str:
        encoded_path = quote(self.path, safe="/")
        return f"{HF_WEB_BASE}/{self.repo_id}/resolve/main/{encoded_path}?download=true"


@dataclass(slots=True)
class HfRepoManifest:
    repo_id: str
    tags: list[str]
    pipeline_tag: str
    card_data: dict
    gguf_files: list[HfGgufFile]

    @property
    def repo_type(self) -> str:
        lowered_tags = {tag.lower() for tag in self.tags}
        modalities = self.card_data.get("modalities") if isinstance(self.card_data, dict) else None
        if isinstance(modalities, list):
            lowered_tags.update(str(item).lower() for item in modalities)

        if self.pipeline_tag.lower() in {"image-text-to-text", "visual-question-answering"}:
            return "Vision"
        if any(tag in lowered_tags for tag in {"image-text-to-text", "vision", "multimodal", "vl", "llava"}):
            return "Vision"
        if any(tag in lowered_tags for tag in {"feature-extraction", "sentence-similarity", "embedding"}):
            return "Embedding"
        return "Chat"


def normalize_repo_id(value: str) -> str:
    cleaned = value.strip().rstrip("/")
    prefix = f"{HF_WEB_BASE}/"
    if cleaned.startswith(prefix):
        cleaned = cleaned[len(prefix) :]
    cleaned = cleaned.replace("/tree/main", "").replace("/blob/main", "")
    return cleaned.strip("/")


def _build_headers(token: str) -> dict[str, str]:
    cleaned = token.strip()
    if not cleaned:
        return {}
    return {"Authorization": f"Bearer {cleaned}"}


def _extract_size(item: dict) -> int:
    size = item.get("size")
    if isinstance(size, int) and size > 0:
        return size
    lfs = item.get("lfs")
    if isinstance(lfs, dict):
        lfs_size = lfs.get("size")
        if isinstance(lfs_size, int) and lfs_size > 0:
            return lfs_size
    return 0


def fetch_repo_manifest(repo_id: str, token: str = "") -> HfRepoManifest:
    url = f"{HF_API_BASE}/{repo_id}"
    response = httpx.get(
        url,
        headers=_build_headers(token),
        timeout=30.0,
        follow_redirects=True,
        params={"blobs": "true"},
    )
    response.raise_for_status()
    payload = response.json()
    siblings = payload.get("siblings", [])

    files: list[HfGgufFile] = []
    for item in siblings:
        path = str(item.get("rfilename", "")).strip()
        if not path.lower().endswith(".gguf"):
            continue
        size_bytes = _extract_size(item)
        files.append(
            HfGgufFile(
                repo_id=repo_id,
                path=path,
                size_bytes=size_bytes,
                quant=detect_quantization(path),
                model_type="Vision" if is_vision_model(path) else "Chat",
            )
        )

    files.sort(key=lambda item: (item.size_bytes, item.name.lower()))
    return HfRepoManifest(
        repo_id=repo_id,
        tags=[str(tag) for tag in payload.get("tags", [])],
        pipeline_tag=str(payload.get("pipeline_tag", "") or ""),
        card_data=payload.get("cardData", {}) if isinstance(payload.get("cardData"), dict) else {},
        gguf_files=files,
    )


def fetch_repo_gguf_files(repo_id: str, token: str = "") -> list[HfGgufFile]:
    return fetch_repo_manifest(repo_id, token=token).gguf_files


def build_auth_headers(token: str) -> dict[str, str]:
    return _build_headers(token)
