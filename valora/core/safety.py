from __future__ import annotations

from enum import Enum

import psutil

from valora.core.config import state
from valora.core.gpu import get_total_vram_mb
from valora.core.models import ModelInfo


class SafetyResult(Enum):
    ALLOW = "allow"
    ALLOW_WITH_WARNINGS = "warn"
    REFUSE = "refuse"
    KILL = "kill"


def check_load_safety(
    model: ModelInfo,
    projector: ModelInfo | None,
    ctx_len: int,
    gpu_layers: int,
) -> tuple[SafetyResult, str]:
    memory = psutil.virtual_memory()
    available_ram_mb = int(memory.available / (1024 * 1024))
    vram_mb = get_total_vram_mb()
    projector_size_mb = projector.size_mb if projector else 0
    total_model_mb = model.size_mb + projector_size_mb

    if total_model_mb > int(available_ram_mb * 0.9):
        return (
            SafetyResult.REFUSE,
            f"Model ({total_model_mb:,} MB) exceeds available RAM ({available_ram_mb:,} MB).",
        )

    if ctx_len > 8192 and memory.total < (8 * 1024 * 1024 * 1024):
        return (
            SafetyResult.ALLOW_WITH_WARNINGS,
            "Large context requested on a system with less than 8 GB RAM.",
        )

    if gpu_layers > 0 and vram_mb > 0 and model.size_mb > int(vram_mb * 0.95):
        if state.get("model_loaded"):
            return (
                SafetyResult.KILL,
                "A model is already marked as running and the new GPU load could exhaust VRAM.",
            )
        return (
            SafetyResult.ALLOW_WITH_WARNINGS,
            "Model may not fit in VRAM - proceeding with caution.",
        )

    return SafetyResult.ALLOW, "Model passed safety checks."
