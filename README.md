# aMELia Qt6 v7.9

Amelia is a local-first Qt6/C++ coding and cloud assistant that talks to a local Ollama server, stores its state under `~/.amelia_qt6`, indexes a local knowledge base, and can optionally use sanitized external web search through SearXNG.

This build rolls forward the existing bootstrap, indexing, transcript, Prompt Lab, notification, and progress-bar work, and adds a Knowledge Base collection model with preserved folder structure, a tree-view browser, a hard-locked Knowledge Base root and safer workspace-jail boundaries under `~/.amelia_qt6`, stronger transcript code-block handling, first-run service prompts, and a full JSON configuration editor. aMELia is also allegorically considered a MEL: Model Enhancement Lab.

NOTE: prompt transcripts are first generated in markdown but after it finishes, they should be properly formatted.

## What's new in v7.9

### Cancel / queue handling and safer KB refresh

- canceling indexing now also drops the remaining queued files from that run, so Amelia does not immediately continue re-indexing assets the user explicitly canceled
- the Knowledge Base tree now switches into a locked refresh view while inventory is being rebuilt, with an animated status line instead of leaving stale rows visible
- Knowledge Base tree controls are disabled while the refreshed inventory is pending, which reduces stale drag/drop and stale-selection mistakes during reindex-triggering operations
- operations that require a Knowledge Base rebuild now ask for confirmation up front and explicitly explain that Amelia will refresh and reindex afterwards
- collection right-click menus now include **Add file to collection** and **Add folder to collection** shortcuts

### Large-asset ingestion, parsing, and chunk coverage

- large assets now use a more adaptive chunking profile instead of one fixed chunk shape for everything
- chunk sizing now scales by source type and asset size, which improves large-document coverage while reducing wasted overlap on huge files
- duplicate chunk bodies from repeated boilerplate are now filtered before embedding so oversized manuals and exported docs waste less embedding time
- PDF ingestion now strips repeated page headers / footers more aggressively and can fall back to a raw `pdftotext` pass when the layout pass yields thin coverage
- imported assets now track extra ingestion metadata including text chars, line count, word count, and the chunking profile used
- asset properties can now show a bounded chunk-dump preview so large assets are easier to inspect without flooding the UI

### Knowledge Base tree actions and safer move behavior

- Knowledge Base collections now support **Create / Delete / Rename / Properties** from the right-click menu
- individual Knowledge Base assets now support **Rename / Delete / Properties** from the right-click menu
- collection and asset property dialogs now expose more relevant stored metadata, and asset properties can include a chunk preview dump
- tree drag/drop now behaves more safely when moving assets, and failed moves force an immediate inventory refresh instead of leaving items visually missing until another manual refresh
- manifest persistence for move operations is now rollback-aware, so a failed save is less likely to leave the KB in an inconsistent state

### Transcript table/code-block safety and model defaults

- transcript rendering now treats fenced code blocks more defensively when the opening fence appears mid-line, which prevents Markdown tables with multiline code snippets from corrupting the rest of the transcript
- default generation model is now **`gpt-oss:20b`**
- default embedding model is now **`embeddinggemma:latest`**
- README startup examples now recommend pulling `gpt-oss:20b` for generation and `embeddinggemma:latest` for embeddings

### Indexing visibility and KB footprint

- reindex progress now advances during long embedding runs instead of appearing frozen on large PDFs or remote Ollama setups
- embedding progress labels now include the current file and completed chunk count, for example `Embedding 1 / 3: manual.pdf — 48/221 chunks`
- neural-indexing chunking now uses a more compact profile with reduced overlap, which lowers duplicated embedding work and improves retrieval focus
- the default Ollama embedding batch size is now smaller, so progress updates arrive more often and long CPU-only embedding requests feel less stuck
- indexing now exposes a dedicated **Cancel index** button during reindexing runs
- canceling indexing now keeps already-committed files, discards the file that was in flight, and writes the partial-safe cache back to disk
- closing Amelia during indexing now triggers the same cancellation path so the app can shut down more cleanly
- Knowledge Base assets can now be **dragged and dropped between collections** directly from the existing tree view
- the Knowledge Base tab now includes a dedicated **footprint / stats panel** showing:
  - number of collections
  - total files
  - total chunks
  - total stored size under Amelia
  - per-collection file / chunk / size totals

