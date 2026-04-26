from __future__ import annotations

import configparser
import json
from pathlib import Path
from typing import Any

from platformdirs import user_config_dir

APP_NAME = "valora"
CONFIG_FILENAME = "valora.ini"

DEFAULT_STATE: dict[str, Any] = {
    "server": "",
    "llama_cli": "",
    "folder": "",
    "llama_cpp_path": "",
    "config_path": "",
    "selected_model": "",
    "model_loaded": False,
    "ctx": "4096",
    "gpu_layers": "auto",
    "port": "8080",
    "threads": "auto",
    "kv_cache_k": "f16",
    "kv_cache_v": "f16",
    "daemon_port": 11435,
    "daemon_internal_port": 11436,
    "cloud_provider": "disabled",
    "cloud_api_key": "",
    "cloud_model": "",
    "hf_api_token": "",
    "provider_profiles": {},
    "chat_provider": "",
    "chat_model": "",
    "mcp_enabled": False,
    "mcp_serpapi_api_key": "",
    "mcp_server_template": "https://mcp.serpapi.com/{api_key}/mcp",
    "mcp_server_url": "",
    "debug_mode": False,
    "ollama_mode": False,
    "server_type": 0,
    "llama_cpp_build": "",
}

state = DEFAULT_STATE.copy()


def get_config_path() -> Path:
    config_dir = Path(user_config_dir(APP_NAME))
    return config_dir / CONFIG_FILENAME


def _coerce_bool(value: str | bool, default: bool) -> bool:
    if isinstance(value, bool):
        return value
    lowered = str(value).strip().lower()
    if lowered in {"1", "true", "yes", "on"}:
        return True
    if lowered in {"0", "false", "no", "off"}:
        return False
    return default


def _coerce_int(value: str | int, default: int) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def _build_mcp_server_url(api_key: str, template: str) -> str:
    cleaned_key = str(api_key).strip()
    cleaned_template = str(template).strip()
    if not cleaned_key or "{api_key}" not in cleaned_template:
        return ""
    return cleaned_template.format(api_key=cleaned_key)


def _coerce_dict(value: str | dict[str, Any], default: dict[str, Any]) -> dict[str, Any]:
    if isinstance(value, dict):
        return value
    cleaned = str(value).strip()
    if not cleaned:
        return default.copy()
    try:
        parsed = json.loads(cleaned)
    except json.JSONDecodeError:
        return default.copy()
    if not isinstance(parsed, dict):
        return default.copy()
    return parsed


def _apply_parser(parser: configparser.ConfigParser) -> None:
    state.update(DEFAULT_STATE)
    state["config_path"] = str(get_config_path())

    paths = parser["paths"] if parser.has_section("paths") else {}
    server = parser["server"] if parser.has_section("server") else {}
    cloud = parser["cloud"] if parser.has_section("cloud") else {}
    providers = parser["providers"] if parser.has_section("providers") else {}
    mcp = parser["mcp"] if parser.has_section("mcp") else {}
    daemon = parser["daemon"] if parser.has_section("daemon") else {}

    state["server"] = paths.get("server", state["server"])
    state["llama_cli"] = paths.get("llama_cli", state["llama_cli"])
    state["folder"] = paths.get("folder", state["folder"])
    state["llama_cpp_path"] = paths.get("llama_cpp_path", state["llama_cpp_path"])
    state["llama_cpp_build"] = paths.get("llama_cpp_build", state["llama_cpp_build"])

    state["selected_model"] = server.get("selected_model", state["selected_model"])
    state["model_loaded"] = _coerce_bool(server.get("model_loaded", state["model_loaded"]), False)
    state["ctx"] = str(server.get("ctx", state["ctx"]))
    state["gpu_layers"] = str(server.get("gpu_layers", state["gpu_layers"]))
    state["port"] = str(server.get("port", state["port"]))
    state["threads"] = str(server.get("threads", state["threads"]))
    state["kv_cache_k"] = str(server.get("kv_cache_k", state["kv_cache_k"]))
    state["kv_cache_v"] = str(server.get("kv_cache_v", state["kv_cache_v"]))
    state["debug_mode"] = _coerce_bool(server.get("debug_mode", state["debug_mode"]), False)
    state["ollama_mode"] = _coerce_bool(server.get("ollama_mode", state["ollama_mode"]), False)
    state["server_type"] = _coerce_int(server.get("server_type", state["server_type"]), 0)

    state["cloud_provider"] = str(cloud.get("provider", state["cloud_provider"]))
    state["cloud_api_key"] = str(cloud.get("api_key", state["cloud_api_key"]))
    state["cloud_model"] = str(cloud.get("model", state["cloud_model"]))
    state["hf_api_token"] = str(cloud.get("hf_api_token", state["hf_api_token"]))
    state["provider_profiles"] = _coerce_dict(
        providers.get("profiles_json", ""),
        {},
    )
    state["chat_provider"] = str(providers.get("chat_provider", state["chat_provider"]))
    state["chat_model"] = str(providers.get("chat_model", state["chat_model"]))

    state["mcp_enabled"] = _coerce_bool(mcp.get("enabled", state["mcp_enabled"]), False)
    state["mcp_serpapi_api_key"] = str(mcp.get("serpapi_api_key", state["mcp_serpapi_api_key"]))
    state["mcp_server_template"] = str(mcp.get("server_template", state["mcp_server_template"]))
    state["mcp_server_url"] = _build_mcp_server_url(
        state["mcp_serpapi_api_key"],
        state["mcp_server_template"],
    )

    state["daemon_port"] = _coerce_int(daemon.get("port", state["daemon_port"]), 11435)
    state["daemon_internal_port"] = _coerce_int(
        daemon.get("internal_port", state["daemon_internal_port"]),
        11436,
    )


