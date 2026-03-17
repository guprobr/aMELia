# Amelia Qt6 v6.5

Amelia is a local offline Qt6/C++ coding and cloud assistant. It talks to a local Ollama server, keeps state on disk under `~/.amelia_qt6`, and can optionally use sanitized external search.

This v6.5 build upgrades the main answer surface and Prompt Lab instead of treating them like raw text boxes.

## What changed in v6.5

### Richer formatted transcript output

The main transcript is now rendered as structured rich text cards instead of a plain colored stream.

It now gives you:

- better spacing and readability for long answers
- markdown-style paragraphs, lists, headings, and inline code rendered more cleanly
- fenced code blocks rendered in dedicated code panels
- clearer separation between `USER`, `ASSISTANT`, and `SYSTEM`
- improved transcript restore behavior for multiline answers and code-heavy replies

### Copy helpers for answers and code blocks

The transcript toolbar now includes:

- **Copy last answer**
- **Copy transcript**
- **Copy code block** from a selector populated with the detected fenced blocks in the conversation output

There is also a transcript context menu with the same copy actions.

This makes it much easier to:

- grab the whole assistant answer quickly
- copy only one generated code block without manual selection
- preserve formatting while reviewing long technical replies

### Prompt Lab improvements

Prompt Lab in v6.5 is more practical for real KB and patch workflows.

It now adds:

- more presets:
  - `General grounding`
  - `Code patch`
  - `Runbook / docs`
  - `Incident investigation`
  - `Dataset from assets`
  - `Executive summary`
  - `Knowledge extraction`
  - `KB-only analysis`
  - `Migration / refactor plan`
- a dedicated multiline area for **filesystem assets to import**
- **Select files** and **Select folder** helpers
- a filterable list of **already indexed knowledge-base assets**
- the ability to add selected KB assets directly into the recipe
- a dedicated field for **manual KB references** that are already in the knowledge base
- a **Copy recipe** action in addition to **Use in input**

This means Prompt Lab can now work with both:

- assets that still need importing
- assets that already exist in Amelia's indexed knowledge base

### Versioning visible in the UI

Version `6.5` now appears in:

- the main window title bar
- the main header label
- the **About Amelia** dialog
- the CMake project version and application version metadata

## Grounding and safety behavior

Amelia still follows the grounding-first behavior from earlier builds.

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
- PDF ingestion still depends on `pdftotext` being available on the system.
- This build is aimed at making long technical answers easier to read and easier to copy back into real patch workflows.
