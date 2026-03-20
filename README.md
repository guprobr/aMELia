# Amelia Qt6 v7.0

Amelia is a local-first Qt6/C++ coding and cloud assistant that talks to a local Ollama server, stores its state under `~/.amelia_qt6`, indexes a local knowledge base, and can optionally use sanitized external web search through SearXNG.

This build rolls forward the existing bootstrap, indexing, transcript, Prompt Lab, notification, and progress-bar work, and adds a transcript-rendering hardening pass so mixed Markdown/HTML responses no longer corrupt the final formatted view.

## What's new in v7.0

### UI and workflow updates

- display version bumped to `7.0`
- **Knowledge Base** moved to the second inspection tab
- **Remember input** renamed to **Manual Memory**
- **Allow sanitized external search** now defaults to off for fresh configs and fresh UI state
- informative hover tooltips were added across the main interactive controls, tabs, and actions

### Transcript formatting hardening

The transcript renderer now sanitizes raw HTML-like tags before sending Markdown fragments into Qt's Markdown parser.

That prevents malformed assistant output such as literal `<br>` tags mixed into Markdown tables / fenced-code responses from breaking the rendered transcript.

The transcript segment parser was also hardened so fenced code blocks with trailing content on the same line are preserved correctly instead of corrupting the surrounding message layout.

### Existing improvements still present

This release keeps the improvements from the earlier 6.9x line, including:

- native desktop notifications for startup, prompt lifecycle, indexing, memory, model refresh, and related events
- status-area progress feedback from prompt preparation through answer completion
- bootstrap dialog visibility at startup
- incremental / asynchronous knowledge-base indexing
- transcript formatting and copy helpers
- Prompt Lab asset-aware recipe composition
- semantic retrieval, external search integration, and outline-first planning

## All aMELia Qt6 features

- **Local-first desktop app** built with C++ and Qt6
- **Local Ollama integration** for model generation, model refresh, backend probing, and model selection
- **Persistent local state** under `~/.amelia_qt6` for config, conversations, memories, summaries, and KB cache
- **Session management** with create, restore, list, and delete conversation workflows
- **Rich transcript view** with colored role cards, Markdown rendering, fenced-code rendering, clickable code-copy links, and clipboard copy of the last answer
- **Transcript sanitization** that neutralizes raw HTML-like tags before Markdown rendering to avoid broken layouts
- **Manual Memory** capture plus persisted memory storage / clearing
- **Knowledge Base ingestion** from files and folders
- **Knowledge Base inspection** with source summary, searchable asset list, remove-selected, and clear-KB actions
- **Knowledge Base prioritization** with **Use once** and **Pin** actions plus an active-priority panel near the prompt box
- **Incremental indexing** so changed assets can be refreshed without rebuilding the entire cache
- **Asynchronous PDF ingestion** and non-blocking KB analysis
- **Semantic retrieval** for stronger local relevance ranking
- **Grounded local-source panel** showing local evidence used for answers
- **Sanitized external search** through SearXNG, with an explicit per-prompt allow checkbox
- **External-source panel** showing sanitized external evidence
- **Privacy preview panel** showing what context is being shared with the backend
- **Outline planning** and outline-first document / procedure generation support
- **Prompt Lab** with presets, local asset helpers, KB-asset references, notes / constraints, recipe composition, clipboard copy, and input injection
- **Backend summary panel** for runtime/backend/config visibility
- **Diagnostics panel** for operational logs and optional reasoning-trace capture
- **Reasoning trace toggle** for backend thinking streams when exposed by the selected model/backend
- **Desktop notifications** for meaningful task lifecycle events
- **Busy indicator and response progress bar** for long-running operations and streamed answer progress
- **Bootstrap dialog** shown immediately at startup while initialization completes
- **Tooltips across the UI** for buttons, tabs, lists, and major controls
- **Config-driven behavior** with user-overridable defaults in `~/.amelia_qt6/config.json`
- **Optional external grounding controls** including domain allowlist and timeout configuration
- **Operational diagnostics** for backend, search, RAG, startup, planner, memory, and related categories

## Versioning

- Version is now `7.0`.
- The display version comes from one place only:
  - `src/appversion.h`

## Ubuntu packages

### Required to build Amelia

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  qt6-base-dev \
  qt6-tools-dev \
  qt6-tools-dev-tools \
  qt6-svg-dev \
  qt6-imageformats-plugins \
  poppler-utils \
  curl \
  git
```

Why these matter:

- `qt6-base-dev` -> Qt Core / Widgets / Network / Concurrent / tray integration
- `qt6-tools-dev` and `qt6-tools-dev-tools` -> standard Qt6 dev tooling on Ubuntu
- `qt6-svg-dev` / `qt6-imageformats-plugins` -> SVG logo rendering and runtime image support
- `poppler-utils` -> provides `pdftotext`, which Amelia uses to ingest PDFs
- `curl` -> convenient for testing Ollama and SearXNG endpoints

## Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j$(nproc)
cmake --install .
```