### Real embeddings and safer retrieval fusion

- Amelia now tries to use a real **Ollama embedding model** for semantic retrieval, instead of relying only on the previous local hash-vector approximation
- the embedding client now supports both Ollama embedding API variants: it tries modern `POST /api/embed` first and automatically retries legacy `POST /api/embeddings` when the server answers 404
- if the configured embedding model is unavailable, Amelia automatically falls back to the previous local hash embedder so the KB remains usable
- backend diagnostics now preserve the last embedding error summary, so it is easier to tell whether neural embeddings are active or the local fallback is being used
- RAG cache metadata now tracks the embedding backend and chunking strategy, so stale caches are discarded when retrieval internals change
- retrieval weighting is now more conservative: lexical evidence stays primary unless a true neural embedding vector is actually available
- new config keys:
  - `ollamaEmbeddingModel`
  - `ollamaEmbeddingTimeoutMs`
  - `ollamaEmbeddingBatchSize`

### Higher-quality chunking

- chunking is now **structure-aware** instead of mostly raw size-based, and the invalid bullet-list regex from the previous build has been fixed
- Markdown headings, PDF page markers, fenced code blocks, bullets, and command/config-like regions are kept together more often
- oversized sections are split on paragraph, line, or whitespace boundaries before a hard cut is used
- overlap is carried by semantic blocks instead of character offsets, reducing mid-paragraph and mid-code splits

### Knowledge Base collections and safer structure handling

- display version bumped to `7.9`
- imported files and folders are now stored as **collections** with:
  - an immutable collection hash / ID
  - a user-visible **label** that can be renamed later
  - duplicate-label protection
- imported folder structure is preserved under the collection, so files with the same leaf name remain distinguishable
- standalone-file imports are grouped deterministically instead of being flattened into one ambiguous directory
- Knowledge Base metadata is now persisted through a manifest file, so collection identity, grouping, and labels survive reindexing
- Knowledge Base removal and clear operations now work against the manifest-aware model instead of a flat loose-file model

### Knowledge Base UI improvements

- the **Knowledge Base** tab now uses a **tree view** with expanders for:
  - collection label
  - folder / subgroup
  - file
- the tree can be sorted by **name** or **file type**
- selected collections can be **renamed** from the UI while keeping the immutable internal collection ID unchanged
- UI display names now prefer collection labels and relative paths instead of ambiguous bare file names

### Safer local workspace handling

- Amelia now exposes and prepares a dedicated workspace jail rooted under `~/.amelia_qt6/workspace`
- runtime scratch space is separated into `~/.amelia_qt6/workspace/runtime`
- backend / diagnostics views now show the workspace jail paths explicitly
- the Knowledge Base root is now hard-locked under Amelia's active data root (`<dataRoot>/knowledge`) instead of allowing it to point elsewhere
- configured `knowledgeRoot` values are normalized back to Amelia's own data root so KB operations stay jailed
- Knowledge Base operations continue to work on copies stored under Amelia's own data root, reducing the chance of acting blindly on the user's original filesystem

### Transcript and code-block fixes

- transcript **Copy code** links now use stable per-block indices again
- the copy-code parser no longer falls back to the wrong block once the transcript grows
- transcript code blocks now render with stricter `white-space: pre` handling, preserving indentation more faithfully in the final transcript output
- fenced-code rendering keeps exact code text in the clipboard flow instead of repeatedly copying the first block

### Tray menu improvement

- right-clicking the system tray icon now opens a useful context menu with:
  - **Show Amelia**
  - **Hide Amelia**
  - **Exit**

### Existing improvements still present


This release keeps the improvements from the earlier 6.9x line, including:

- native desktop notifications for startup, prompt lifecycle, indexing, memory, model refresh, and related events
- status-area progress feedback from prompt preparation through answer completion
- bootstrap dialog visibility at startup
- incremental / asynchronous knowledge-base indexing
- transcript formatting and copy helpers
- reasoning-only stall guard that retries once without backend thinking when a model loops before the first visible answer
- Prompt Lab asset-aware recipe composition
- semantic retrieval, external search integration, and outline-first planning

## All aMELia Qt6 features

