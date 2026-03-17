# Amelia Qt6 v5.1.2

Amelia is a local-first Qt6/C++ desktop assistant for WRCP, Linux, Kubernetes, debugging and operational documentation. It keeps prompts local, talks to a local Ollama server, stores state on disk, and can optionally perform sanitized external search.

## What changed in v5.1.2

This version focuses on the exact failure mode we saw with oversized document-generation prompts.

### Prompt budgeting

Amelia now applies a stricter prompt budget before calling Ollama:

- tighter local-context budget
- tighter external-context budget
- tighter memory/session-summary budget
- tighter recent-history budget
- diagnostics that show prompt-envelope size before sending the request

The goal is to avoid sending one giant prompt envelope that stalls local generation or times out before the first token.

### Outline-only first pass

For structured document requests such as:

- `MOP`
- `runbook`
- `playbook`
- `guide`
- `markdown`
- `.md`

Amelia now switches to an **outline-only first pass**.

Instead of trying to generate the entire final document immediately, it asks the local model to return only:

- assumptions
- prerequisites
- deployment phases
- validation gates
- rollback points
- appendix items

This keeps the first generation much lighter and more reliable.

### Explicit Ollama `num_ctx`

Amelia now sends an explicit `options.num_ctx` to Ollama.

Default in this build:

- `ollamaNumCtx = 32768`

You can override it in `~/amelia_qt6/config.json`.

### New defaults

The compiled defaults now match the safer runtime profile we discussed:

```json
{
  "maxLocalHits": 3,
  "maxExternalHits": 2,
  "maxHistoryTurns": 4,
  "externalSearchTimeoutMs": 15000,
  "ollamaProbeTimeoutMs": 10000,
  "ollamaResponseHeadersTimeoutMs": 180000,
  "ollamaFirstTokenTimeoutMs": 600000,
  "ollamaInactivityTimeoutMs": 300000,
  "ollamaTotalTimeoutMs": 0,
  "preferOutlinePlanning": true,
  "enableExternalSearch": false
}
```

Notes:

- external search is now **off by default**
- if `externalSearchDomainAllowlist` is empty, all domains are allowed once external search is enabled

### UI improvements

This build also adds:

- Amelia SVG logo in the main title row
- `Help -> About Amelia`
- `Help -> About Qt`
- window icon from the bundled SVG resource

### Desktop installation

This version installs a `.desktop` entry and icon with:

```bash
cmake --install .
```

Installed assets:

- desktop entry: `${CMAKE_INSTALL_PREFIX}/share/applications/amelia_qt6.desktop`
- icon: `${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps/amelia_qt6.svg`

## Persistence layout

Runtime data lives in:

- `${HOME}/.amelia_qt6`

Typical files and folders:

- `${HOME}/.amelia_qt6/conversations/`
- `${HOME}/.amelia_qt6/conversations_index.json`
- `${HOME}/.amelia_qt6/memories.json`
- `${HOME}/.amelia_qt6/state.json`
- `${HOME}/.amelia_qt6/rag_cache.json`
- `${HOME}/.amelia_qt6/knowledge/`

Configuration is searched in this preferred location first:

- `~/amelia_qt6/config.json`

If that file does not exist, Amelia falls back to local project-relative config paths.

## Quick setup

Create the preferred config directory and copy the example config:

```bash
mkdir -p ~/amelia_qt6
cp config/config.example.json ~/amelia_qt6/config.json
```

## Ollama

Amelia expects a local Ollama API on:

- `http://127.0.0.1:11434`

Example:

```bash
ollama pull qwen2.5-coder:7b
curl http://127.0.0.1:11434/api/tags
```

If `/api/tags` responds, Amelia should be able to probe the backend and list models.

The default explicit context value is now:

- `ollamaNumCtx: 32768`

## External search

External search is **disabled by default** in v5.1.2.

When enabled, Amelia expects a SearXNG-compatible endpoint that accepts:

- `q=<query>`
- `format=json`

Example local endpoint:

- `http://127.0.0.1:8080/search`

If `externalSearchDomainAllowlist` is empty, Amelia allows all domains.

## Knowledge base and ingestion

Import content using the UI buttons:

- `Import files`
- `Import folder`
- `Reindex docs`

Supported inputs include:

- PDF (via `pdftotext`)
- Markdown / text
- logs
- YAML / JSON / config-like files
- code files

Knowledge is stored under:

- `${HOME}/.amelia_qt6/knowledge`

## Build

Typical Qt6/CMake flow:

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j
cmake --install .
```

## Notes

- This project is still intentionally local-first and dependency-light.
- The semantic retrieval remains a pragmatic offline implementation rather than a heavyweight neural embedding service.
- `QdrantClient` remains a future extension point.
- PDF extraction depends on `pdftotext` being available in the system.
- The SVG logo is bundled as a Qt resource and also installed for the desktop entry.
