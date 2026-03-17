# Amelia Qt6 v6.1

Amelia is a local offline Qt6/C++ coding and cloud assistant. It talks to a local Ollama server, keeps state on disk under `~/.amelia_qt6`, and can optionally use sanitized external search.

This v6.1 build focuses on reducing hallucinations in code, infrastructure, and project-specific answers.

## What changed in v6.1

### Anti-hallucination architecture

- switched Ollama calls from flat `/api/generate` prompting to structured `/api/chat`
- separated `system`, `developer`, and `user` messages
- added a strict offline coding/system prompt
- added a hard refusal path for project-scoped questions with no grounded evidence
- removed assistant-history recycling from prompts by default
- stripped `<think>` and `<END>` markers from visible output

### Safer coding defaults

Amelia now uses conservative generation defaults aimed at coding and cloud work:

```json
{
  "ollamaTemperature": 0.15,
  "ollamaTopP": 0.95,
  "ollamaTopK": 50,
  "ollamaRepeatPenalty": 1.12,
  "ollamaPresencePenalty": 0.0,
  "ollamaFrequencyPenalty": 0.0,
  "ollamaStopSequences": ["<END>"]
}
```

### Grounding policy

For project-scoped prompts such as questions about:

- repository structure
- files, classes, functions, modules
- current app capabilities
- current indexed documentation
- local filesystem or configuration state

Amelia now refuses to guess when no supporting context is present and returns:

```text
I don't know based on the provided context.
```

### Timeouts

This build keeps the phase-based timeout model:

- `ollamaProbeTimeoutMs`
- `ollamaResponseHeadersTimeoutMs`
- `ollamaFirstTokenTimeoutMs`
- `ollamaInactivityTimeoutMs`
- `ollamaTotalTimeoutMs`

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

## Example config

The packaged example config is:

- `config/config.example.json`

Preferred user config path:

- `~/.amelia_qt6/config.json`

## Recommended local models

For dependable coding and cloud work:

- `qwen2.5-coder:14b` — recommended default
- `qwen2.5-coder:7b` — lighter fallback

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

## Git default

This v6.1 package is prepared with the repository default branch set to `master`.

If you initialize a new remote manually:

```bash
git init -b master
git add .
git commit -m "Amelia Qt6 v6.1 anti-hallucination patch"
```

## Notes

- Amelia is intentionally local-first.
- External search is disabled by default.
- Sample docs may still exist in `docs/sample/`; remove or replace them as needed for your own knowledge base.
- PDF ingestion still depends on `pdftotext` being available on the system.
