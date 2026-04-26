from __future__ import annotations

import os
import re
from dataclasses import dataclass
from pathlib import Path


MAX_MODELS = 512
QUANT_PATTERN = re.compile(
    r"(Q\d+_K_[MSL]|Q\d+_[0-9A-Z]+|IQ\d+_[A-Z]+|F16|BF16|FP16|FP32|Q\d+_K|Q\d+)",
    re.IGNORECASE,
)


@dataclass(slots=True)
class ModelInfo:
    name: str
    path: Path
    size_mb: int
    quant: str
    model_type: str
    projector: str = ""


@dataclass(slots=True)
class RecommendedModel:
    family: str
    name: str
    category: str
    params: str
    approx_q4_mb: int
    tags: str
    hf_repo: str = ""
    hf_pattern: str = "*Q4_K_M*.gguf"


def detect_quantization(path: str) -> str:
    name = Path(path).name
    match = QUANT_PATTERN.search(name)
    if not match:
        return "Unknown"
    return match.group(1).upper()


def get_model_size_mb(path: str) -> int:
    try:
        size_bytes = Path(path).stat().st_size
    except OSError:
        return 0
    return int(size_bytes / (1024 * 1024))


def is_vision_model(name: str) -> bool:
    lowered = name.lower()
    if any(token in lowered for token in ("mmproj", "vision", "llava", "minicpm")):
        return True
    return bool(re.search(r"(^|[-_.])vl([-. _]|$)", lowered))


def is_projector_file(name: str) -> bool:
    return "mmproj" in name.lower()


def _detect_model_type(name: str) -> str:
    lowered = name.lower()
    if is_projector_file(lowered):
        return "Projector"
    if is_vision_model(lowered):
        return "Vision"
    if any(token in lowered for token in ("embed", "bge-", "nomic-embed")):
        return "Embedding"
    return "Chat"


def find_projector(model_path: str, all_models: list[str]) -> str:
    base = Path(model_path).stem.lower()
    parent = Path(model_path).parent
    for candidate in all_models:
        candidate_name = Path(candidate).name.lower()
        if "mmproj" not in candidate_name:
            continue
        if base.split(".")[0] in candidate_name or any(
            token in candidate_name for token in ("llava", "vision", "minicpm")
        ):
            return str(parent / candidate)
    return ""


def scan_models(folder: str) -> list[ModelInfo]:
    root = Path(folder)
    if not root.exists():
        return []

    found: list[Path] = []
    all_gguf_names: list[str] = []
    for path in root.rglob("*.gguf"):
        all_gguf_names.append(path.name)
        if is_projector_file(path.name):
            continue
        found.append(path)
        if len(found) >= MAX_MODELS:
            break

    models: list[ModelInfo] = []
    for path in sorted(found, key=lambda item: item.name.lower()):
        model = ModelInfo(
            name=path.name,
            path=path,
            size_mb=get_model_size_mb(str(path)),
            quant=detect_quantization(path.name),
            model_type=_detect_model_type(path.name),
            projector="",
        )
        if model.model_type == "Vision":
            model.projector = find_projector(str(path), all_gguf_names)
        models.append(model)
    return models


def is_model_usable(model: ModelInfo, vram_mb: int) -> bool:
    if vram_mb <= 0:
        return True
    return model.size_mb <= int(vram_mb * 0.85)


def pick_best_model(models: list[ModelInfo], vram_mb: int) -> int:
    if not models:
        return -1

    usable_indices = [index for index, model in enumerate(models) if is_model_usable(model, vram_mb)]
    if usable_indices:
        usable_indices.sort(key=lambda index: (models[index].size_mb, models[index].quant), reverse=True)
        return usable_indices[0]

    fallback_index = min(range(len(models)), key=lambda index: models[index].size_mb)
    return fallback_index


def format_size_mb(size_mb: int) -> str:
    if size_mb >= 1024:
        return f"{size_mb / 1024:.1f} GB"
    return f"{size_mb} MB"


def resolve_auto_runtime_settings(model: ModelInfo, gpu_layers: str, threads: str) -> tuple[str, str]:
    resolved_gpu_layers = gpu_layers
    resolved_threads = threads

    if threads == "auto":
        cpu_count = os.cpu_count() or 1
        resolved_threads = str(min(cpu_count, 8))

    if gpu_layers == "auto":
        try:
            from valora.core.gpu import get_total_vram_mb
        except ImportError:
            resolved_gpu_layers = "0"
        else:
            vram_mb = get_total_vram_mb()
            if vram_mb >= int(model.size_mb * 1.1):
                resolved_gpu_layers = "999"
            elif vram_mb >= int(model.size_mb * 0.5) and model.size_mb > 0:
                estimated_layers = max(1, int((vram_mb / model.size_mb) * 100))
                resolved_gpu_layers = str(estimated_layers)
            else:
                resolved_gpu_layers = "0"

    return resolved_gpu_layers, resolved_threads