## Desktop install

`cmake --install .` installs:

- desktop entry: `${CMAKE_INSTALL_PREFIX}/share/applications/amelia_qt6.desktop`
- icon: `${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps/amelia_qt6.svg`
- example config: `${CMAKE_INSTALL_PREFIX}/share/amelia_qt6/config/config.example.json`

## Starting Ollama

### Native install on Ubuntu/Linux

```bash
curl -fsSL https://ollama.com/install.sh | sh
sudo systemctl enable --now ollama
sudo systemctl status ollama
```

Pull at least one model, for example:

```bash
ollama pull qwen2.5-coder:14b
```

Quick API test:

```bash
curl http://127.0.0.1:11434/api/generate -d '{
  "model": "qwen2.5-coder:14b",
  "prompt": "hello"
}'
```

### Ollama in Docker

CPU-only quick start:

```bash
docker run -d \
  -v ollama:/root/.ollama \
  -p 11434:11434 \
  --name ollama \
  ollama/ollama
```

Then pull a model inside the container:

```bash
docker exec -it ollama ollama pull qwen2.5-coder:14b
```

## Starting SearXNG search container

Quick container setup:

```bash
mkdir -p ./searxng/config ./searxng/data

docker pull docker.io/searxng/searxng:latest

docker run --name searxng -d \
  -p 8080:8080 \
  -v "$(pwd)/searxng/config:/etc/searxng" \
  -v "$(pwd)/searxng/data:/var/cache/searxng" \
  docker.io/searxng/searxng:latest
```

Amelia expects, by default:

```json
"searxngUrl": "http://127.0.0.1:8080/search"
```

If you prefer another host port, update Amelia's config accordingly.

## Runtime layout

Amelia stores runtime data in:

- `~/.amelia_qt6/config.json`
- `~/.amelia_qt6/conversations/`
- `~/.amelia_qt6/conversations_index.json`
- `~/.amelia_qt6/memories.json`
- `~/.amelia_qt6/state.json`
- `~/.amelia_qt6/rag_cache.json`
- `~/.amelia_qt6/knowledge/`

Preferred user config path:

- `~/.amelia_qt6/config.json`

## Notes about existing configs

Changing defaults in source files does **not** overwrite an existing user config.

If you already have:

- `~/.amelia_qt6/config.json`

then its values still win. Update that file manually if you want the new defaults on an existing installation.

## Knowledge-base behavior

Amelia behaves better with large KBs because:

- cached KB state can load first
- stale-cache detection uses a lighter source-level comparison
- incremental refresh rebuilds only changed/new files
- prompt preparation no longer blocks the UI thread while retrieval/outline prep runs

## Prompt Lab and transcript helpers still present

This build keeps the existing Prompt Lab and transcript helpers, including richer presets, KB-asset references, browse helpers, recipe copy, colored transcript rendering, fenced code formatting, answer copy, and code-block copy actions.

## Troubleshooting

### I do not receive desktop notifications

Check:

- `enableDesktopNotifications` in `~/.amelia_qt6/config.json`
- whether your desktop environment exposes a system tray / notification service
- whether tray popups are blocked by the shell or Do Not Disturb mode

Amelia falls back to `QApplication::alert()` when native tray popups are not available, but that fallback is less visible than a real notification balloon.

### PDFs do not index

Make sure `pdftotext` exists:

```bash
which pdftotext
```

If not:

```bash
sudo apt install poppler-utils
```

### New defaults did not take effect

Your existing user config is overriding the source defaults. Edit:

```bash
~/.amelia_qt6/config.json
```

### Amelia still feels slow with a huge KB

Main things to check:

- model size in Ollama
- number of indexed files and chunk count
- whether the KB is currently refreshing in the background
- whether your local disk is slow
- whether Ollama is CPU-only instead of GPU-backed

## Recent UI additions

- Knowledge Base tab supports live filename/path filtering for indexed assets.
- Diagnostics includes an optional **Capture reasoning trace** toggle. When enabled, Amelia asks Ollama for backend thinking streams when supported and also records explicit tagged reasoning notes if the model emits them. This remains intentionally separate from any hidden internal chain-of-thought.
- Session list includes **Delete selected** to remove an individual saved conversation from history.
- Knowledge Base supports **Use once** and **Pin** actions so indexed assets can be prioritized for retrieval. One-shot priorities are consumed by the next prompt; pinned assets stay active until cleared. Active priorities are shown in a dedicated panel near the prompt box.
- **Knowledge Base** is now the second inspection tab for a faster review workflow.
- The external-search checkbox now defaults to off on fresh installs/configs.
- The transcript renderer now sanitizes raw HTML-like fragments before Markdown rendering.
