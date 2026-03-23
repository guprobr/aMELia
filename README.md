# aMELia Qt6 v9.14.3

Amelia is a local-first Qt6/C++ coding and cloud assistant that talks to a local Ollama server, stores its state under `~/.amelia_qt6`, indexes a local knowledge base, and can optionally use sanitized external web search through SearXNG.

This build rolls forward the existing bootstrap, indexing, transcript, Prompt Lab, notification, and progress-bar work, and adds a Knowledge Base collection model with preserved folder structure, a tree-view browser, a hard-locked Knowledge Base root and safer workspace-jail boundaries under `~/.amelia_qt6`, stronger transcript code-block handling, first-run service prompts, and a full JSON configuration editor. aMELia is also allegorically considered a MEL: Model Enhancement Lab.

NOTE: prompt transcripts are first generated in markdown but after it finishes, they should be properly formatted.

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

Pull the recommended default generation model and the dedicated embedding model:

```bash
ollama pull gpt-oss:20b
ollama pull embeddinggemma:latest
```

`gpt-oss:20b` is the recommended default in Amelia because it is available directly in the Ollama library and is designed for powerful reasoning and developer use cases. On Windows, Amelia pairs well with Ollama's Vulkan GPU path when your driver / hardware stack supports it.

Quick API tests:

```bash
curl http://localhost:11434/api/generate -d '{
  "model": "gpt-oss:20b",
  "prompt": "hello"
}'
```

```bash
curl http://localhost:11434/api/embed -d '{
  "model": "embeddinggemma:latest",
  "input": "hello"
}'
```

If your Ollama runtime is older and responds with 404 on `/api/embed`, Amelia automatically retries the legacy `/api/embeddings` route.

### Ollama in Docker

CPU-only quick start:

```bash
docker run -d \
  -v ollama:/root/.ollama \
  -p 11434:11434 \
  --name ollama \
  ollama/ollama
```

Then pull the recommended chat model and embedding model inside the container:

