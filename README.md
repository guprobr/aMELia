# Amelia Qt6 v6.2

Amelia is a local offline Qt6/C++ coding and cloud assistant. It talks to a local Ollama server, keeps state on disk under `~/.amelia_qt6`, and can optionally use sanitized external search.

This v6.2 build keeps the anti-hallucination work from v6.1 and adds visual diagnostics plus a lightweight training-oriented Prompt Lab in the UI.

## What changed in v6.2

### Colored diagnostics

Amelia now emits category-colored diagnostics in two places:

- terminal / console output with ANSI colors
- the in-app **Diagnostics** tab with per-category colors

Diagnostic categories include:

- `backend`
- `search`
- `rag`
- `memory`
- `planner`
- `guardrail`
- `ingest`
- `startup`
- `budget`
- `chat`

To disable terminal colors, set:

```bash
export NO_COLOR=1
```

### Colored transcript

The main conversation transcript is now rendered as rich text with distinct colors for:

- user messages
- assistant messages
- system notices

This makes long local sessions much easier to scan while testing prompts, grounding, and retrieval behavior.

### Prompt Lab training helper tab

A new **Prompt Lab** tab was added to the right-side panel. It is not a full model trainer, but it does make training-style preparation much easier.

Prompt Lab lets you:

- choose a preset such as `Code patch`, `Runbook / docs`, `Incident investigation`, or `Dataset from assets`
- describe a concrete goal
- list asset paths to import into the knowledge base
- add extra notes, schema hints, style constraints, or supervision hints
- generate a reusable grounded prompt recipe
- preview a compact JSONL-style training example
- copy that recipe straight into the main input box
- import the listed assets into Amelia knowledge storage

### Memory manager fixes merged

This build also merges the recent memory fixes:

- fixed regex escaping for platform/release extraction
- restored `scoreMemory(...)` so linking succeeds again

## Grounding and safety behavior

Amelia still follows the v6.1 grounding rules.

For project-scoped prompts such as questions about:

- repository structure
- files, classes, functions, modules
- current app capabilities
- current indexed documentation
- local filesystem or configuration state

Amelia refuses to guess when no supporting context is present and returns:

```text
I don't know based on the provided context.
```

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

## Notes

- Amelia is intentionally local-first.
- External search is disabled by default.
- Prompt Lab helps prepare grounded prompt and JSONL-style samples, but it does not fine-tune models by itself.
- Sample docs may still exist in `docs/sample/`; remove or replace them as needed for your own knowledge base.
- PDF ingestion still depends on `pdftotext` being available on the system.
