# aMELia Qt6 v10.1.2

Amelia is a local-first Qt6/C++ coding and cloud assistant that talks to a local Ollama server, stores its state under `~/.amelia_qt6`, indexes a local knowledge base, and can optionally use sanitized external web search through SearXNG.
aMELia is also allegorically considered a MEL: Model Enhancement Lab.

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
  qt6-pdf-dev \
  qt6-multimedia-dev \
  qt6-webengine-dev \
  libtesseract-dev \
  libleptonica-dev \
  libzip-dev \
  tesseract-ocr-eng \
  tesseract-ocr-por \
  curl \
  git
```

Why these matter:

- `qt6-base-dev` -> Qt Core / Widgets / Network / Concurrent / tray integration
- `qt6-tools-dev` and `qt6-tools-dev-tools` -> standard Qt6 dev tooling on Ubuntu
- `qt6-svg-dev` / `qt6-imageformats-plugins` -> SVG logo rendering and runtime image support
- `qt6-pdf-dev` -> `QPdfDocument`/`QPdfSelection`, which Amelia links directly to extract PDF text and rasterize pages for OCR (no `pdftotext`/`pdftoppm` subprocess anymore)
- `qt6-multimedia-dev` -> `QSoundEffect`, used to play the bundled answer-started/answer-completed notification chimes
- `qt6-webengine-dev` -> `QWebEngineView`/`QWebEnginePage`, which render the transcript (including KaTeX-typeset math) and drive PDF export via `printToPdf()`
- `libtesseract-dev` / `libleptonica-dev` -> libtesseract's C++ API, which Amelia links directly for in-process OCR (no `tesseract` CLI subprocess anymore)
- `libzip-dev` -> reads `.docx` files (a zip archive of WordprocessingML XML parts) directly in-process; paired with Qt's own `QXmlStreamReader` to parse `word/document.xml`, so no LibreOffice/pandoc/antiword subprocess is needed
- `tesseract-ocr-eng` -> the English `eng.traineddata` language data libtesseract loads at runtime; without it OCR silently stays disabled
- `tesseract-ocr-por` -> optional Portuguese `por.traineddata`; Amelia tries `eng+por` first and falls back to `eng`-only if this package isn't installed, so it's not required, only recommended for Portuguese-language documents
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

## What changed in v10.1.2

- **LaTeX math rendering in the transcript**: migrated the transcript from `QTextBrowser` to `QWebEngineView`, rendering math with KaTeX (vendored from the Ubuntu `libjs-katex`/`fonts-katex` packages, MIT-licensed) instead of showing `$$...$$`, `\(...\)`, `\[...\]` as raw LaTeX text. Single-dollar `$...$` inline math is also supported, using Pandoc's heuristic to tell real math apart from currency (`$20,000 and $30,000` stays plain text; `$a \neq 0$` renders)
- fixed a transcript-sanitizer bug where LaTeX commands starting with `\t`/`\n`/`\r` (`\text`, `\theta`, `\to`, `\neq`, `\right`, ...) had that prefix silently eaten as a stray layout-escape before ever reaching the renderer
- math spans are extracted before Qt's Markdown-to-HTML pass and reinserted as literal KaTeX targets afterward, so CommonMark's emphasis parsing can no longer mistake underscores inside math (e.g. `\sum_{i=1}^{n} x_i`) for `_italic_`
- fixed a longstanding bug (predating this release, in `ensureBlankLineBeforeFence`/`ensureBlankLineAfterFence`) where every fenced code block in the transcript rendered a literal `\n\n` right before its closing fence, caused by a `QRegularExpression::replace()` replacement string using two literal characters (`\`, `n`) instead of a real newline
- **export transcript answers to PDF**: a "Convert to PDF" link next to "Copy Answer" on every assistant turn exports that single answer; a selection checkbox on each answer plus a "Convert Selected to PDF" toolbar button combine multiple chosen answers into one PDF, typeset (including math) via the same KaTeX pipeline as the live transcript
- reorganized the chat toolbar's buttons into two grouped rows so labels stop getting visually truncated as more actions were added

## What changed in v10.1.1

- **internal modularization**: `ChatController`, `RagIndexer`, and `MainWindow` were the three largest files in the codebase, each mixing many responsibilities in one class. Split into focused, independently-compilable modules (prompt/context budgeting, OCR, DOCX/PDF extraction, semantic chunking, Knowledge Base manifest, lexical scoring, document outline extraction, markdown rendering, and more) with no behavior change, verified by a full rebuild at every step:

  | File | Before | After | Reduction |
  |---|---:|---:|---:|
  | `ragindexer.cpp` | 5,073 | 2,837 | -44.1% |
  | `mainwindow.cpp` | 4,087 | 3,250 | -20.5% |
  | `chatcontroller.cpp` | 3,384 | 2,830 | -16.4% |

- **hardened external search against prompt injection**: SearXNG results are now wrapped in explicit untrusted-data markers, and the system prompt tells the model to never treat fetched web content as instructions
- **OCR tries Portuguese + English**: falls back to English-only automatically if `tesseract-ocr-por` isn't installed
- **removed `ToolExecutor`**: dead code, instantiated but never wired into any functional path

## What changed in v10.1.0

- **auto-continue past the context limit**: Ollama's `num_predict` is intentionally left uncapped, so the only thing that could previously cut an answer short was `num_ctx` filling up mid-response (`done_reason: "length"`). `OllamaClient` now exposes `done_reason` (it was already parsed but never surfaced), and `ChatController` uses it to automatically re-send a follow-up request — same grounding context, plus the tail of what's already been written, with an explicit "continue exactly where you left off" instruction — appending seamlessly to the same streamed answer instead of surfacing a hard-truncated one. Capped at 6 rounds to avoid a runaway loop; a natural stop (matched `<END>` / any `done_reason` other than `"length"`) ends the loop immediately
- **prompt-eval ETA countdown**: Ollama has no API for "% done processing the prompt," so the "waiting for first token" phase used to just show a bare indeterminate spinner. `OllamaClient` now also parses `prompt_eval_count`/`prompt_eval_duration` from each finished response, `ChatController` keeps a rolling (EMA) estimate of this backend's prompt-eval throughput, and the progress bar shows a live "~Ns remaining" countdown instead once at least one real sample exists — falling back to the old spinner on the very first request each session, and to a "taking longer than expected" label if a request runs past its estimate
- **Memory tab: Edit selected**: saved memories could previously only be added or deleted, never corrected. `MemoryManager::updateMemoryById()` upserts an existing record in place (by id, preserving its key/category/createdAt) via the storage layer's existing upsert-by-id path, exposed through a new "✏️ Edit selected" button that opens a small dialog to change the memory's text and pinned status
- **About dialog**: replaced the old run-on sentence of UI conveniences with a bullet list of the actual "hardcore" technical capabilities (native in-process PDF/DOCX extraction, hybrid RAG with reranking, TOC-aware document-study mode, the outline planner, grounding guardrails, auto-continue, adaptive context budgeting)

## What changed in v10.0.1

- **native `.docx` ingestion**: Word documents are now indexed in-process, the same way PDFs are — `libzip` unpacks the `.docx` archive and Qt's own `QXmlStreamReader` parses `word/document.xml` directly. No LibreOffice/pandoc/antiword subprocess. Legacy binary `.doc` is not supported. Sources are tagged `docx:xml-native` in the Knowledge Base inventory; a failed extraction is tagged `docx:load-failed`. Requires `libzip-dev` to build (see the updated package list below)
- **answer notification chimes**: Amelia now plays a short sound when the assistant's visible answer starts streaming, and another when it finishes, via Qt Multimedia (`QSoundEffect`) so it behaves the same on Linux, Windows, and macOS. Controlled by the `enableNotificationSounds` config key (or `AMELIA_ENABLE_NOTIFICATION_SOUNDS` env override), on by default. Bundled chimes are extracted from Qt resources to a real file on disk before playback, working around a Qt Multimedia FFmpeg-backend limitation where `QSoundEffect` can't decode sources referenced by a `qrc:` URL
- **document-study outline-planner fix**: prompts that look like a "teach me this document" / full-TOC-coverage request now skip the outline planner's generic MOP/runbook/guide template entirely, so they always get the TOC-aware `DOCUMENT_OUTLINE_MAP`/`SECTION_COVERAGE_PACKET` retrieval path with its adaptive, document-size-aware context budget instead of being misclassified into a fixed 4800-char "outline-only first pass" (which could trigger just from boilerplate text like a Prompt Lab preset name containing the word "runbook")

## What changed in v10.0.0

- **native PDF/OCR pipeline**: replaced the `pdftotext`/`pdftoppm`/`tesseract` `QProcess` subprocesses with `Qt6::Pdf` (`QPdfDocument`/`QPdfSelection`) for text extraction and page rasterization, and libtesseract's C++ API (`TessBaseAPI`) for OCR, linked directly into the binary. No more external CLI tools, no `QProcess`, no OCR temp files on disk
- fixed a rendering bug found while verifying the migration: `QPdfDocument::render()` returns a transparent-background image (alpha 0, RGB 0,0,0), so converting it straight to grayscale for OCR produced an all-black image and Tesseract would have silently returned empty text on every scanned page. Pages are now composited onto opaque white before grayscale conversion
- each worker thread keeps one lazily-initialized `TessBaseAPI` instance (language data loaded once, not once per page), with Tesseract's internal layout/diacritic diagnostics redirected away from stderr instead of spamming the console on every OCR'd page
- sources whose PDF extractor used OCR are now tagged in the Knowledge Base inventory as `pdf:qtpdf-paged+ocr(Np)`; a plain load failure is tagged `pdf:load-failed`
- requires `qt6-pdf-dev`, `libtesseract-dev`, `libleptonica-dev` to build and the `tesseract-ocr-eng` language-data package at runtime (see the updated package list below); `poppler-utils` and `tesseract-ocr` (the CLI tools) are no longer required at all
- **cross-platform build fixes**: `Qt6::DBus` (used only for freedesktop desktop-notification integration, which already had a `QSystemTrayIcon` fallback) is no longer a hard-required component — it's only requested and linked on Linux/BSD, where mainstream Windows/macOS Qt distributions don't ship it anyway. Tesseract/Leptonica discovery now falls back from pkg-config to each library's own CMake `CONFIG` package (e.g. vcpkg) when pkg-config isn't available. The `.desktop` launcher and hicolor icon `install()` rules, which are freedesktop/XDG-only concepts, are now gated to Unix-only installs

### Reindex note

- PDF sources need a manual Knowledge Base reindex to pick up the new extractor: the cache-staleness check only compares file mtime/size, not extractor logic, so unchanged PDFs on disk keep serving their old `pdftotext`-era cached chunks until you trigger a reindex yourself

## What changed in v9.19.9

- PDF ingestion and OCR are now fully native: Amelia no longer shells out to `pdftotext`, `pdftoppm`, or the `tesseract` CLI. It links `Qt6::Pdf` (`QPdfDocument`/`QPdfSelection`) directly for text extraction and page rasterization, and libtesseract's C++ API directly for OCR — no external binaries, no `QProcess`, no temp files on disk for OCR pages
- any page whose extracted text comes back near-blank (fewer than 6 words) is re-rendered in-process at 300dpi as a grayscale `QImage` and fed straight into a `TessBaseAPI` instance; the OCR'd text is spliced back into the page in place of the near-blank original if it recovered more words. This targets scanned handbook pages, photographed diagrams, and CLI screenshots embedded as images, which previously indexed as silently empty gaps
- OCR is per-page and gated on word count, so born-digital pages (the common case) never pay the OCR cost — only pages that actually look image-only get rendered and OCR'd
- each worker thread keeps one lazily-initialized `TessBaseAPI` instance (language data is loaded once, not once per page), and OCR results are cached per page index within a single extraction pass so a given page is never re-OCR'd twice
- OCR availability is checked once by attempting to initialize the Tesseract engine with `eng+por`, falling back to `eng`-only if `por.traineddata` isn't installed; if `eng.traineddata` isn't installed either, Amelia detects that once and skips OCR silently, falling back to plain text extraction
- sources whose PDF extractor used OCR are now tagged in the Knowledge Base inventory as `pdf:qtpdf-paged+ocr(Np)`, so you can see which files benefited
- requires `qt6-pdf-dev`, `libtesseract-dev`, `libleptonica-dev` to build and the `tesseract-ocr-eng` language-data package at runtime (see the updated package list below); `poppler-utils` and `tesseract-ocr` (the CLI tools) are no longer required at all

- replaces the regex/line-shape heuristics that decided chunk boundaries (`isProceduralLeadLine`, `isStructuredCodeLikeLine`, etc.) with an embedding-similarity-driven merge: atomic blocks are now folded into a chunk as long as they stay semantically on-topic, and a boundary is only cut once the running chunk has reached a reasonable size *and* the next block's embedding has drifted away (cosine similarity) from the chunk built so far
- this directly targets the recurring "gap" failure mode in technical handbooks, where a numbered procedure step, its command block, and its output got separated because a char-count threshold happened to fall between them even though they were clearly one unit — chunk boundaries now follow meaning instead of guessing from line patterns
- works with or without a configured neural embedding backend: it reuses the same `EmbeddingClient::embedTexts()` path already used for retrieval, which transparently falls back to the existing local hash-based embedding when no Ollama embedding model is configured, so even lexical-only setups get real similarity-driven boundaries instead of pure char-count splitting
- chunk embeddings are now derived as the sum of their member blocks' embeddings (computed once, during the boundary decision) instead of being re-embedded a second time from the final joined chunk text, which avoids doubling embedding calls and avoids a second silent truncation of long chunks by the embedding input cap
- bumps the on-disk chunking-strategy cache stamp so existing Knowledge Base collections automatically rebuild their chunks on the next reindex

- adds a repetition-loop guard for the *visible* assistant answer stream: if the same normalized line repeats 5 times in a row (e.g. a local model getting stuck regenerating an identical markdown table row), Amelia now stops generation early, trims the repeated tail down to a single instance, and appends a short note explaining the answer was cut short. This is separate from — and did not previously exist alongside — the older loop guard, which only ever watched the hidden pre-answer `<think>` stream and explicitly stood down the moment real output began
- the stop is deferred with `QTimer::singleShot(0, ...)` so it never races with the network reply's own callback stack, the same safe pattern the existing runner-failure retry already used

- adds `ollamaNumThread` (default `0` = auto) to override Ollama's CPU thread count per request, for hybrid P/E-core CPUs where auto-detection isn't optimal
- adds `ollamaKeepAlive` (default `"10m"`, up from Ollama's stock `5m`) sent on every chat request so a model stays resident between prompts instead of reloading from disk after a short idle gap
- adds `ollamaEmbeddingForceCpu` (default `false`), which adds `num_gpu: 0` to embedding requests only — lets you keep GPU acceleration for chat generation while working around embedding-model crashes on early/beta GPU backends (e.g. an IPEX-LLM SYCL backend segfaulting specifically on `embeddinggemma`) without disabling neural embeddings altogether

- every button across the main window, Prompt Lab, Knowledge Base, and Memory tabs now has a semantically matched emoji prefix (🗑️ delete/remove, 📌 pin, 🔄 reindex, 🧠 reasoning trace, 🧹 clear, ✨ compose, etc.), including the two dynamic ON/OFF diagnostic toggle buttons

### Reindex note

- reindex your Knowledge Base once after upgrading to v9.19.9 to rebuild chunks with the new boundary logic; the cache-version bump means this happens automatically the next time indexing runs

## What changed in v9.19.8

- fixes the `mainwindow.cpp` streamed-assistant compile regression by passing the active palette into the new palette-aware transcript color helpers
- improves final transcript sanitization so stray literal `\n`, `\n\n`, `\t`, and fence-adjacent escaped layout tokens render as real spacing instead of leaking into the visible answer
- preserves quoted string escapes inside code blocks and inline code while normalizing display-only escaped layout outside those quoted regions

## What changed in v9.19.7

- removes most fixed widget/label colors and reworks the transcript, diagnostics, and toast rendering to derive colors from the active Qt/system palette instead of assuming a dark theme
- rebuilds transcript and diagnostics rendering on palette/style changes so Amelia follows light/dark or accent changes more naturally while running
- broadens external search snippet parsing to accept more result fields (`content`, `snippet`, `description`, `text`, `summary`, and `descriptions[]`) and keeps longer sanitized excerpts
- improves large-document exact extraction by preferring near-fit full-file coverage more often, adding intrinsic actionability scoring for commands/YAML/config/procedure chunks, and widening raw-window sampling across long files
- improves hit excerpts so matches can show both the first and later matching regions of the same chunk instead of truncating too aggressively around the first hit

## What changed in v9.19.6

- fixes the `ragindexer.cpp` build break caused by malformed multiline `QStringLiteral(...)` insertion in the procedural-lead helpers
- rewrites those helpers to use valid single-literal regex construction and proper `QLatin1Char('\n')` splitting
- preserves the earlier exact-extraction and chunk-boundary behavior without requiring further logic changes

## What changed in v9.19.5

- fixed the document-study `num_ctx` reserve calculation so large grounded requests no longer fall back to the old `safeNumCtx / 8` floor in common 32768-context setups
- added an exact-extraction retrieval mode for scraper-style prompts such as `extract all`, `gather all actionable snippets`, `preserve YAML`, and similar exhaustive requests
- exact-extraction mode emits ordered raw chunk windows from the selected file, biased toward actionable hits plus evenly spaced spans across the document, instead of relying only on outline/section summaries
- improved semantic chunk building so a procedural lead like `4. Run ...:` stays attached to the following command/config block instead of being split just because the next line looks code-like
- softened PDF page-break boundaries so `[[PAGE N]]` markers no longer force a mid-procedure semantic split by default
- improved section preview stitching so procedure headers can pull in more following chunks before balanced trimming, reducing missing command lines after page breaks
- Knowledge Base controls remain locked while prompt generation or indexing is active

### Reindex note

- the new exact-extraction retrieval path is a runtime-only change and works immediately after upgrade
- the semantic block/page-break fixes improve how new chunks are built, so reindex once after upgrading if you want existing cached documents to benefit from the better chunk boundaries

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

If your machine is CPU-only in practice, or if `ollama ps` shows the generation model staying on `100% CPU`, smaller reasoning-capable alternatives such as `qwen3:8b` or `deepseek-r1:8b` are often a better fit for document-study mode than forcing very large grounded prompts through a 20B model on CPU.

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

### Large document study / big PDFs

For document-study prompts against very large manuals or PDFs, Amelia v9.16.2 now does more than scale budgets from source size. It also derives a safe retrieved-context budget from Ollama's configured `num_ctx`, then applies that cap all the way through document-packet assembly. This avoids the previous failure mode where ChatController computed a reasonable target but the packet formatter quietly expanded it again for very large books.

The effective policy is now:

- estimate document size from indexed characters and chunk counts
- compute a safe retrieved-context ceiling from `ollamaNumCtx`
- keep a reserve for system/developer text, history, and the model's answer
- scale representative coverage and section sweep density to the available budget
- hard-trim each document-study packet so the formatter cannot outgrow the runtime budget
- preserve both the beginning and end of oversized prompt sections instead of left-trimming away the tail of the document
- supplement heading-based section anchors with evenly distributed document spans so late sections and appendixes still receive explicit coverage even when heading extraction is sparse

By default Amelia uses an `auto` runtime profile for these limits. If your Ollama setup is CPU-only or unstable under heavy loads, you can force a more conservative policy with:

```bash
export AMELIA_OLLAMA_RUNTIME_PROFILE=cpu
```

If your Ollama runtime is genuinely stable on GPU and you want Amelia to be a little less conservative:

```bash
export AMELIA_OLLAMA_RUNTIME_PROFILE=gpu
```

Recommended Ollama-side tuning for large document prompts:

```bash
OLLAMA_CONTEXT_LENGTH=65536 OLLAMA_NUM_PARALLEL=1 OLLAMA_MAX_LOADED_MODELS=1 OLLAMA_FLASH_ATTENTION=1 OLLAMA_KV_CACHE_TYPE=q8_0 OLLAMA_KEEP_ALIVE=30m ollama serve
```

For CPU-only systems or for Windows hosts where Vulkan is unstable or unavailable, Amelia usually behaves best with:

```bash
export AMELIA_OLLAMA_RUNTIME_PROFILE=cpu
```

For GPU-backed systems where `ollama ps` confirms the chat model is actually offloaded to GPU, you can let Amelia spend a little more of `num_ctx` on retrieved context:

```bash
export AMELIA_OLLAMA_RUNTIME_PROFILE=gpu
```

Notes:

- If Ollama accepts a large grounded request but later returns `model runner has unexpectedly stopped`, Amelia v9.17.5 retries once with `think=false`, a lower request `num_ctx`, and a smaller balanced local-context packet. This fallback is generic and applies to any large document-heavy request; it does not hardcode subject-specific knowledge.
- `OLLAMA_CONTEXT_LENGTH` is the main capacity knob for large grounded prompts.
- `OLLAMA_NUM_PARALLEL=1` is important for big prompts because parallel request handling multiplies KV/context memory pressure.
- `OLLAMA_MAX_LOADED_MODELS=1` keeps other models from competing for VRAM or RAM while a large prompt is running.
- `OLLAMA_FLASH_ATTENTION=1` reduces memory pressure at larger context sizes on supported backends.
- `OLLAMA_KV_CACHE_TYPE=q8_0` is a good first compromise when you need more room. If you are desperate for headroom, `q4_0` saves more memory but may reduce answer fidelity.
- `OLLAMA_KEEP_ALIVE` helps amortize reload cost, but it does not increase prompt capacity.
- `AMELIA_OLLAMA_RUNTIME_PROFILE=cpu` tells Amelia to spend a smaller fraction of `num_ctx` on retrieved context, which is usually the safer choice when `ollama ps` shows the chat model on CPU.
- `AMELIA_OLLAMA_RUNTIME_PROFILE=gpu` lets Amelia be less conservative only when Ollama is truly GPU-backed.

For truly massive corpora (for example, thousands of pages), no single prompt budget is enough to preserve the entire source verbatim. The correct approach is hierarchical coverage: outline extraction, representative section sweeps, and grounded answer synthesis over staged context packets. Amelia now leans further in that direction automatically.

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

PDF text extraction is built into the binary (`Qt6::Pdf`), so there is no external tool to check on `PATH`. If a PDF fails to extract, check the Knowledge Base inventory for its `pdf:load-failed` tag — this means the document is encrypted, corrupt, or otherwise unsupported by `QPdfDocument`.

### Scanned/image-only PDF pages are not OCR'd

OCR requires the `eng.traineddata` language file. Confirm it is installed:

```bash
dpkg -L tesseract-ocr-eng | grep traineddata
```

If not:

```bash
sudo apt install tesseract-ocr-eng
```

For better OCR quality on Portuguese-language documents, also install `tesseract-ocr-por` (`por.traineddata`); Amelia uses it automatically when present and falls back to `eng`-only otherwise.

### .docx files do not index

DOCX text extraction is also built into the binary (`libzip` + `QXmlStreamReader` over the `word/document.xml` part), no external tool required. If a `.docx` fails to extract, check the Knowledge Base inventory for its `docx:load-failed` tag — this means the file is corrupt, password-protected, or not actually a valid Word document (`.doc`, the legacy binary format, is not supported).

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
