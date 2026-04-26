from __future__ import annotations

import json
import platform
import subprocess
from pathlib import Path


def _run_command(command: list[str]) -> str:
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            check=False,
            timeout=5,
        )
    except (OSError, subprocess.SubprocessError):
        return ""
    if result.returncode != 0:
        return ""
    return result.stdout.strip()


def _detect_nvml() -> tuple[int, str]:
    try:
        import pynvml  # type: ignore
    except ImportError:
        return 0, "Unknown"

    try:
        pynvml.nvmlInit()
        handle = pynvml.nvmlDeviceGetHandleByIndex(0)
        info = pynvml.nvmlDeviceGetMemoryInfo(handle)
        name = pynvml.nvmlDeviceGetName(handle)
        if isinstance(name, bytes):
            name = name.decode("utf-8", errors="ignore")
        return int(info.total / (1024 * 1024)), str(name)
    except Exception:
        return 0, "Unknown"
    finally:
        try:
            pynvml.nvmlShutdown()
        except Exception:
            pass


def _detect_vulkan_binding() -> tuple[int, str]:
    try:
        import vulkan as vk  # type: ignore
    except ImportError:
        return 0, "Unknown"

    try:
        app_info = vk.VkApplicationInfo(
            sType=vk.VK_STRUCTURE_TYPE_APPLICATION_INFO,
            pApplicationName="Valora",
            applicationVersion=1,
            pEngineName="Valora",
            engineVersion=1,
            apiVersion=vk.VK_API_VERSION_1_0,
        )
        instance_info = vk.VkInstanceCreateInfo(
            sType=vk.VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            pApplicationInfo=app_info,
        )
        instance = vk.vkCreateInstance(instance_info, None)
        devices = vk.vkEnumeratePhysicalDevices(instance)
        if not devices:
            vk.vkDestroyInstance(instance, None)
            return 0, "Unknown"

        device = devices[0]
        props = vk.vkGetPhysicalDeviceProperties(device)
        name = props.deviceName
        heaps = vk.vkGetPhysicalDeviceMemoryProperties(device).memoryHeaps
        vram_bytes = 0
        for heap in heaps:
            flags = getattr(heap, "flags", 0)
            if flags & getattr(vk, "VK_MEMORY_HEAP_DEVICE_LOCAL_BIT", 0):
                vram_bytes = max(vram_bytes, int(heap.size))
        vk.vkDestroyInstance(instance, None)
        return int(vram_bytes / (1024 * 1024)), str(name)
    except Exception:
        return 0, "Unknown"


def _detect_vulkaninfo() -> tuple[int, str]:
    text = _run_command(["vulkaninfo", "--summary"])
    if not text:
        text = _run_command(["vkvia"])
    if not text:
        return 0, "Unknown"

    gpu_name = "Unknown"
    for line in text.splitlines():
        cleaned = line.strip()
        if "GPU id" in cleaned or "deviceName" in cleaned:
            gpu_name = cleaned.split("=", 1)[-1].strip() if "=" in cleaned else cleaned
            break
    return 0, gpu_name


def _detect_nvidia_smi() -> tuple[int, str]:
    name = _run_command(["nvidia-smi", "--query-gpu=name", "--format=csv,noheader"])
    memory = _run_command(["nvidia-smi", "--query-gpu=memory.total", "--format=csv,noheader,nounits"])
    if not name and not memory:
        return 0, "Unknown"
    first_name = name.splitlines()[0].strip() if name else "Unknown"
    try:
        first_memory = int(memory.splitlines()[0].strip()) if memory else 0
    except ValueError:
        first_memory = 0
    return first_memory, first_name


def _detect_windows_wmi() -> tuple[int, str]:
    if platform.system() != "Windows":
        return 0, "Unknown"

    command = [
        "powershell",
        "-NoProfile",
        "-Command",
        "Get-CimInstance Win32_VideoController | Select-Object -First 1 Name,AdapterRAM | ConvertTo-Json -Compress",
    ]
    raw = _run_command(command)
    if not raw:
        return 0, "Unknown"

    try:
        data = json.loads(raw)
    except json.JSONDecodeError:
        return 0, "Unknown"

    try:
        ram_mb = int(int(data.get("AdapterRAM", 0)) / (1024 * 1024))
    except (TypeError, ValueError):
        ram_mb = 0
    return ram_mb, str(data.get("Name", "Unknown"))


def get_total_vram_mb() -> int:
    for detector in (_detect_nvml, _detect_vulkan_binding, _detect_nvidia_smi, _detect_windows_wmi):
        vram_mb, _ = detector()
        if vram_mb > 0:
            return vram_mb
    return 0


def get_gpu_name() -> str:
    for detector in (
        _detect_nvml,
        _detect_vulkan_binding,
        _detect_nvidia_smi,
        _detect_windows_wmi,
        _detect_vulkaninfo,
    ):
        _, name = detector()
        if name and name != "Unknown":
            return name
    return "Unknown"


def detect_vulkan_support() -> bool:
    if _detect_vulkan_binding()[0] > 0:
        return True
    if _run_command(["vulkaninfo", "--summary"]):
        return True
    if _run_command(["vkvia"]):
        return True
    if platform.system() == "Windows":
        reg_check = _run_command(
            [
                "reg",
                "query",
                r"HKLM\SOFTWARE\Khronos\Vulkan\Drivers",
            ]
        )
        return bool(reg_check)
    return False
