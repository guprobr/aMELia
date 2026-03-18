# Amelia Qt6 v6.69

Amelia is a local-first Qt6/C++ coding and cloud assistant. It talks to a local Ollama server, stores state under `~/.amelia_qt6`, uses a local knowledge base, and can optionally enrich answers with sanitized external search.

This build preserves the Prompt Lab / KB-aware workflow and carries forward the transcript-widget upgrade line that started after v6.2.

## What changed since v6.2

### v6.5 transcript and Prompt Lab upgrade

The main transcript is no longer treated like a raw text dump.

It now provides:

- richer visual separation between `USER`, `ASSISTANT`, `SYSTEM`, and `STATUS`
- fenced code blocks rendered in dedicated code panels
- markdown-style rendering for headings, lists, quotes, and inline code inside answers
- more robust transcript restore behavior for multiline answers and code-heavy replies
- a copy-friendly answer surface for patch, runbook, and KB workflows

Transcript copy helpers now include:

- **Copy answer** for the last assistant response
- **Copy transcript** for the whole visible conversation
- **Copy code block** using a selector populated from fenced blocks detected in the last assistant response
- transcript context-menu actions for the same copy operations

### Prompt Lab improvements carried forward

Prompt Lab is aimed at real KB and patch workflows, not just toy prompt composition.

It includes:

- multiple grounded presets
- a local asset field for files and folders that still need importing
- browse helpers for files and folders
- a KB asset/reference field for assets that are already indexed
- notes/constraints for style, schema, or operational guidance
- recipe generation, copy, and “use in input” helpers

### Grounding and retrieval fixes

The grounding path was updated so Amelia is less likely to over-refuse when there is partial but usable KB context.

The current behavior is:

- prefer grounded KB / retrieved external context
- avoid inventing unsupported project facts
- use the hard fallback only when there is genuinely no usable retrieved context
- summarize supported themes first when the KB is only partially relevant

External-search policy was broadened so prompts such as:

- `search the internet`
- `look up`
- `who is`
- `what is`
- `latest`
- `current`
- `recent`

can trigger external retrieval when allowed.

### Async indexing and large-PDF usability

Local reindexing is asynchronous so `pdftotext` does not freeze the main UI during KB refresh.

The UI now shows progress for:

- local file scanning
- PDF extraction
- embedding / cache rebuild phases

### v6.69 default-config update

The shipped defaults now favor a more usable KB-first experience out of the box.

Default values are now:

- `enableSemanticRetrieval: true`
- `enableExternalSearch: true`
- `autoSuggestExternalSearch: true`
- `ollamaResponseHeadersTimeoutMs: 1800000`

These defaults apply both to the in-code defaults and to `config/config.example.json`.

## Runtime layout

Amelia stores runtime data in:

- `~/.amelia_qt6`

Typical content:

- `~/.amelia_qt6/config.json`
- `~/.amelia_qt6/conversations/`
- `~/.amelia_qt6/conversations_index.json`
- `~/.amelia_qt6/memories.json`
- `~/.amelia_qt6/state.json`
- `~/.amelia_qt6/rag_cache.json`
- `~/.amelia_qt6/knowledge/`

On first run, Amelia creates `~/.amelia_qt6`, `~/.amelia_qt6/knowledge`, and seeds `~/.amelia_qt6/config.json` from the installed example config when available.

## Config

Packaged example config:

- `config/config.example.json`

Preferred user config path:

- `~/.amelia_qt6/config.json`

Important note:

- once `~/.amelia_qt6/config.json` already exists, user-config values override newly shipped defaults until that file is edited or recreated.

## Recommended local models

For dependable coding and cloud work:

- `qwen2.5-coder:14b` — balanced default
- `qwen2.5-coder:7b` — lighter fallback
- `qwen3-coder:30b` — heavier option if your hardware supports it

Example:

```bash
ollama pull qwen2.5-coder:14b
curl http://127.0.0.1:11434/api/tags
```

## Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j
cmake --install .
```

## Desktop install

`cmake --install .` installs:

- desktop entry: `${CMAKE_INSTALL_PREFIX}/share/applications/amelia_qt6.desktop`
- icon: `${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps/amelia_qt6.svg`
- example config: `${CMAKE_INSTALL_PREFIX}/share/amelia_qt6/config/config.example.json`

## Notes

- Amelia is intentionally local-first.
- External search is enabled by default in the shipped config/defaults, but user config still wins if it already exists.
- Prompt Lab prepares grounded prompts and dataset-style examples; it does not fine-tune models by itself.
- PDF ingestion still depends on `pdftotext` being available on the system.
- If you want old configs to inherit the new defaults, update or recreate `~/.amelia_qt6/config.json`.

## About

A local-first AI assistant built in C++ and Qt6 using Ollama, with persistent KB, prompt budgeting, diagnostics, Prompt Lab, and copy-friendly transcript formatting.
