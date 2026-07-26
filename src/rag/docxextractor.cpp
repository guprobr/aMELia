#include "rag/docxextractor.h"

#include "rag/textcleanup.h"

#include <QXmlStreamReader>

#include <zip.h>

namespace {
// Walks the WordprocessingML body (word/document.xml, already unzipped) and
// linearizes it back to plain text: <w:t> runs are concatenated, <w:tab/>
// becomes a literal tab, <w:br type="page"/> becomes a form-feed (matching
// the page-break marker the PDF path uses), other <w:br>/<w:cr> become a
// newline, and each paragraph/table row ends with a blank line so downstream
// heading/procedure detection still sees paragraph boundaries.
QString extractDocxParagraphs(const QByteArray &documentXml)
{
    QXmlStreamReader reader(documentXml);
    QString result;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView name = reader.name();
            if (name == QStringLiteral("t")) {
                result += reader.readElementText(QXmlStreamReader::IncludeChildElements);
            } else if (name == QStringLiteral("tab")) {
                result += QLatin1Char('\t');
            } else if (name == QStringLiteral("br") || name == QStringLiteral("cr")) {
                const QString breakType = reader.attributes().value(QStringLiteral("type")).toString();
                result += (breakType == QStringLiteral("page")) ? QStringLiteral("\f") : QStringLiteral("\n");
            }
        } else if (token == QXmlStreamReader::EndElement) {
            const QStringView name = reader.name();
            if (name == QStringLiteral("p") || name == QStringLiteral("tr")) {
                result += QStringLiteral("\n\n");
            }
        }
    }
    return result;
}
}

// .docx is a zip archive; word/document.xml holds the document body. Both
// the archive and the XML are read in-process (libzip + QXmlStreamReader),
// the same "no external subprocess" approach the PDF/OCR path takes.
bool readDocxFile(const QString &path, QString *text, QString *extractor)
{
    const QByteArray pathUtf8 = path.toUtf8();
    zip_t *archive = zip_open(pathUtf8.constData(), ZIP_RDONLY, nullptr);
    if (archive == nullptr) {
        if (extractor != nullptr) {
            *extractor = QStringLiteral("docx:load-failed");
        }
        return false;
    }

    const zip_int64_t index = zip_name_locate(archive, "word/document.xml", 0);
    zip_stat_t entryStat;
    zip_stat_init(&entryStat);
    if (index < 0 || zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &entryStat) != 0
            || (entryStat.valid & ZIP_STAT_SIZE) == 0) {
        zip_close(archive);
        if (extractor != nullptr) {
            *extractor = QStringLiteral("docx:load-failed");
        }
        return false;
    }

    zip_file_t *entry = zip_fopen_index(archive, static_cast<zip_uint64_t>(index), 0);
    if (entry == nullptr) {
        zip_close(archive);
        if (extractor != nullptr) {
            *extractor = QStringLiteral("docx:load-failed");
        }
        return false;
    }

    QByteArray documentXml;
    documentXml.resize(static_cast<qsizetype>(entryStat.size));
    const zip_int64_t bytesRead = zip_fread(entry, documentXml.data(), entryStat.size);
    zip_fclose(entry);
    zip_close(archive);

    if (bytesRead < 0 || static_cast<zip_uint64_t>(bytesRead) != entryStat.size) {
        if (extractor != nullptr) {
            *extractor = QStringLiteral("docx:load-failed");
        }
        return false;
    }

    if (text != nullptr) {
        *text = cleanedText(extractDocxParagraphs(documentXml));
    }
    if (extractor != nullptr) {
        *extractor = QStringLiteral("docx:xml-native");
    }
    return true;
}