```bash
docker exec -it ollama ollama pull gpt-oss:20b
docker exec -it ollama ollama pull embeddinggemma:latest
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
- `~/.amelia_qt6/knowledge/collections/`
- `~/.amelia_qt6/knowledge/.amelia_kb_manifest.json`
- `~/.amelia_qt6/workspace/`
- `~/.amelia_qt6/workspace/runtime/`

Preferred user config path:

- `~/.amelia_qt6/config.json`

## Notes about existing configs

Changing defaults in source files does **not** overwrite an existing user config.

If you already have:

- `~/.amelia_qt6/config.json`

then its values still win. Update that file manually if you want the new defaults on an existing installation.

Note: `knowledgeRoot` is now normalized under Amelia's active `dataRoot`, so it can no longer relocate the Knowledge Base outside Amelia's own storage jail.

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

## All aMELia Qt6 features

- **Local-first desktop app** built with C++ and Qt6
- **Local Ollama integration** for model generation, model refresh, backend probing, and model selection
- **Persistent local state** under `~/.amelia_qt6` for config, conversations, memories, summaries, KB cache, collection manifests, and workspace jail data
- **Session management** with create, restore, list, and delete conversation workflows
- **Rich transcript view** with colored role cards, Markdown rendering, fenced-code rendering, clickable code-copy links, and clipboard copy of the last answer
- **Transcript sanitization** that neutralizes raw HTML-like tags before Markdown rendering to avoid broken layouts
- **Exact code-block transcript handling** with stable copy links and stronger indentation preservation
- **Manual Memory** capture plus persisted memory storage / clearing
- **Prompt-safe memory reuse** for stored memories that you save manually, so reused memory text is trimmed and filtered before it is re-injected into later prompts
- **Per-memory deletion UI** from the structured **Memory** tab
- **Memory details panel** with description, confidence, pin state, and timestamps for the selected memory
- **Auto-memory disabled** by default in this build to avoid prompt-loop feedback; use **Manual Memory** when you want to persist something intentionally
- **Knowledge Base ingestion** from files and folders with preserved collection structure
- **Knowledge Base collections** with immutable IDs, user-facing unique labels, rename support, manifest-backed grouping, and a KB root locked under Amelia's data root
- **Knowledge Base inspection** with source summary, searchable tree view, collection/folder expanders, sorting by name or file type, remove-selected, and clear-KB actions
- **Knowledge Base prioritization** with **Use once** and **Pin** actions plus an active-priority panel near the prompt box
- **Incremental indexing** so changed assets can be refreshed without rebuilding the entire cache
- **Content-hash reuse** so touched-but-unchanged assets can skip reparsing and re-embedding
- **Shared chunk embedding reuse** so duplicate chunk text across assets can borrow cached embeddings instead of calling Ollama again
- **Partial-safe cancellation** so user-canceled reindexes keep finished work and discard only the in-flight file
- **Tree-view asset moves** so Knowledge Base files can be dragged to another collection or folder without re-importing them
- **Asynchronous PDF ingestion** and non-blocking KB analysis
- **Semantic retrieval** with a real Ollama embedding path plus automatic local fallback
- **Structure-aware chunking** that preserves headings, code fences, page markers, and list regions more faithfully
- **Grounded local-source panel** showing local evidence used for answers
- **Sanitized external search** through SearXNG, with an explicit per-prompt allow checkbox
- **External-source panel** showing sanitized external evidence
- **Privacy preview panel** showing what context is being shared with the backend
- **Outline planning** and outline-first document / procedure generation support
- **Prompt Lab** with presets, local asset helpers, KB-asset references, notes / constraints, recipe composition, clipboard copy, and input injection
- **Backend summary panel** for runtime/backend/config visibility
- **Diagnostics panel** for operational logs and optional reasoning-trace capture
- **Reasoning trace toggle** for backend thinking streams when exposed by the selected model/backend
- **Desktop notifications** for meaningful task lifecycle events, excluding model refresh/change toasts
- **System tray controls** with Show / Hide / Exit actions
- **Busy indicator and response progress bar** for long-running operations and streamed answer progress
- **Bootstrap dialog** shown immediately at startup while initialization completes
- **Tooltips across the UI** for buttons, tabs, lists, and major controls
- **Config-driven behavior** with user-overridable defaults in `~/.amelia_qt6/config.json`
- **Optional external grounding controls** including domain allowlist and timeout configuration
- **Operational diagnostics** for backend, search, RAG, startup, planner, memory, and related categories

## Cache / index regeneration notes

- Most code changes do **not** require a manual forced cache wipe, but Amelia will automatically invalidate older KB caches when the chunking strategy changes.
- This build upgrades the KB cache format to **`amelia-rag-cache-v3`** and stores per-file content hashes plus per-chunk fingerprints for faster reuse on later reindexes.
- Moving or renaming assets inside the Knowledge Base **does** change their stored path / collection metadata, so Amelia refreshes the KB index after those operations.
- Cancel-index support remains backward-compatible with the partial-safe cache write path.

## Recent UI additions

- The **Memory** tab now shows persisted entries in a structured table and supports **Delete selected** for one-at-a-time cleanup.
- Knowledge Base tab supports live filename/path filtering for indexed assets.
- Diagnostics includes an optional **Capture reasoning trace** toggle. When enabled, Amelia asks Ollama for backend thinking streams when supported and also records explicit tagged reasoning notes if the model emits them. This remains intentionally separate from any hidden internal chain-of-thought.
- Session list includes **Delete selected** to remove an individual saved conversation from history.
- Knowledge Base supports **Use once** and **Pin** actions so indexed assets can be prioritized for retrieval. One-shot priorities are consumed by the next prompt; pinned assets stay active until cleared. Active priorities are shown in a dedicated panel near the prompt box.
- **Knowledge Base** is now the second inspection tab for a faster review workflow.
- The external-search checkbox now defaults to off on fresh installs/configs.
- The transcript renderer now sanitizes raw HTML-like fragments before Markdown rendering.

---

- For large document-study prompts, Amelia now omits **FULL_DOCUMENT_TEXT** entirely and relies on the **DOCUMENT_OUTLINE_MAP** plus **SECTION_COVERAGE_PACKET**, which prevents huge PDFs from crowding out late sections and overloading Ollama.
- Document-study payloads are now slimmer overall: fewer coverage hits, a much smaller retrieved-hit sidecar, and a lower local-context budget tuned for stability instead of giant front-loaded packets.
- Heavy document-study requests now force **think=false** for the active Ollama call, reducing backend load and avoiding runner crashes on large HLD/manual summaries.

- Document-study prompts now build a **SECTION_COVERAGE_PACKET** instead of spending most of the budget on a single front-trimmed full-document blob.
- Major top-level sections are mapped to chunk anchors and the prompt budget is distributed across those sections, so late chapters survive much more reliably.
- For document-study requests, the ordinary retrieved-hit appendix is now trimmed much harder so it does not crowd out the section sweep.
- Prompt diagnostics now also report `section_packets` so you can verify the new path in one run.
