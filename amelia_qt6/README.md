# Amelia Qt6 v6.96

Amelia is a local-first Qt6/C++ coding and cloud assistant that talks to a local Ollama server, stores its state under `~/.amelia_qt6`, indexes a local knowledge base, and can optionally use sanitized external web search through SearXNG.

This build focuses on **startup visibility**, **incremental indexing**, and **keeping the UI responsive** as the knowledge base grows.

## What's new in v6.96

### Bootstrap visibility

- Startup now shows a **bootstrap dialog** immediately instead of leaving the screen blank.
- The bootstrap dialog uses the Amelia logo as a rotating spinner.
- A live **bootstrap log window** shows config/data-root/bootstrap messages until the main window is displayed.
- The bootstrap dialog closes automatically once the main window is up.

### Incremental indexing

- Amelia no longer throws away the whole RAG index just because **one file changed** or **one more asset was added**.
- The indexer now reuses cached chunks for unchanged files and rebuilds only:
  - new files
  - changed files
  - removed-file deltas
- On startup, Amelia can load the cache and then schedule an **incremental background refresh** only when the KB changed.

### Lower UI freeze risk

- Prompt submission now prepares local context **off the main thread**.
- Large KB retrieval and outline preparation no longer block the main UI while you wait after pressing **Send**.
- PDF indexing remains asynchronous, with progress visible in the status bar.

### Default config changes

These defaults are now enabled for fresh configs:

- `enableSemanticRetrieval: true`
- `enableExternalSearch: true`
- `autoSuggestExternalSearch: true`
- `ollamaResponseHeadersTimeoutMs: 1800000`

### Versioning

- Version is now `6.96`.
- The display version comes from a single place:
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

- `qt6-base-dev` -> Qt Core / Widgets / Network / Concurrent used by Amelia
- `qt6-tools-dev` and `qt6-tools-dev-tools` -> standard Qt6 dev tooling on Ubuntu
- `qt6-svg-dev` / `qt6-imageformats-plugins` -> smoother SVG logo handling at runtime
- `poppler-utils` -> provides `pdftotext`, which Amelia uses to ingest PDFs

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
sudo systemctl start ollama
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

Amelia now behaves better with large KBs:

- cached KB state can load first
- stale-cache detection uses a lighter source-level comparison
- incremental refresh rebuilds only changed/new files
- sending a prompt no longer blocks the UI while retrieval/outline prep happens

## Prompt Lab and transcript helpers still present

This build keeps the existing UI enhancements already merged in your tree, including:

- richer Prompt Lab presets and KB-asset fields
- Browse files / Browse folder helpers
- Copy recipe
- colored transcript rendering
- fenced code formatting
- Copy answer
- Copy code block(s)

## Troubleshooting

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
