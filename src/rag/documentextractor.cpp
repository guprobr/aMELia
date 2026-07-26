#include "rag/documentextractor.h"

#include "rag/cancellation.h"
#include "rag/docxextractor.h"
#include "rag/ocrengine.h"
#include "rag/pdftextcleanup.h"
#include "rag/textcleanup.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPdfDocument>
#include <QPdfSelection>

bool readTextFile(const QString &path, QString *text, QString *extractor, std::atomic_bool *cancelRequested)
{
    QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("pdf")) {
        QPdfDocument document;
        if (document.load(path) != QPdfDocument::Error::None) {
            if (extractor != nullptr) {
                *extractor = isCancelRequested(cancelRequested)
                        ? QStringLiteral("canceled")
                        : QStringLiteral("pdf:load-failed");
            }
            return false;
        }

        const int pageCount = document.pageCount();
        QStringList pages;
        pages.reserve(pageCount);
        for (int page = 0; page < pageCount; ++page) {
            if (isCancelRequested(cancelRequested)) {
                if (extractor != nullptr) {
                    *extractor = QStringLiteral("canceled");
                }
                return false;
            }
            const QPdfSelection selection = document.getAllText(page);
            pages << (selection.isValid() ? selection.text() : QString());
        }

        QString extractedText = pages.join(QChar('\f'));

        QHash<int, QString> ocrCache;
        int ocrPagesApplied = 0;
        extractedText = ocrAugmentLowContentPages(&document, extractedText, cancelRequested, &ocrCache, &ocrPagesApplied);

        QString extractorName = QStringLiteral("pdf:qtpdf-paged");
        if (ocrPagesApplied > 0) {
            extractorName += QStringLiteral("+ocr(%1p)").arg(ocrPagesApplied);
        }
        if (text != nullptr) {
            *text = cleanedPdfText(extractedText, cancelRequested);
        }
        if (extractor != nullptr) {
            *extractor = extractorName;
        }
        return true;
    }

    if (suffix == QStringLiteral("docx")) {
        if (isCancelRequested(cancelRequested)) {
            if (extractor != nullptr) {
                *extractor = QStringLiteral("canceled");
            }
            return false;
        }
        return readDocxFile(path, text, extractor);
    }

    if (isCancelRequested(cancelRequested)) {
        if (extractor != nullptr) {
            *extractor = QStringLiteral("canceled");
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (extractor != nullptr) {
            *extractor = QStringLiteral("read-failed");
        }
        return false;
    }

    QByteArray bytes;
    bytes.reserve(static_cast<int>(qMin<qint64>(info.size(), 4 * 1024 * 1024)));
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(256 * 1024);
        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            file.close();
            if (extractor != nullptr) {
                *extractor = QStringLiteral("read-failed");
            }
            return false;
        }
        bytes.append(chunk);
        if (isCancelRequested(cancelRequested)) {
            file.close();
            if (extractor != nullptr) {
                *extractor = QStringLiteral("canceled");
            }
            return false;
        }
    }
    file.close();

    if (bytes.contains('\0')) {
        if (extractor != nullptr) {
            *extractor = QStringLiteral("binary-skipped");
        }
        return false;
    }

    if (text != nullptr) {
        *text = cleanedText(QString::fromUtf8(bytes));
    }
    if (extractor != nullptr) {
        *extractor = QStringLiteral("direct");
    }
    return true;
}

QString fallbackZeroChunkReason(const QString &extractor, int textCharCount, int wordCount)
{
    const QString normalizedExtractor = extractor.trimmed().toLower();
    if (normalizedExtractor == QStringLiteral("canceled")) {
        return QStringLiteral("Indexing was canceled before this asset finished chunk generation.");
    }
    if (normalizedExtractor == QStringLiteral("binary-skipped")) {
        return QStringLiteral("The file appears to be binary or otherwise unsupported for text chunking.");
    }
    if (normalizedExtractor == QStringLiteral("read-failed")) {
        return QStringLiteral("Amelia could not read the file contents from disk.");
    }
    if (normalizedExtractor == QStringLiteral("pdf:load-failed")) {
        return QStringLiteral("PDF text extraction failed. The document may be encrypted, corrupt, or otherwise unsupported.");
    }
    if (normalizedExtractor.startsWith(QStringLiteral("pdf:")) && textCharCount <= 0) {
        return QStringLiteral("PDF extraction produced no usable text after cleanup. The document may be scanned or image-only.");
    }
    if (normalizedExtractor == QStringLiteral("docx:load-failed")) {
        return QStringLiteral("DOCX text extraction failed. The file may be corrupt, password-protected, or not a valid Word document.");
    }
    if (normalizedExtractor.startsWith(QStringLiteral("docx:")) && textCharCount <= 0) {
        return QStringLiteral("DOCX extraction produced no usable text after cleanup.");
    }
    if (textCharCount <= 0 || wordCount <= 0) {
        return QStringLiteral("Text extraction finished, but no usable text remained after cleanup.");
    }
    return QStringLiteral("The asset was ingested, but no chunk survived filtering or deduplication.");
}
