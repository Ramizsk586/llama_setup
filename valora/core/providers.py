from __future__ import annotations

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class ProviderSpec:
    slug: str
    label: str
    category: str
    protocol: str
    default_base_url: str
    auth_kind: str
    supports_chat: bool
    notes: str


PROVIDERS: dict[str, ProviderSpec] = {
    "openai": ProviderSpec(
        slug="openai",
        label="OpenAI",
        category="Cloud LLM",
        protocol="openai",
        default_base_url="https://api.openai.com/v1",
        auth_kind="api_key",
        supports_chat=True,
        notes="official cloud chat",
    ),
    "groq": ProviderSpec(
        slug="groq",
        label="Groq",
        category="Cloud LLM",
        protocol="openai",
        default_base_url="https://api.groq.com/openai/v1",
        auth_kind="api_key",
        supports_chat=True,
        notes="OpenAI-compatible",
    ),
    "gemini": ProviderSpec(
        slug="gemini",
        label="Gemini",
        category="Cloud LLM",
        protocol="gemini",
        default_base_url="https://generativelanguage.googleapis.com/v1beta",
        auth_kind="api_key",
        supports_chat=True,
        notes="Google Generative Language API",
    ),
    "ollama-cloud": ProviderSpec(
        slug="ollama-cloud",
        label="Ollama Cloud",
        category="Cloud LLM",
        protocol="openai",
        default_base_url="",
        auth_kind="api_key",
        supports_chat=True,
        notes="set your cloud endpoint manually",
    ),
    "valora-local": ProviderSpec(
        slug="valora-local",
        label="Valora llama.cpp",
        category="Local LLM",
        protocol="openai",
        default_base_url="http://127.0.0.1:8080/v1",
        auth_kind="none",
        supports_chat=True,
        notes="local llama.cpp server",
    ),
    "ollama": ProviderSpec(
        slug="ollama",
        label="Ollama",
        category="Local LLM",
        protocol="openai",
        default_base_url="http://127.0.0.1:11434/v1",
        auth_kind="none",
        supports_chat=True,
        notes="local OpenAI-compatible endpoint",
    ),
    "lmstudio": ProviderSpec(
        slug="lmstudio",
        label="LM Studio",
        category="Local LLM",
        protocol="openai",
        default_base_url="http://127.0.0.1:1234/v1",
        auth_kind="none",
        supports_chat=True,
        notes="local OpenAI-compatible endpoint",
    ),
    "vllm": ProviderSpec(
        slug="vllm",
        label="vLLM",
        category="Local LLM",
        protocol="openai",
        default_base_url="http://127.0.0.1:8000/v1",
        auth_kind="none",
        supports_chat=True,
        notes="local OpenAI-compatible endpoint",
    ),
    "huggingface": ProviderSpec(
        slug="huggingface",
        label="HF API",
        category="Utility API",
        protocol="utility",
        default_base_url="",
        auth_kind="token",
        supports_chat=False,
        notes="repo access and model downloads",
    ),
    "serpapi": ProviderSpec(
        slug="serpapi",
        label="SerpAPI",
        category="Utility API",
        protocol="utility",
        default_base_url="https://mcp.serpapi.com/{api_key}/mcp",
        auth_kind="api_key",
        supports_chat=False,
        notes="search and MCP integration",
    ),
}


def get_provider(slug: str) -> ProviderSpec | None:
    return PROVIDERS.get(slug.strip().lower())