- **Local-first desktop app** built with C++ and Qt6
- **Local Ollama integration** for model generation, model refresh, backend probing, and model selection
- **Persistent local state** under `~/.amelia_qt6` for config, conversations, memories, summaries, KB cache, collection manifests, and workspace jail data
- **Session management** with create, restore, list, and delete conversation workflows
- **Rich transcript view** with colored role cards, Markdown rendering, fenced-code rendering, clickable code-copy links, and clipboard copy of the last answer
- **Transcript sanitization** that neutralizes raw HTML-like tags before Markdown rendering to avoid broken layouts
- **Exact code-block transcript handling** with stable copy links and stronger indentation preservation
- **Manual Memory** capture plus persisted memory storage / clearing
- **Knowledge Base ingestion** from files and folders with preserved collection structure
- **Knowledge Base collections** with immutable IDs, user-facing unique labels, rename support, manifest-backed grouping, and a KB root locked under Amelia's data root
- **Knowledge Base inspection** with source summary, searchable tree view, collection/folder expanders, sorting by name or file type, remove-selected, and clear-KB actions
- **Knowledge Base prioritization** with **Use once** and **Pin** actions plus an active-priority panel near the prompt box
- **Incremental indexing** so changed assets can be refreshed without rebuilding the entire cache
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
- **Desktop notifications** for meaningful task lifecycle events
- **System tray controls** with Show / Hide / Exit actions
- **Busy indicator and response progress bar** for long-running operations and streamed answer progress
- **Bootstrap dialog** shown immediately at startup while initialization completes
- **Tooltips across the UI** for buttons, tabs, lists, and major controls
- **Config-driven behavior** with user-overridable defaults in `~/.amelia_qt6/config.json`
- **Optional external grounding controls** including domain allowlist and timeout configuration
- **Operational diagnostics** for backend, search, RAG, startup, planner, memory, and related categories

## Versioning

- Version is now `7.9`.
- The display version comes from one place only:
  - `src/core/appversion.h`

## Cache / index regeneration notes

- These code changes do **not** require a manual forced cache wipe, but Amelia will automatically invalidate older KB caches when the chunking strategy changes.
- Moving or renaming assets inside the Knowledge Base **does** change their stored path / collection metadata, so Amelia refreshes the KB index after those operations.
- Cancel-index support remains backward-compatible with the partial-safe cache write path.

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

## Recent UI additions

- Knowledge Base tab supports live filename/path filtering for indexed assets.
- Diagnostics includes an optional **Capture reasoning trace** toggle. When enabled, Amelia asks Ollama for backend thinking streams when supported and also records explicit tagged reasoning notes if the model emits them. This remains intentionally separate from any hidden internal chain-of-thought.
- Session list includes **Delete selected** to remove an individual saved conversation from history.
- Knowledge Base supports **Use once** and **Pin** actions so indexed assets can be prioritized for retrieval. One-shot priorities are consumed by the next prompt; pinned assets stay active until cleared. Active priorities are shown in a dedicated panel near the prompt box.
- **Knowledge Base** is now the second inspection tab for a faster review workflow.
- The external-search checkbox now defaults to off on fresh installs/configs.
- The transcript renderer now sanitizes raw HTML-like fragments before Markdown rendering.


## Recent changes

- 7.9 cancel / queue clearing: canceling indexing now drops the remaining queued files from that run instead of continuing to re-index assets the user explicitly canceled.
- 7.9 safer KB refresh UX: the Knowledge Base tree now switches to a locked refresh view with an animated status line while stale inventory is being rebuilt.
- 7.9 collection shortcuts: collection context menus now include **Add file to collection** and **Add folder to collection**, and reindex-triggering actions warn the user before they proceed.
- 7.8 large-asset ingestion: adaptive chunking, repeated-boilerplate reduction, duplicate-chunk filtering, richer ingestion metadata, and bounded chunk previews make large KB assets cheaper to inspect and better covered during indexing.
- 7.8 Knowledge Base context menus: collections now support create/delete/rename/properties and assets now support rename/delete/properties directly from the tree.
- 7.8 safer KB moves: failed drag/drop moves now refresh the inventory immediately, and manifest-save failures are rolled back more defensively.
- 7.1e1 transcript rendering hotfix: unsafe markdown tables containing fenced code or HTML line breaks are automatically rewritten into stacked sections before rendering, preventing the rest of the transcript from breaking.
