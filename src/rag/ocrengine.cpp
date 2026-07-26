#include "rag/ocrengine.h"

#include "rag/cancellation.h"
#include "rag/textmetrics.h"

#include <QImage>
#include <QPainter>
#include <QPdfDocument>
#include <QStringList>

#include <tesseract/baseapi.h>
#include <tesseract/ocrclass.h>

namespace {
// One TessBaseAPI per worker thread: Init() loads language data from disk,
// so it's cached lazily per-thread rather than paid on every page.
tesseract::TessBaseAPI *threadLocalOcrEngine()
{
    thread_local tesseract::TessBaseAPI engine;
    thread_local bool initialized = false;
    thread_local bool ready = false;
    if (!initialized) {
        initialized = true;
        // Prefer eng+por (better OCR quality on Portuguese-accented text) but fall
        // back to eng-only, since tesseract-ocr-por is an optional package and Init()
        // fails outright if any requested language's traineddata is missing.
        ready = engine.Init(nullptr, "eng+por") == 0;
        if (!ready) {
            ready = engine.Init(nullptr, "eng") == 0;
        }
        if (ready) {
            engine.SetPageSegMode(tesseract::PSM_AUTO);
            // Tesseract writes layout/diacritic diagnostics straight to stderr
            // by default; redirect that debug stream instead of spamming the
            // app's console on every OCR'd page.
#ifdef Q_OS_WIN
            engine.SetVariable("debug_file", "NUL");
#else
            engine.SetVariable("debug_file", "/dev/null");
#endif
        }
    }
    return ready ? &engine : nullptr;
}

bool ocrToolsAvailable()
{
    return threadLocalOcrEngine() != nullptr;
}

bool tessCancelCallback(void *cancelThis, int)
{
    return isCancelRequested(static_cast<const std::atomic_bool *>(cancelThis));
}

// Renders a single PDF page to a grayscale image via QPdfDocument and runs
// libtesseract on it in-process. Used only for pages whose embedded text
// layer comes back near-empty (i.e. likely a scanned image or a screenshot),
// so the common case of born-digital PDFs never pays this cost.
QString ocrPageText(QPdfDocument *document, int pageIndex, std::atomic_bool *cancelRequested)
{
    tesseract::TessBaseAPI *engine = threadLocalOcrEngine();
    if (engine == nullptr || isCancelRequested(cancelRequested)) {
        return QString();
    }

    const QSizeF pointSize = document->pagePointSize(pageIndex);
    if (pointSize.width() <= 0 || pointSize.height() <= 0) {
        return QString();
    }

    constexpr qreal kRenderDpi = 300.0;
    const QSize pixelSize(qMax(1, qRound(pointSize.width() * kRenderDpi / 72.0)),
                          qMax(1, qRound(pointSize.height() * kRenderDpi / 72.0)));

    QImage image = document->render(pageIndex, pixelSize, QPdfDocumentRenderOptions());
    if (image.isNull() || isCancelRequested(cancelRequested)) {
        return QString();
    }

    // QPdfDocument::render() paints onto a transparent canvas (background
    // alpha 0, RGB 0,0,0), so converting straight to grayscale would read
    // every pixel — background included — as black. Composite onto opaque
    // white first so the page renders as normal black-on-white for OCR.
    QImage opaque(image.size(), QImage::Format_RGB32);
    opaque.fill(Qt::white);
    {
        QPainter compositor(&opaque);
        compositor.drawImage(0, 0, image);
    }
    image = opaque.convertToFormat(QImage::Format_Grayscale8);

    engine->SetImage(image.constBits(), image.width(), image.height(), 1,
                     static_cast<int>(image.bytesPerLine()));

    tesseract::ETEXT_DESC monitor;
    monitor.cancel = tessCancelCallback;
    monitor.cancel_this = cancelRequested;

    if (engine->Recognize(&monitor) != 0) {
        engine->Clear();
        return QString();
    }

    char *rawText = engine->GetUTF8Text();
    const QString result = rawText != nullptr ? QString::fromUtf8(rawText) : QString();
    delete[] rawText;
    engine->Clear();
    return result;
}
}

QString ocrAugmentLowContentPages(QPdfDocument *document,
                                  const QString &rawExtractedText,
                                  std::atomic_bool *cancelRequested,
                                  QHash<int, QString> *ocrCache,
                                  int *pagesOcrApplied)
{
    if (!ocrToolsAvailable()) {
        return rawExtractedText;
    }

    constexpr int kOcrCandidateWordThreshold = 6;
    constexpr int kMaxPagesToOcrPerFile = 150;

    const QStringList rawPages = rawExtractedText.split(QChar('\f'), Qt::KeepEmptyParts);
    if (rawPages.isEmpty()) {
        return rawExtractedText;
    }

    QStringList result = rawPages;
    int applied = 0;
    for (int i = 0; i < rawPages.size(); ++i) {
        if (isCancelRequested(cancelRequested) || applied >= kMaxPagesToOcrPerFile) {
            break;
        }
        const QString &pageText = rawPages.at(i);
        const int wordCount = countWordsInText(pageText);
        if (wordCount >= kOcrCandidateWordThreshold) {
            continue;
        }

        QString ocrText;
        const auto cachedIt = ocrCache->constFind(i);
        if (cachedIt != ocrCache->cend()) {
            ocrText = cachedIt.value();
        } else {
            ocrText = ocrPageText(document, i, cancelRequested).trimmed();
            ocrCache->insert(i, ocrText);
        }
        if (ocrText.isEmpty() || countWordsInText(ocrText) <= wordCount) {
            continue;
        }
        result[i] = ocrText;
        ++applied;
    }

    if (pagesOcrApplied != nullptr) {
        *pagesOcrApplied += applied;
    }
    if (applied == 0) {
        return rawExtractedText;
    }
    return result.join(QChar('\f'));
}