def _default_catalog() -> list[RecommendedModel]:
    return [
        RecommendedModel("Qwen", "Qwen3.5-0.8B-Instruct", "Chat", "0.8B", 900, "tiny, multilingual"),
        RecommendedModel("Qwen", "Qwen3.5-2B-Instruct", "Chat", "2B", 1800, "balanced, multilingual"),
        RecommendedModel(
            "Qwen",
            "Qwen3.5-4B-Instruct",
            "Chat",
            "4B",
            3200,
            "strong small generalist",
            hf_repo="openresearchtools/Qwen3.5-4B-Instruct-GGUF",
        ),
        RecommendedModel("Qwen", "Qwen3.6-35B-A3B", "Reasoning", "35B-A3B", 21000, "latest Qwen family, agentic"),
        RecommendedModel("Gemma", "Gemma 3 270M", "Chat", "270M", 500, "ultra-light"),
        RecommendedModel(
            "Gemma",
            "Gemma 3 1B",
            "Chat",
            "1B",
            1100,
            "fast text-only",
            hf_repo="gguf-org/gemma-3-1b-it-gguf",
        ),
        RecommendedModel("Gemma", "Gemma 3 4B", "Multimodal", "4B", 3400, "vision + chat"),
        RecommendedModel("Gemma", "Gemma 3n E2B", "On-device", "E2B", 2200, "mobile-friendly"),
        RecommendedModel(
            "LiquidAI",
            "LFM2.5-350M",
            "Chat",
            "350M",
            450,
            "very fast edge model",
            hf_repo="LiquidAI/LFM2.5-350M-GGUF",
        ),
        RecommendedModel(
            "LiquidAI",
            "LFM2.5-1.2B-Instruct",
            "Chat",
            "1.2B",
            950,
            "efficient, recent",
            hf_repo="LiquidAI/LFM2.5-1.2B-Instruct-GGUF",
        ),
        RecommendedModel("LiquidAI", "LFM2-VL-3B", "Multimodal", "3B", 2600, "vision + text"),
        RecommendedModel(
            "Llama",
            "Llama 3.2 1B",
            "Chat",
            "1B",
            850,
            "popular local starter",
            hf_repo="unsloth/Llama-3.2-1B-Instruct-GGUF",
        ),
        RecommendedModel(
            "Llama",
            "Llama 3.2 3B",
            "Chat",
            "3B",
            1500,
            "popular everyday chat",
            hf_repo="merterbak/Llama-3.2-3B-Instruct-GGUF",
        ),
        RecommendedModel(
            "Phi",
            "Phi-4-mini-instruct",
            "Reasoning",
            "3.8B",
            3200,
            "small reasoning/coding",
            hf_repo="llmware/phi-4-mini-gguf",
        ),
        RecommendedModel(
            "Mistral",
            "Ministral 3 3B",
            "Chat",
            "3B",
            2400,
            "recent Mistral small model",
            hf_repo="mistralai/Ministral-3-3B-Instruct-2512-GGUF",
        ),
        RecommendedModel("Mistral", "Ministral 3 8B", "Chat", "8B", 5200, "stronger quality tier"),
    ]


def estimate_context_overhead_mb(ctx_len: int) -> int:
    normalized_ctx = max(ctx_len, 1024)
    return max(256, int((normalized_ctx / 4096) * 768))


def _fits_memory(requirement_mb: int, available_mb: int, reserve_ratio: float) -> bool:
    if available_mb <= 0:
        return False
    return requirement_mb <= int(available_mb * reserve_ratio)


def recommend_models_for_device(
    available_ram_mb: int,
    vram_mb: int,
    ctx_len: int,
) -> list[tuple[RecommendedModel, str]]:
    requirement_overhead_mb = estimate_context_overhead_mb(ctx_len)
    recommended: list[tuple[RecommendedModel, str]] = []

    for model in _default_catalog():
        requirement_mb = model.approx_q4_mb + requirement_overhead_mb
        fits_gpu = _fits_memory(requirement_mb, vram_mb, 0.85)
        fits_ram = _fits_memory(requirement_mb, available_ram_mb, 0.9)
        if not fits_gpu and not fits_ram:
            continue

        preferred_runtime = "GPU" if fits_gpu else "CPU/RAM"
        recommended.append((model, preferred_runtime))

    recommended.sort(key=lambda item: (item[0].family, item[0].approx_q4_mb, item[0].name))
    return recommended
