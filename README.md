# Amelia Qt6 v6.99

Amelia is a local-first Qt6/C++ coding and cloud assistant that talks to a local Ollama server, stores its state under `~/.amelia_qt6`, indexes a local knowledge base, and can optionally use sanitized external web search through SearXNG.

This build keeps the earlier **bootstrap visibility**, **incremental indexing**, **responsive prompt preparation**, **transcript formatting**, and **Prompt Lab enhancements**, and adds **native desktop notifications** for meaningful task lifecycle events.

It also adds a **response lifecycle progress bar** in the status area, so Amelia now shows visual progress from prompt preparation through external search / backend wait time until the answer stream completes.

## What's new in v6.99

### Native desktop notifications

Amelia now emits desktop notifications for meaningful task lifecycle events through `QSystemTrayIcon`, with `QApplication::alert()` as a fallback when a native tray popup is unavailable.

Covered events include:

- startup complete
- prompt start / completion / failure / manual stop
- knowledge import start / completion / failure
- knowledge indexing start / completion / blocked state
- external search start / completion / failure
- Ollama probe start / completion / failure
- model refresh start / completion
- conversation create / restore
- memory save / clear
- active model change

Fresh configs now default to:

- `enableDesktopNotifications: true`
- `notifyOnTaskStart: true`
- `notifyOnTaskSuccess: true`
- `notifyOnTaskFailure: true`
- `desktopNotificationTimeoutMs: 7000`

### Earlier v6.96 improvements still present

- Knowledge Base tab now includes live filename/path filtering with a visible match counter
- bootstrap dialog appears immediately at startup
- logo spinner + bootstrap log window remain in place until the main window shows
- incremental KB refresh avoids rebuilding the whole cache for one changed or added asset
- prompt context preparation remains off the main thread
- PDF ingestion remains asynchronous with progress visible in the UI
- Prompt Lab enhancements remain in place
- transcript formatting / copy helpers remain in place
- semantic retrieval / external search defaults remain enabled for fresh configs

## Versioning

- Version is now `6.99`.
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

This build keeps the existing UI enhancements already present in your tree, including:

- richer Prompt Lab presets and KB-asset fields
- Browse files / Browse folder helpers
- Copy recipe
- colored transcript rendering
- fenced code formatting
- Copy answer
- Copy code block(s)

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

- Knowledge Base tab now supports live filename/path filtering for indexed assets.
- Diagnostics tab now includes an optional **Capture reasoning trace** toggle. When enabled, Amelia asks Ollama for backend thinking streams when supported and also records explicit tagged reasoning notes if the model emits them. This remains intentionally separate from any hidden internal chain-of-thought.
- Session list now includes **Delete selected** to remove an individual saved conversation from history.
- Knowledge Base now supports **Use once** and **Pin** actions so indexed assets can be prioritized for retrieval. One-shot priorities are consumed by the next prompt; pinned assets stay active until cleared. Active priorities are shown in a dedicated panel near the prompt box.
- The **Privacy** tab was moved to the end of the inspection tabs for a cleaner flow.