def load_config() -> bool:
    parser = configparser.ConfigParser()
    config_path = get_config_path()
    state["config_path"] = str(config_path)

    if not config_path.exists():
        state.update(DEFAULT_STATE)
        state["config_path"] = str(config_path)
        return False

    try:
        parser.read(config_path, encoding="utf-8")
        _apply_parser(parser)
        return True
    except (configparser.Error, OSError, UnicodeDecodeError):
        state.update(DEFAULT_STATE)
        state["config_path"] = str(config_path)
        return False


def save_config() -> bool:
    parser = configparser.ConfigParser()
    config_path = get_config_path()

    parser["paths"] = {
        "server": str(state["server"]),
        "llama_cli": str(state["llama_cli"]),
        "folder": str(state["folder"]),
        "llama_cpp_path": str(state["llama_cpp_path"]),
        "llama_cpp_build": str(state.get("llama_cpp_build", "")),
    }
    parser["server"] = {
        "selected_model": str(state["selected_model"]),
        "model_loaded": str(state["model_loaded"]),
        "ctx": str(state["ctx"]),
        "gpu_layers": str(state["gpu_layers"]),
        "port": str(state["port"]),
        "threads": str(state["threads"]),
        "kv_cache_k": str(state["kv_cache_k"]),
        "kv_cache_v": str(state["kv_cache_v"]),
        "debug_mode": str(state["debug_mode"]),
        "ollama_mode": str(state["ollama_mode"]),
        "server_type": str(state["server_type"]),
    }
    parser["cloud"] = {
        "provider": str(state["cloud_provider"]),
        "api_key": str(state["cloud_api_key"]),
        "model": str(state["cloud_model"]),
        "hf_api_token": str(state["hf_api_token"]),
    }
    parser["providers"] = {
        "profiles_json": json.dumps(state.get("provider_profiles", {}), ensure_ascii=True),
        "chat_provider": str(state.get("chat_provider", "")),
        "chat_model": str(state.get("chat_model", "")),
    }
    parser["mcp"] = {
        "enabled": str(state["mcp_enabled"]),
        "serpapi_api_key": str(state["mcp_serpapi_api_key"]),
        "server_template": str(state["mcp_server_template"]),
    }
    parser["daemon"] = {
        "port": str(state["daemon_port"]),
        "internal_port": str(state["daemon_internal_port"]),
    }

    state["mcp_server_url"] = _build_mcp_server_url(
        state["mcp_serpapi_api_key"],
        state["mcp_server_template"],
    )

    try:
        config_path.parent.mkdir(parents=True, exist_ok=True)
        with config_path.open("w", encoding="utf-8") as handle:
            parser.write(handle)
        state["config_path"] = str(config_path)
        return True
    except OSError:
        return False
