#include "rag/ragindexer.h"

#include "rag/embeddingclient.h"

#include <algorithm>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <utility>

namespace {
QString detectSourceType(const QFileInfo &info)
{
    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("log")) {
        return QStringLiteral("log");
    }
    if (suffix == QStringLiteral("yaml") || suffix == QStringLiteral("yml") || suffix == QStringLiteral("json")
        || suffix == QStringLiteral("xml") || suffix == QStringLiteral("ini") || suffix == QStringLiteral("cfg")
        || suffix == QStringLiteral("conf") || suffix == QStringLiteral("service")) {
        return QStringLiteral("config");
    }
    if (suffix == QStringLiteral("md") || suffix == QStringLiteral("txt") || suffix == QStringLiteral("pdf") || suffix == QStringLiteral("csv")) {
        return QStringLiteral("doc");
    }
    if (suffix == QStringLiteral("cpp") || suffix == QStringLiteral("cxx") || suffix == QStringLiteral("cc")
        || suffix == QStringLiteral("c") || suffix == QStringLiteral("h") || suffix == QStringLiteral("hpp")
        || suffix == QStringLiteral("py") || suffix == QStringLiteral("sh") || suffix == QStringLiteral("cmake")) {
        return QStringLiteral("code");
    }
    return QStringLiteral("misc");
}

QString detectSourceRole(const QFileInfo &info, const QString &sourceType, const QString &text)
{
    const QString fingerprint = (info.fileName() + QLatin1Char(' ') + info.filePath() + QLatin1Char(' ') + text.left(4000)).toLower();

    if (sourceType == QStringLiteral("log") || fingerprint.contains(QStringLiteral("alarm")) || fingerprint.contains(QStringLiteral("traceback"))) {
        return QStringLiteral("log");
    }
    if (sourceType == QStringLiteral("config")) {
        return QStringLiteral("config");
    }
    if (sourceType == QStringLiteral("code")) {
        return QStringLiteral("code");
    }

    if (fingerprint.contains(QStringLiteral("hld")) || fingerprint.contains(QStringLiteral("lld"))
            || fingerprint.contains(QStringLiteral("topology")) || fingerprint.contains(QStringLiteral("architecture"))
            || fingerprint.contains(QStringLiteral("design")) || fingerprint.contains(QStringLiteral("scenario"))
            || fingerprint.contains(QStringLiteral("ciq"))) {
        return QStringLiteral("scenario");
    }

    if (fingerprint.contains(QStringLiteral("runbook")) || fingerprint.contains(QStringLiteral("mop"))
            || fingerprint.contains(QStringLiteral("operator training")) || fingerprint.contains(QStringLiteral("training manual"))
            || fingerprint.contains(QStringLiteral("procedure")) || fingerprint.contains(QStringLiteral("deployment"))
            || fingerprint.contains(QStringLiteral("install")) || fingerprint.contains(QStringLiteral("bootstrap"))
            || fingerprint.contains(QStringLiteral("workflow"))) {
        return QStringLiteral("procedure");
    }

    if (fingerprint.contains(QStringLiteral("guide")) || fingerprint.contains(QStringLiteral("manual"))
            || fingerprint.contains(QStringLiteral("reference")) || fingerprint.contains(QStringLiteral("concatenated"))
            || fingerprint.contains(QStringLiteral("release notes")) || fingerprint.contains(QStringLiteral("overview"))) {
        return QStringLiteral("reference");
    }

    return sourceType == QStringLiteral("doc") ? QStringLiteral("reference") : sourceType;
}

QStringList extensions()
{
    return {
        QStringLiteral("*.txt"), QStringLiteral("*.md"), QStringLiteral("*.log"), QStringLiteral("*.yaml"),
        QStringLiteral("*.yml"), QStringLiteral("*.json"), QStringLiteral("*.ini"), QStringLiteral("*.cfg"),
        QStringLiteral("*.conf"), QStringLiteral("*.xml"), QStringLiteral("*.csv"), QStringLiteral("*.service"),
        QStringLiteral("*.sh"), QStringLiteral("*.py"), QStringLiteral("*.c"), QStringLiteral("*.cc"),
        QStringLiteral("*.cpp"), QStringLiteral("*.cxx"), QStringLiteral("*.h"), QStringLiteral("*.hpp"),
        QStringLiteral("*.cmake"), QStringLiteral("*.pdf")
    };
}

QString trimTrailingWhitespacePerLine(const QString &text)
{
    QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (QString &line : lines) {
        while (!line.isEmpty() && (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t')))) {
            line.chop(1);
        }
    }
    return lines.join(QStringLiteral("\n"));
}

QString collapseExcessBlankLines(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("\\n{4,}")), QStringLiteral("\n\n\n"));
    return text;
}

QString cleanedText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QChar(0x00A0), QLatin1Char(' '));
    text.replace(QLatin1Char('\t'), QStringLiteral("    "));
    text.replace(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F]")), QStringLiteral(" "));
    text = trimTrailingWhitespacePerLine(text);
    text = collapseExcessBlankLines(text);
    return text.trimmed();
}

QString cleanedPdfText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QChar(0x00A0), QLatin1Char(' '));
    text.replace(QLatin1Char('\t'), QStringLiteral("    "));
    text.replace(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F]")), QStringLiteral(" "));

    const QStringList rawPages = text.split(QChar('\f'), Qt::KeepEmptyParts);
    QStringList pages;
    pages.reserve(rawPages.size());
    int pageNumber = 0;
    for (const QString &rawPage : rawPages) {
        const QString normalizedPage = collapseExcessBlankLines(trimTrailingWhitespacePerLine(rawPage)).trimmed();
        if (normalizedPage.isEmpty()) {
            continue;
        }
        ++pageNumber;
        pages << QStringLiteral("[[PAGE %1]]\n%2").arg(pageNumber).arg(normalizedPage);
    }

    if (pages.isEmpty()) {
        return cleanedText(text);
    }

    return pages.join(QStringLiteral("\n\n"));
}

bool isPageMarkerLine(const QString &trimmed)
{
    return trimmed.startsWith(QStringLiteral("[[PAGE ")) && trimmed.endsWith(QStringLiteral("]]"));
}

bool isFenceDelimiter(const QString &trimmed)
{
    return trimmed.startsWith(QStringLiteral("```")) || trimmed.startsWith(QStringLiteral("~~~"));
}

bool isHeadingLikeLine(const QString &trimmed)
{
    if (trimmed.startsWith(QLatin1Char('#'))) {
        return true;
    }
    static const QRegularExpression numberedHeading(QStringLiteral(R"(^\d+(?:\.\d+){0,4}[.)]?\s+\S+)"));
    static const QRegularExpression titledHeading(QStringLiteral(R"(^[A-Z][A-Za-z0-9 _/().:+-]{2,80}:$)"));
    return numberedHeading.match(trimmed).hasMatch() || titledHeading.match(trimmed).hasMatch();
}

bool isBulletLine(const QString &trimmed)
{
    static const QRegularExpression bulletExpression(QStringLiteral(R"(^(?:[-*+]\s+|\d+[.)]\s+|[a-zA-Z][.)]\s+))"));
    return bulletExpression.match(trimmed).hasMatch();
}

bool isStructuredCodeLikeLine(const QString &line)
{
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    if (line.startsWith(QStringLiteral("    ")) || line.startsWith(QLatin1Char('\t'))) {
        return true;
    }
    return trimmed.startsWith(QLatin1Char('$'))
            || trimmed.startsWith(QLatin1Char('#'))
            || trimmed.contains(QStringLiteral("::"))
            || trimmed.contains(QStringLiteral("=>"))
            || trimmed.contains(QLatin1Char('{'))
            || trimmed.contains(QLatin1Char('}'))
            || trimmed.contains(QLatin1Char('='));
}

QString normalizeBlockText(const QString &text)
{
    return collapseExcessBlankLines(trimTrailingWhitespacePerLine(text)).trimmed();
}

QStringList splitOversizedBlock(const QString &block, int maxBlockChars)
{
    const QString normalized = normalizeBlockText(block);
    if (normalized.isEmpty()) {
        return {};
    }
    if (normalized.size() <= maxBlockChars) {
        return {normalized};
    }

    QStringList parts;
    int offset = 0;
    while (offset < normalized.size()) {
        const int remaining = normalized.size() - offset;
        if (remaining <= maxBlockChars) {
            parts << normalized.mid(offset).trimmed();
            break;
        }

        const int hardEnd = qMin(normalized.size(), offset + maxBlockChars);
        const int minimumSplit = offset + qMax(600, maxBlockChars / 2);
        int split = normalized.lastIndexOf(QStringLiteral("\n\n"), hardEnd);
        if (split < minimumSplit) {
            split = normalized.lastIndexOf(QLatin1Char('\n'), hardEnd);
        }
        if (split < minimumSplit) {
            split = normalized.lastIndexOf(QLatin1Char(' '), hardEnd);
        }
        if (split < minimumSplit) {
            split = hardEnd;
        }

        const QString piece = normalized.mid(offset, split - offset).trimmed();
        if (!piece.isEmpty()) {
            parts << piece;
        }
        offset = split;
        while (offset < normalized.size() && normalized.at(offset).isSpace()) {
            ++offset;
        }
    }

    return parts;
}

QVector<QString> buildSemanticBlocks(const QString &text)
{
    QVector<QString> blocks;
    if (text.trimmed().isEmpty()) {
        return blocks;
    }

    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    QStringList currentLines;
    QString pendingPrefix;
    bool inFence = false;

    auto flushCurrent = [&]() {
        if (currentLines.isEmpty()) {
            return;
        }
        const QString block = normalizeBlockText(currentLines.join(QStringLiteral("\n")));
        if (!block.isEmpty()) {
            const QStringList pieces = splitOversizedBlock(block, 1800);
            for (const QString &piece : pieces) {
                if (!piece.trimmed().isEmpty()) {
                    blocks.push_back(piece.trimmed());
                }
            }
        }
        currentLines.clear();
    };

    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();

        if (isPageMarkerLine(trimmed)) {
            flushCurrent();
            pendingPrefix = trimmed;
            continue;
        }

        if (isFenceDelimiter(trimmed)) {
            if (!inFence) {
                flushCurrent();
                if (!pendingPrefix.isEmpty()) {
                    currentLines << pendingPrefix;
                    pendingPrefix.clear();
                }
            }
            currentLines << line;
            inFence = !inFence;
            if (!inFence) {
                flushCurrent();
            }
            continue;
        }

        if (inFence) {
            currentLines << line;
            continue;
        }

        if (trimmed.isEmpty()) {
            flushCurrent();
            continue;
        }

        if (isHeadingLikeLine(trimmed)) {
            flushCurrent();
            QString headingBlock = trimmed;
            if (!pendingPrefix.isEmpty()) {
                headingBlock.prepend(pendingPrefix + QStringLiteral("\n"));
                pendingPrefix.clear();
            }
            blocks.push_back(headingBlock.trimmed());
            continue;
        }

        const bool startsStructuredBlock = isBulletLine(trimmed) || isStructuredCodeLikeLine(line);
        if (startsStructuredBlock && !currentLines.isEmpty()) {
            const QString previousTrimmed = currentLines.constLast().trimmed();
            const bool previousStructured = isBulletLine(previousTrimmed) || isStructuredCodeLikeLine(currentLines.constLast());
            if (!previousStructured) {
                flushCurrent();
            }
        }

        if (!pendingPrefix.isEmpty() && currentLines.isEmpty()) {
            currentLines << pendingPrefix;
            pendingPrefix.clear();
        }
        currentLines << line;
    }

    flushCurrent();
    if (!pendingPrefix.isEmpty()) {
        blocks.push_back(pendingPrefix);
    }
    return blocks;
}

int totalBlockChars(const QStringList &blocks)
{
    int total = 0;
    for (const QString &block : blocks) {
        total += block.size();
    }
    if (blocks.size() > 1) {
        total += (blocks.size() - 1) * 2;
    }
    return total;
}

QStringList overlapTailBlocks(const QStringList &blocks, int overlapChars)
{
    QStringList carry;
    int carryChars = 0;
    for (int i = blocks.size() - 1; i >= 0; --i) {
        const QString &block = blocks.at(i);
        const int blockChars = block.size() + (carry.isEmpty() ? 0 : 2);
        if (!carry.isEmpty() && carryChars + blockChars > overlapChars * 2) {
            break;
        }
        carry.prepend(block);
        carryChars += blockChars;
        if (carryChars >= overlapChars) {
            break;
        }
    }
    return carry;
}

QVector<QString> buildChunksFromBlocks(const QVector<QString> &blocks, bool preferCompactChunks)
{
    const int targetChunkChars = preferCompactChunks ? 1400 : 2400;
    const int minimumChunkChars = preferCompactChunks ? 650 : 1100;
    const int hardChunkChars = preferCompactChunks ? 1800 : 3200;
    const int overlapChars = preferCompactChunks ? 140 : 360;

    QVector<QString> chunks;
    if (blocks.isEmpty()) {
        return chunks;
    }

    QStringList currentBlocks;
    int currentChars = 0;

    auto flushChunk = [&]() {
        if (currentBlocks.isEmpty()) {
            return;
        }
        const QString chunk = normalizeBlockText(currentBlocks.join(QStringLiteral("\n\n")));
        if (!chunk.isEmpty()) {
            chunks.push_back(chunk);
        }
    };

    for (const QString &block : blocks) {
        if (block.trimmed().isEmpty()) {
            continue;
        }

        const int separatorChars = currentBlocks.isEmpty() ? 0 : 2;
        const int projected = currentChars + separatorChars + block.size();
        const bool shouldSplit = !currentBlocks.isEmpty()
                && projected > targetChunkChars
                && currentChars >= minimumChunkChars;
        const bool mustSplit = !currentBlocks.isEmpty() && projected > hardChunkChars;
        if (shouldSplit || mustSplit) {
            const QStringList carry = overlapTailBlocks(currentBlocks, overlapChars);
            flushChunk();
            currentBlocks = carry;
            currentChars = totalBlockChars(currentBlocks);
        }

        currentBlocks << block;
        currentChars = totalBlockChars(currentBlocks);
    }

    flushChunk();

    if (chunks.size() >= 2 && chunks.constLast().size() < minimumChunkChars / 2) {
        const QString merged = normalizeBlockText(chunks.at(chunks.size() - 2) + QStringLiteral("\n\n") + chunks.constLast());
        chunks[chunks.size() - 2] = merged;
        chunks.removeLast();
    }

    return chunks;
}

bool shouldKeepChunkText(const QString &text)
{
    const QString simplified = text.simplified();
    if (simplified.isEmpty()) {
        return false;
    }

    if (simplified.size() >= 80) {
        return true;
    }

    int alnumCount = 0;
    int wordCount = 0;
    bool inWord = false;
    for (const QChar ch : simplified) {
        if (ch.isLetterOrNumber()) {
            ++alnumCount;
            if (!inWord) {
                ++wordCount;
                inWord = true;
            }
        } else {
            inWord = false;
        }
    }

    return alnumCount >= 32 && wordCount >= 6;
}

bool isCancelRequested(const std::atomic_bool *cancelRequested)
{
    return cancelRequested != nullptr && cancelRequested->load(std::memory_order_relaxed);
}

bool readTextFile(const QString &path, QString *text, QString *extractor, std::atomic_bool *cancelRequested)
{
    QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("pdf")) {
        QProcess process;
        process.start(QStringLiteral("pdftotext"), {QStringLiteral("-layout"), path, QStringLiteral("-")});
        if (!process.waitForStarted(1500)) {
            if (extractor != nullptr) {
                *extractor = QStringLiteral("pdf:pdftotext-not-found");
            }
            return false;
        }
        while (!process.waitForFinished(150)) {
            if (isCancelRequested(cancelRequested)) {
                process.kill();
                process.waitForFinished(1000);
                if (extractor != nullptr) {
                    *extractor = QStringLiteral("canceled");
                }
                return false;
            }
        }
        if (isCancelRequested(cancelRequested)) {
            process.kill();
            process.waitForFinished(1000);
            if (extractor != nullptr) {
                *extractor = QStringLiteral("canceled");
            }
            return false;
        }
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            if (extractor != nullptr) {
                *extractor = QStringLiteral("pdf:pdftotext-failed");
            }
            return false;
        }
        if (text != nullptr) {
            *text = cleanedPdfText(QString::fromUtf8(process.readAllStandardOutput()));
        }
        if (extractor != nullptr) {
            *extractor = QStringLiteral("pdf:pdftotext-layout-paged");
        }
        return true;
    }

    if (isCancelRequested(cancelRequested)) {
        if (extractor != nullptr) {
            *extractor = QStringLiteral("canceled");
        }
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray bytes = file.readAll();
    file.close();

    if (isCancelRequested(cancelRequested)) {
        if (extractor != nullptr) {
            *extractor = QStringLiteral("canceled");
        }
        return false;
    }
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


QString canonicalPathFor(const QString &path)
{
    const QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));
    if (cleaned.isEmpty()) {
        return QString();
    }

    QFileInfo info(cleaned);
    QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty()) {
        canonical = info.absoluteFilePath();
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(canonical));
}

QString manifestPathForRoot(const QString &destinationRoot)
{
    return QDir(destinationRoot).filePath(QStringLiteral(".amelia_kb_manifest.json"));
}

QString collectionsRootFor(const QString &destinationRoot)
{
    return QDir(destinationRoot).filePath(QStringLiteral("collections"));
}

QString stableHashHex(const QString &value)
{
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString sanitizeLabelComponent(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9._-]+)")), QStringLiteral("_"));
    value.replace(QRegularExpression(QStringLiteral(R"(_{2,})")), QStringLiteral("_"));
    value.remove(QRegularExpression(QStringLiteral(R"(^[_\.]+|[_\.]+$)")));
    if (value.isEmpty()) {
        return QStringLiteral("item");
    }
    return value;
}

QString groupLabelFromRelativePath(const QString &relativePath)
{
    const QString relativeDir = QFileInfo(relativePath).path();
    if (relativeDir == QStringLiteral(".") || relativeDir.trimmed().isEmpty()) {
        return QStringLiteral("(root)");
    }
    return QDir::fromNativeSeparators(relativeDir);
}

QString ensureUniqueRelativePath(QString relativePath, QSet<QString> *usedRelativePaths)
{
    if (usedRelativePaths == nullptr) {
        return QDir::cleanPath(QDir::fromNativeSeparators(relativePath));
    }

    relativePath = QDir::cleanPath(QDir::fromNativeSeparators(relativePath));
    if (!usedRelativePaths->contains(relativePath)) {
        usedRelativePaths->insert(relativePath);
        return relativePath;
    }

    QFileInfo info(relativePath);
    const QString dirPath = info.path() == QStringLiteral(".") ? QString() : info.path();
    const QString baseName = info.completeBaseName().isEmpty() ? info.fileName() : info.completeBaseName();
    const QString suffix = info.suffix();

    int attempt = 1;
    while (true) {
        const QString candidateName = suffix.isEmpty()
                ? QStringLiteral("%1_%2").arg(baseName).arg(attempt)
                : QStringLiteral("%1_%2.%3").arg(baseName).arg(attempt).arg(suffix);
        const QString candidate = dirPath.isEmpty()
                ? candidateName
                : QDir::cleanPath(dirPath + QLatin1Char('/') + candidateName);
        if (!usedRelativePaths->contains(candidate)) {
            usedRelativePaths->insert(candidate);
            return candidate;
        }
        ++attempt;
    }
}

QString standaloneRelativePathFor(const QFileInfo &info)
{
    const QString parentPath = canonicalPathFor(info.absolutePath());
    const QString parentName = sanitizeLabelComponent(QFileInfo(parentPath).fileName());
    const QString parentHash = stableHashHex(parentPath).left(10);
    return QDir::cleanPath(QStringLiteral("%1_%2/%3").arg(parentName, parentHash, info.fileName()));
}

struct ManifestEntry {
    QString internalPath;
    QString relativePath;
    QString originalPath;
    QString groupId;
    QString groupLabel;
};

struct ManifestCollection {
    QString collectionId;
    QString label;
    QString createdAt;
    QVector<ManifestEntry> entries;
};

QJsonObject manifestEntryToJson(const ManifestEntry &entry)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("internalPath"), entry.internalPath);
    obj.insert(QStringLiteral("relativePath"), entry.relativePath);
    obj.insert(QStringLiteral("originalPath"), entry.originalPath);
    obj.insert(QStringLiteral("groupId"), entry.groupId);
    obj.insert(QStringLiteral("groupLabel"), entry.groupLabel);
    return obj;
}

ManifestEntry manifestEntryFromJson(const QJsonObject &obj)
{
    ManifestEntry entry;
    entry.internalPath = obj.value(QStringLiteral("internalPath")).toString();
    entry.relativePath = obj.value(QStringLiteral("relativePath")).toString();
    entry.originalPath = obj.value(QStringLiteral("originalPath")).toString();
    entry.groupId = obj.value(QStringLiteral("groupId")).toString();
    entry.groupLabel = obj.value(QStringLiteral("groupLabel")).toString();
    return entry;
}

QJsonObject manifestCollectionToJson(const ManifestCollection &collection)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("collectionId"), collection.collectionId);
    obj.insert(QStringLiteral("label"), collection.label);
    obj.insert(QStringLiteral("createdAt"), collection.createdAt);

    QJsonArray entries;
    for (const ManifestEntry &entry : collection.entries) {
        entries.push_back(manifestEntryToJson(entry));
    }
    obj.insert(QStringLiteral("entries"), entries);
    return obj;
}

ManifestCollection manifestCollectionFromJson(const QJsonObject &obj)
{
    ManifestCollection collection;
    collection.collectionId = obj.value(QStringLiteral("collectionId")).toString();
    collection.label = obj.value(QStringLiteral("label")).toString();
    collection.createdAt = obj.value(QStringLiteral("createdAt")).toString();

    const QJsonArray entries = obj.value(QStringLiteral("entries")).toArray();
    collection.entries.reserve(entries.size());
    for (const QJsonValue &value : entries) {
        if (!value.isObject()) {
            continue;
        }
        const ManifestEntry entry = manifestEntryFromJson(value.toObject());
        if (!entry.internalPath.isEmpty()) {
            collection.entries.push_back(entry);
        }
    }
    return collection;
}

QVector<ManifestCollection> loadManifestCollections(const QString &destinationRoot)
{
    QVector<ManifestCollection> collections;
    QFile file(manifestPathForRoot(destinationRoot));
    if (!file.exists()) {
        return collections;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return collections;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return collections;
    }

    const QJsonArray array = doc.object().value(QStringLiteral("collections")).toArray();
    collections.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const ManifestCollection collection = manifestCollectionFromJson(value.toObject());
        if (!collection.collectionId.isEmpty()) {
            collections.push_back(collection);
        }
    }
    return collections;
}

bool saveManifestCollections(const QString &destinationRoot, const QVector<ManifestCollection> &collections)
{
    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("amelia-kb-manifest-v1"));
    root.insert(QStringLiteral("knowledgeRoot"), QDir::cleanPath(QDir::fromNativeSeparators(destinationRoot)));

    QJsonArray array;
    for (const ManifestCollection &collection : collections) {
        array.push_back(manifestCollectionToJson(collection));
    }
    root.insert(QStringLiteral("collections"), array);

    QSaveFile file(manifestPathForRoot(destinationRoot));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        return false;
    }
    return file.commit();
}

bool labelExistsInManifest(const QVector<ManifestCollection> &collections,
                           const QString &label,
                           const QString &excludedCollectionId = QString())
{
    const QString needle = label.trimmed();
    for (const ManifestCollection &collection : collections) {
        if (!excludedCollectionId.isEmpty() && collection.collectionId == excludedCollectionId) {
            continue;
        }
        if (collection.label.compare(needle, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

struct SourceMetadata {
    QString collectionId;
    QString collectionLabel;
    QString groupId;
    QString groupLabel;
    QString relativePath;
    QString originalPath;
};

QHash<QString, SourceMetadata> buildMetadataByInternalPath(const QVector<ManifestCollection> &collections)
{
    QHash<QString, SourceMetadata> map;
    for (const ManifestCollection &collection : collections) {
        for (const ManifestEntry &entry : collection.entries) {
            SourceMetadata metadata;
            metadata.collectionId = collection.collectionId;
            metadata.collectionLabel = collection.label;
            metadata.groupId = entry.groupId;
            metadata.groupLabel = entry.groupLabel;
            metadata.relativePath = entry.relativePath;
            metadata.originalPath = entry.originalPath;
            map.insert(canonicalPathFor(entry.internalPath), metadata);
        }
    }
    return map;
}

bool copyFileIntoCollection(const QString &sourceFile,
                            const QString &collectionRoot,
                            const QString &relativePath,
                            const QString &collectionId,
                            QVector<ManifestEntry> *entries,
                            QSet<QString> *usedRelativePaths)
{
    if (entries == nullptr) {
        return false;
    }

    QFileInfo info(sourceFile);
    if (!info.exists() || !info.isFile()) {
        return false;
    }

    const QString uniqueRelativePath = ensureUniqueRelativePath(relativePath, usedRelativePaths);
    const QString destinationPath = QDir(collectionRoot).filePath(uniqueRelativePath);
    QDir().mkpath(QFileInfo(destinationPath).dir().absolutePath());
    QFile::remove(destinationPath);
    if (!QFile::copy(info.absoluteFilePath(), destinationPath)) {
        return false;
    }

    ManifestEntry entry;
    entry.internalPath = canonicalPathFor(destinationPath);
    entry.relativePath = uniqueRelativePath;
    entry.originalPath = canonicalPathFor(info.absoluteFilePath());
    entry.groupLabel = groupLabelFromRelativePath(uniqueRelativePath);
    entry.groupId = stableHashHex(collectionId + QStringLiteral("|") + entry.groupLabel);
    entries->push_back(entry);
    return true;
}

bool moveStoredKnowledgeFile(const QString &sourcePath, const QString &destinationPath)
{
    const QString normalizedSource = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(sourcePath).absoluteFilePath()));
    const QString normalizedDestination = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(destinationPath).absoluteFilePath()));
    if (normalizedSource == normalizedDestination) {
        return true;
    }

    QDir().mkpath(QFileInfo(destinationPath).dir().absolutePath());
    QFile::remove(destinationPath);
    if (QFile::rename(sourcePath, destinationPath)) {
        return true;
    }
    if (!QFile::copy(sourcePath, destinationPath)) {
        return false;
    }
    return QFile::remove(sourcePath);
}

int importPathIntoCollection(const QString &path,
                             const QString &collectionId,
                             const QString &collectionRoot,
                             QVector<ManifestEntry> *entries,
                             QSet<QString> *usedRelativePaths)
{
    QFileInfo info(path);
    if (!info.exists()) {
        return 0;
    }

    int copied = 0;
    if (info.isFile()) {
        if (copyFileIntoCollection(info.absoluteFilePath(),
                                   collectionRoot,
                                   standaloneRelativePathFor(info),
                                   collectionId,
                                   entries,
                                   usedRelativePaths)) {
            ++copied;
        }
        return copied;
    }

    const QString rootPath = canonicalPathFor(info.absoluteFilePath());
    QDirIterator it(info.absoluteFilePath(), extensions(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourceFile = it.next();
        const QFileInfo sourceInfo(sourceFile);
        QString relative = QDir(rootPath).relativeFilePath(sourceFile);
        if (relative.startsWith(QStringLiteral("../")) || relative.trimmed().isEmpty()) {
            relative = standaloneRelativePathFor(sourceInfo);
        }
        if (copyFileIntoCollection(sourceFile,
                                   collectionRoot,
                                   relative,
                                   collectionId,
                                   entries,
                                   usedRelativePaths)) {
            ++copied;
        }
    }

    return copied;
}

void pruneEmptyKnowledgeDirectories(const QString &path, const QString &stopRoot)
{
    QString currentPath = QFileInfo(path).dir().absolutePath();
    const QString canonicalStopRoot = canonicalPathFor(stopRoot);
    while (!currentPath.isEmpty()) {
        const QString canonicalCurrent = canonicalPathFor(currentPath);
        if (canonicalCurrent.isEmpty() || canonicalCurrent == canonicalStopRoot) {
            break;
        }

        QDir dir(currentPath);
        const QStringList entries = dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries);
        if (!entries.isEmpty()) {
            break;
        }
        dir.rmdir(currentPath);
        currentPath = QFileInfo(currentPath).dir().absolutePath();
    }
}

bool isReservedKnowledgeMetadataFile(const QFileInfo &info)
{
    return info.fileName().startsWith(QStringLiteral(".amelia_"));
}

QStringList queryTerms(const QString &query)
{
    QString normalized = query.toLower();
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9._+:-]+")), QStringLiteral(" "));
    const QStringList parts = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    static const QSet<QString> stopWords = {
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"), QStringLiteral("with"),
        QStringLiteral("from"), QStringLiteral("into"), QStringLiteral("that"), QStringLiteral("this"),
        QStringLiteral("what"), QStringLiteral("when"), QStringLiteral("where"), QStringLiteral("which"),
        QStringLiteral("your"), QStringLiteral("through"), QStringLiteral("create"), QStringLiteral("write"),
        QStringLiteral("format"), QStringLiteral("whole"), QStringLiteral("please"), QStringLiteral("show"),
        QStringLiteral("explain"), QStringLiteral("about")
    };

    static const QHash<QString, QStringList> expansions = {
        {QStringLiteral("deploy"), {QStringLiteral("deployment"), QStringLiteral("install"), QStringLiteral("bootstrap")}},
        {QStringLiteral("deployment"), {QStringLiteral("deploy"), QStringLiteral("install")}},
        {QStringLiteral("runbook"), {QStringLiteral("mop"), QStringLiteral("playbook"), QStringLiteral("procedure")}},
        {QStringLiteral("mop"), {QStringLiteral("runbook"), QStringLiteral("procedure")}},
        {QStringLiteral("hld"), {QStringLiteral("architecture"), QStringLiteral("topology")}},
        {QStringLiteral("lld"), {QStringLiteral("design"), QStringLiteral("implementation")}},
        {QStringLiteral("k8s"), {QStringLiteral("kubernetes")}},
        {QStringLiteral("harbor"), {QStringLiteral("registry")}}
    };

    QStringList terms;
    QSet<QString> seen;
    for (const QString &part : parts) {
        if (part.size() < 2 || stopWords.contains(part) || seen.contains(part)) {
            continue;
        }
        seen.insert(part);
        terms.push_back(part);
        const auto expansionIt = expansions.constFind(part);
        if (expansionIt != expansions.cend()) {
            for (const QString &expanded : expansionIt.value()) {
                if (!seen.contains(expanded)) {
                    seen.insert(expanded);
                    terms.push_back(expanded);
                }
            }
        }
    }
    return terms;
}

double lexicalScoreChunk(const QString &text, const QString &fileName, const QStringList &terms, const QString &query)
{
    if (terms.isEmpty()) {
        return 0.0;
    }

    const QString lower = text.toLower();
    const QString fileLower = fileName.toLower();
    int matched = 0;
    double score = 0.0;
    for (const QString &term : terms) {
        const int bodyCount = lower.count(term);
        const int fileCount = fileLower.count(term);
        if (bodyCount > 0 || fileCount > 0) {
            ++matched;
        }
        if (bodyCount > 0) {
            score += 0.95 + qMin(1.25, 0.16 * static_cast<double>(bodyCount - 1));
        }
        if (fileCount > 0) {
            score += 0.55 + qMin(0.75, 0.18 * static_cast<double>(fileCount - 1));
        }
    }

    const double coverage = static_cast<double>(matched) / static_cast<double>(terms.size());
    score += coverage * 1.8;

    const QString queryLower = query.toLower().trimmed();
    if (!queryLower.isEmpty() && lower.contains(queryLower)) {
        score += 1.2;
    }
    if (!queryLower.isEmpty() && fileLower.contains(queryLower)) {
        score += 0.75;
    }

    return score;
}

double roleBias(RetrievalIntent intent, const QString &role, const QStringList &preferredRoles)
{
    double bias = 0.0;
    if (intent == RetrievalIntent::DocumentGeneration) {
        if (role == QStringLiteral("scenario")) bias += 0.55;
        else if (role == QStringLiteral("procedure")) bias += 0.48;
        else if (role == QStringLiteral("reference")) bias += 0.24;
        else if (role == QStringLiteral("config")) bias += 0.08;
    } else if (intent == RetrievalIntent::Troubleshooting) {
        if (role == QStringLiteral("log")) bias += 0.55;
        else if (role == QStringLiteral("config")) bias += 0.46;
        else if (role == QStringLiteral("procedure")) bias += 0.22;
        else if (role == QStringLiteral("reference")) bias += 0.12;
    } else if (intent == RetrievalIntent::Architecture) {
        if (role == QStringLiteral("scenario")) bias += 0.58;
        else if (role == QStringLiteral("reference")) bias += 0.30;
        else if (role == QStringLiteral("procedure")) bias += 0.15;
    } else if (intent == RetrievalIntent::Implementation) {
        if (role == QStringLiteral("procedure")) bias += 0.55;
        else if (role == QStringLiteral("config")) bias += 0.32;
        else if (role == QStringLiteral("reference")) bias += 0.24;
        else if (role == QStringLiteral("scenario")) bias += 0.18;
    } else {
        if (role == QStringLiteral("procedure") || role == QStringLiteral("reference")) {
            bias += 0.10;
        }
    }

    for (const QString &preferred : preferredRoles) {
        if (role == preferred.trimmed().toLower()) {
            bias += 0.28;
            break;
        }
    }

    return bias;
}

QString makeExcerpt(const QString &text, const QStringList &terms)
{
    const QString lower = text.toLower();
    int firstIndex = -1;
    for (const QString &term : terms) {
        const int idx = lower.indexOf(term);
        if (idx >= 0 && (firstIndex < 0 || idx < firstIndex)) {
            firstIndex = idx;
        }
    }

    if (firstIndex < 0) {
        return text.left(300).trimmed();
    }

    const int start = qMax(0, firstIndex - 90);
    return text.mid(start, 360).trimmed();
}

QString matchReason(double lexical, double semantic, const QString &role, bool neuralSemantic)
{
    QStringList reasons;
    if (lexical >= 1.8) {
        reasons << QStringLiteral("strong lexical overlap");
    } else if (lexical > 0.0) {
        reasons << QStringLiteral("keyword overlap");
    }
    if (semantic >= 0.65) {
        reasons << (neuralSemantic
                    ? QStringLiteral("strong embedding similarity")
                    : QStringLiteral("strong surface-form vector similarity"));
    } else if (semantic >= 0.38) {
        reasons << (neuralSemantic
                    ? QStringLiteral("embedding similarity")
                    : QStringLiteral("surface-form vector similarity"));
    }
    if (!role.trimmed().isEmpty()) {
        reasons << QStringLiteral("role=%1").arg(role);
    }
    return reasons.join(QStringLiteral(", "));
}
}

void RagIndexer::setDocsRoot(const QString &rootPath)
{
    m_docsRoot = QDir::cleanPath(QDir::fromNativeSeparators(rootPath.trimmed()));
}

void RagIndexer::setCachePath(const QString &cachePath)
{
    m_cachePath = QDir::cleanPath(QDir::fromNativeSeparators(cachePath.trimmed()));
}

void RagIndexer::setSemanticEnabled(bool enabled)
{
    m_semanticEnabled = enabled;
}

void RagIndexer::configureEmbeddingBackend(const QString &baseUrl, const QString &model, int timeoutMs, int batchSize)
{
    m_embeddingClient.configureOllama(baseUrl, model, timeoutMs, batchSize);
}

void RagIndexer::requestCancel()
{
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

bool RagIndexer::lastReindexCanceled() const
{
    return m_lastReindexCanceled;
}

void RagIndexer::rebuildEmbeddings()
{
    ensureEmbeddingsForChunks(m_chunks);
}

void RagIndexer::ensureEmbeddingsForChunks(QVector<Chunk> &chunks) const
{
    if (!m_semanticEnabled) {
        for (Chunk &chunk : chunks) {
            chunk.embedding.clear();
        }
        return;
    }

    QStringList missingTexts;
    QVector<int> missingIndexes;
    missingTexts.reserve(chunks.size());
    missingIndexes.reserve(chunks.size());
    for (int i = 0; i < chunks.size(); ++i) {
        if (chunks[i].embedding.isEmpty()) {
            missingIndexes.push_back(i);
            missingTexts.push_back(chunks[i].text);
        }
    }

    if (missingTexts.isEmpty()) {
        return;
    }

    const QVector<QVector<float>> embeddings = m_embeddingClient.embedTexts(missingTexts);
    const int assignCount = qMin(missingIndexes.size(), embeddings.size());
    for (int i = 0; i < assignCount; ++i) {
        chunks[missingIndexes.at(i)].embedding = embeddings.at(i);
    }
}

bool RagIndexer::sourceMatchesFile(const SourceInfo &source, const QFileInfo &info)
{
    return source.fileModifiedMs == info.lastModified().toMSecsSinceEpoch()
            && source.fileSizeBytes == info.size();
}

QString RagIndexer::chunkingStrategyName()
{
    return QStringLiteral("semantic-blocks-v3");
}

int RagIndexer::reindex(const std::function<void(int, int, const QString &)> &progressCallback)
{
    m_cancelRequested.store(false, std::memory_order_relaxed);
    m_lastReindexCanceled = false;

    auto finalizeWorkingState = [this, &progressCallback](const QHash<QString, SourceInfo> &workingSourcesByPath,
                                                          const QHash<QString, QVector<Chunk>> &workingChunksByPath,
                                                          const QString &label,
                                                          bool canceled,
                                                          int progressValue,
                                                          int progressMaximum) -> int {
        QVector<SourceInfo> finalizedSources;
        finalizedSources.reserve(workingSourcesByPath.size());
        for (auto it = workingSourcesByPath.cbegin(); it != workingSourcesByPath.cend(); ++it) {
            finalizedSources.push_back(it.value());
        }
        std::sort(finalizedSources.begin(), finalizedSources.end(), [](const SourceInfo &a, const SourceInfo &b) {
            if (a.collectionLabel != b.collectionLabel) {
                return a.collectionLabel.toLower() < b.collectionLabel.toLower();
            }
            if (a.groupLabel != b.groupLabel) {
                return a.groupLabel.toLower() < b.groupLabel.toLower();
            }
            if (a.relativePath != b.relativePath) {
                return a.relativePath.toLower() < b.relativePath.toLower();
            }
            return a.fileName.toLower() < b.fileName.toLower();
        });

        QVector<Chunk> finalizedChunks;
        for (const SourceInfo &source : finalizedSources) {
            const QVector<Chunk> chunksForPath = workingChunksByPath.value(source.filePath);
            for (const Chunk &chunk : chunksForPath) {
                finalizedChunks.push_back(chunk);
            }
        }

        m_sources = finalizedSources;
        m_chunks = finalizedChunks;
        saveCache();
        m_lastReindexCanceled = canceled;

        if (progressCallback) {
            progressCallback(progressValue, progressMaximum, label);
        }
        return m_chunks.size();
    };

    if (m_docsRoot.trimmed().isEmpty()) {
        m_chunks.clear();
        m_sources.clear();
        saveCache();
        return 0;
    }

    const QVector<ManifestCollection> manifestCollections = loadManifestCollections(m_docsRoot);
    const QHash<QString, SourceMetadata> metadataByPath = buildMetadataByInternalPath(manifestCollections);

    QStringList paths;
    QDirIterator gatherIt(m_docsRoot, extensions(), QDir::Files, QDirIterator::Subdirectories);
    while (gatherIt.hasNext()) {
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            return finalizeWorkingState({}, {},
                                        QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."),
                                        true,
                                        0,
                                        1);
        }

        const QString path = gatherIt.next();
        const QFileInfo info(path);
        if (isReservedKnowledgeMetadataFile(info)) {
            continue;
        }
        paths.push_back(path);
    }
    std::sort(paths.begin(), paths.end());

    const int totalFiles = paths.size();
    if (progressCallback) {
        progressCallback(0, totalFiles > 0 ? totalFiles : 1, QStringLiteral("Scanning local documents..."));
    }

    QHash<QString, SourceInfo> existingSourcesByPath;
    existingSourcesByPath.reserve(m_sources.size());
    for (const SourceInfo &source : m_sources) {
        existingSourcesByPath.insert(source.filePath, source);
    }

    QHash<QString, QVector<Chunk>> existingChunksByPath;
    existingChunksByPath.reserve(m_sources.size());
    for (const Chunk &chunk : m_chunks) {
        existingChunksByPath[chunk.filePath].push_back(chunk);
    }

    QHash<QString, SourceInfo> workingSourcesByPath = existingSourcesByPath;
    QHash<QString, QVector<Chunk>> workingChunksByPath = existingChunksByPath;

    QSet<QString> currentPathSet;
    for (const QString &path : paths) {
        currentPathSet.insert(path);
    }
    for (auto it = workingSourcesByPath.begin(); it != workingSourcesByPath.end();) {
        if (!currentPathSet.contains(it.key())) {
            workingChunksByPath.remove(it.key());
            it = workingSourcesByPath.erase(it);
            continue;
        }
        ++it;
    }

    int reusedFiles = 0;
    int rebuiltFiles = 0;
    int committedFiles = 0;

    const auto applyMetadata = [this, &metadataByPath](SourceInfo &source) {
        const QString canonicalPath = canonicalPathFor(source.filePath);
        const auto metadataIt = metadataByPath.constFind(canonicalPath);
        if (metadataIt != metadataByPath.cend()) {
            const SourceMetadata &metadata = metadataIt.value();
            source.collectionId = metadata.collectionId;
            source.collectionLabel = metadata.collectionLabel;
            source.groupId = metadata.groupId;
            source.groupLabel = metadata.groupLabel;
            source.relativePath = metadata.relativePath;
            source.originalPath = metadata.originalPath;
            return;
        }

        source.collectionId = QStringLiteral("legacy");
        source.collectionLabel = QStringLiteral("Legacy imports");
        source.relativePath = QDir(m_docsRoot).relativeFilePath(source.filePath);
        source.groupLabel = groupLabelFromRelativePath(source.relativePath);
        source.groupId = stableHashHex(source.collectionId + QStringLiteral("|") + source.groupLabel);
        source.originalPath.clear();
    };

    const auto cancelWithPartialCommit = [&](const QString &label) -> int {
        return finalizeWorkingState(workingSourcesByPath,
                                    workingChunksByPath,
                                    label,
                                    true,
                                    qBound(0, committedFiles, totalFiles > 0 ? totalFiles : 1),
                                    totalFiles > 0 ? totalFiles : 1);
    };

    for (int i = 0; i < totalFiles; ++i) {
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
        }

        const QString path = paths.at(i);
        const QFileInfo info(path);
        const QString sourceType = detectSourceType(info);

        const auto sourceIt = existingSourcesByPath.constFind(path);
        const auto chunksIt = existingChunksByPath.constFind(path);
        const bool canReuse = sourceIt != existingSourcesByPath.cend()
                && chunksIt != existingChunksByPath.cend()
                && sourceMatchesFile(sourceIt.value(), info);

        if (canReuse) {
            SourceInfo source = sourceIt.value();
            applyMetadata(source);
            QVector<Chunk> reusedChunks = chunksIt.value();
            ensureEmbeddingsForChunks(reusedChunks);
            if (!m_semanticEnabled) {
                for (Chunk &chunk : reusedChunks) {
                    chunk.embedding.clear();
                }
            }

            source.chunkCount = reusedChunks.size();
            workingSourcesByPath.insert(path, source);
            workingChunksByPath.insert(path, reusedChunks);
            ++reusedFiles;
            ++committedFiles;

            if (progressCallback) {
                progressCallback(i + 1,
                                 totalFiles > 0 ? totalFiles : 1,
                                 QStringLiteral("Using cached index %1 / %2: %3 (%4 chunks)").arg(i + 1).arg(totalFiles).arg(info.fileName()).arg(reusedChunks.size()));
            }
            continue;
        }

        if (progressCallback) {
            const QString phaseLabel = (info.suffix().compare(QStringLiteral("pdf"), Qt::CaseInsensitive) == 0)
                    ? QStringLiteral("Extracting PDF %1 / %2: %3").arg(i + 1).arg(totalFiles).arg(info.fileName())
                    : QStringLiteral("Indexing file %1 / %2: %3").arg(i + 1).arg(totalFiles).arg(info.fileName());
            progressCallback(i, totalFiles > 0 ? totalFiles : 1, phaseLabel);
        }

        QString textValue;
        QString extractor;
        const bool ok = readTextFile(path, &textValue, &extractor, &m_cancelRequested);
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
        }
        const QString sourceRole = detectSourceRole(info, sourceType, textValue);

        SourceInfo source;
        source.filePath = path;
        source.fileName = info.fileName();
        source.sourceType = sourceType;
        source.sourceRole = sourceRole;
        source.extractor = extractor.isEmpty() ? QStringLiteral("unknown") : extractor;
        source.fileModifiedMs = info.lastModified().toMSecsSinceEpoch();
        source.fileSizeBytes = info.size();
        applyMetadata(source);

        QVector<Chunk> rebuiltChunksForPath;
        if (ok && !textValue.trimmed().isEmpty()) {
            const bool preferCompactChunks = m_semanticEnabled && m_embeddingClient.isConfigured();
            const QVector<QString> blocks = buildSemanticBlocks(textValue);
            const QVector<QString> chunkTexts = buildChunksFromBlocks(blocks, preferCompactChunks);
            QStringList embeddingsInput;
            embeddingsInput.reserve(chunkTexts.size());
            for (const QString &chunkText : chunkTexts) {
                const QString normalizedChunk = chunkText.trimmed();
                if (shouldKeepChunkText(normalizedChunk)) {
                    embeddingsInput.push_back(normalizedChunk);
                }
            }

            QVector<QVector<float>> embeddings;
            if (m_semanticEnabled && !embeddingsInput.isEmpty()) {
                if (progressCallback) {
                    progressCallback(0, embeddingsInput.size(),
                                     QStringLiteral("Embedding %1 / %2: %3 — 0/%4 chunks")
                                         .arg(i + 1)
                                         .arg(totalFiles)
                                         .arg(info.fileName())
                                         .arg(embeddingsInput.size()));
                }
                embeddings = m_embeddingClient.embedTexts(embeddingsInput,
                                                          [progressCallback, i, totalFiles, info](int completed, int total) {
                                                              if (progressCallback) {
                                                                  progressCallback(completed, total,
                                                                                   QStringLiteral("Embedding %1 / %2: %3 — %4/%5 chunks")
                                                                                       .arg(i + 1)
                                                                                       .arg(totalFiles)
                                                                                       .arg(info.fileName())
                                                                                       .arg(completed)
                                                                                       .arg(total));
                                                              }
                                                          });
            }
            if (m_cancelRequested.load(std::memory_order_relaxed)) {
                return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
            }

            int chunkIndex = 0;
            for (int j = 0; j < embeddingsInput.size(); ++j) {
                if (m_cancelRequested.load(std::memory_order_relaxed)) {
                    return cancelWithPartialCommit(QStringLiteral("Indexing canceled. Partial cache saved; the in-flight file was discarded."));
                }

                Chunk chunk;
                chunk.filePath = path;
                chunk.fileName = info.fileName();
                chunk.sourceType = sourceType;
                chunk.sourceRole = sourceRole;
                chunk.text = embeddingsInput.at(j);
                chunk.chunkIndex = chunkIndex++;
                chunk.fileModifiedMs = source.fileModifiedMs;
                if (m_semanticEnabled && j < embeddings.size()) {
                    chunk.embedding = embeddings.at(j);
                }
                rebuiltChunksForPath.push_back(chunk);
                ++source.chunkCount;
            }
        }

        workingSourcesByPath.insert(path, source);
        workingChunksByPath.insert(path, rebuiltChunksForPath);
        ++rebuiltFiles;
        ++committedFiles;

        if (progressCallback) {
            progressCallback(i + 1,
                             totalFiles > 0 ? totalFiles : 1,
                             QStringLiteral("Indexed %1 / %2: %3").arg(i + 1).arg(totalFiles).arg(info.fileName()));
        }
    }

    return finalizeWorkingState(workingSourcesByPath,
                                workingChunksByPath,
                                QStringLiteral("Index ready: %1 files (%2 reused, %3 rebuilt, %4 chunks)")
                                    .arg(totalFiles)
                                    .arg(reusedFiles)
                                    .arg(rebuiltFiles)
                                    .arg([&workingChunksByPath]() {
                                        int count = 0;
                                        for (auto it = workingChunksByPath.cbegin(); it != workingChunksByPath.cend(); ++it) {
                                            count += it.value().size();
                                        }
                                        return count;
                                    }()),
                                false,
                                totalFiles > 0 ? totalFiles : 1,
                                totalFiles > 0 ? totalFiles : 1);
}

bool RagIndexer::loadCache()
{
    if (m_cachePath.trimmed().isEmpty()) {
        return false;
    }

    QFile file(m_cachePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return false;
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QStringLiteral("amelia-rag-cache-v2")) {
        return false;
    }
    if (root.value(QStringLiteral("docsRoot")).toString() != m_docsRoot) {
        return false;
    }
    if (root.value(QStringLiteral("chunkingStrategy")).toString() != chunkingStrategyName()) {
        return false;
    }
    if (m_semanticEnabled
        && root.value(QStringLiteral("embeddingCacheKey")).toString() != m_embeddingClient.cacheKey()) {
        return false;
    }

    QVector<Chunk> cachedChunks;
    const QJsonArray chunkArray = root.value(QStringLiteral("chunks")).toArray();
    cachedChunks.reserve(chunkArray.size());
    for (const QJsonValue &value : chunkArray) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        Chunk chunk;
        chunk.filePath = obj.value(QStringLiteral("filePath")).toString();
        chunk.fileName = obj.value(QStringLiteral("fileName")).toString();
        chunk.sourceType = obj.value(QStringLiteral("sourceType")).toString();
        chunk.sourceRole = obj.value(QStringLiteral("sourceRole")).toString();
        chunk.text = obj.value(QStringLiteral("text")).toString();
        chunk.chunkIndex = obj.value(QStringLiteral("chunkIndex")).toInt();
        chunk.fileModifiedMs = static_cast<qint64>(obj.value(QStringLiteral("fileModifiedMs")).toDouble());
        const QJsonArray embeddingArray = obj.value(QStringLiteral("embedding")).toArray();
        chunk.embedding.reserve(embeddingArray.size());
        for (const QJsonValue &embeddingValue : embeddingArray) {
            chunk.embedding.push_back(static_cast<float>(embeddingValue.toDouble()));
        }
        if (chunk.filePath.isEmpty() || chunk.text.isEmpty()) {
            continue;
        }
        cachedChunks.push_back(chunk);
    }

    QVector<SourceInfo> cachedSources;
    const QJsonArray sourceArray = root.value(QStringLiteral("sources")).toArray();
    cachedSources.reserve(sourceArray.size());
    for (const QJsonValue &value : sourceArray) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        SourceInfo source;
        source.filePath = obj.value(QStringLiteral("filePath")).toString();
        source.fileName = obj.value(QStringLiteral("fileName")).toString();
        source.sourceType = obj.value(QStringLiteral("sourceType")).toString();
        source.sourceRole = obj.value(QStringLiteral("sourceRole")).toString();
        source.extractor = obj.value(QStringLiteral("extractor")).toString();
        source.collectionId = obj.value(QStringLiteral("collectionId")).toString();
        source.collectionLabel = obj.value(QStringLiteral("collectionLabel")).toString();
        source.groupId = obj.value(QStringLiteral("groupId")).toString();
        source.groupLabel = obj.value(QStringLiteral("groupLabel")).toString();
        source.relativePath = obj.value(QStringLiteral("relativePath")).toString();
        source.originalPath = obj.value(QStringLiteral("originalPath")).toString();
        source.fileModifiedMs = static_cast<qint64>(obj.value(QStringLiteral("fileModifiedMs")).toDouble());
        source.fileSizeBytes = static_cast<qint64>(obj.value(QStringLiteral("fileSizeBytes")).toDouble());
        source.chunkCount = obj.value(QStringLiteral("chunkCount")).toInt();
        if (source.filePath.isEmpty()) {
            continue;
        }
        cachedSources.push_back(source);
    }

    m_chunks = std::move(cachedChunks);
    m_sources = std::move(cachedSources);
    if (!m_semanticEnabled) {
        for (Chunk &chunk : m_chunks) {
            chunk.embedding.clear();
        }
    }
    return !m_sources.isEmpty() || !m_chunks.isEmpty();
}

bool RagIndexer::saveCache() const
{
    if (m_cachePath.trimmed().isEmpty()) {
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("amelia-rag-cache-v2"));
    root.insert(QStringLiteral("docsRoot"), m_docsRoot);
    root.insert(QStringLiteral("semanticEnabled"), m_semanticEnabled);
    root.insert(QStringLiteral("chunkingStrategy"), chunkingStrategyName());
    root.insert(QStringLiteral("embeddingBackend"), m_embeddingClient.backendName());
    root.insert(QStringLiteral("embeddingCacheKey"), m_embeddingClient.cacheKey());

    QJsonArray chunkArray;
    for (const Chunk &chunk : m_chunks) {
        QJsonObject obj;
        obj.insert(QStringLiteral("filePath"), chunk.filePath);
        obj.insert(QStringLiteral("fileName"), chunk.fileName);
        obj.insert(QStringLiteral("sourceType"), chunk.sourceType);
        obj.insert(QStringLiteral("sourceRole"), chunk.sourceRole);
        obj.insert(QStringLiteral("text"), chunk.text);
        obj.insert(QStringLiteral("chunkIndex"), chunk.chunkIndex);
        obj.insert(QStringLiteral("fileModifiedMs"), static_cast<double>(chunk.fileModifiedMs));
        if (!chunk.embedding.isEmpty()) {
            QJsonArray embeddingArray;
            for (float value : chunk.embedding) {
                embeddingArray.push_back(static_cast<double>(value));
            }
            obj.insert(QStringLiteral("embedding"), embeddingArray);
        }
        chunkArray.push_back(obj);
    }
    root.insert(QStringLiteral("chunks"), chunkArray);

    QJsonArray sourceArray;
    for (const SourceInfo &source : m_sources) {
        QJsonObject obj;
        obj.insert(QStringLiteral("filePath"), source.filePath);
        obj.insert(QStringLiteral("fileName"), source.fileName);
        obj.insert(QStringLiteral("sourceType"), source.sourceType);
        obj.insert(QStringLiteral("sourceRole"), source.sourceRole);
        obj.insert(QStringLiteral("extractor"), source.extractor);
        obj.insert(QStringLiteral("collectionId"), source.collectionId);
        obj.insert(QStringLiteral("collectionLabel"), source.collectionLabel);
        obj.insert(QStringLiteral("groupId"), source.groupId);
        obj.insert(QStringLiteral("groupLabel"), source.groupLabel);
        obj.insert(QStringLiteral("relativePath"), source.relativePath);
        obj.insert(QStringLiteral("originalPath"), source.originalPath);
        obj.insert(QStringLiteral("fileModifiedMs"), static_cast<double>(source.fileModifiedMs));
        obj.insert(QStringLiteral("fileSizeBytes"), static_cast<double>(source.fileSizeBytes));
        obj.insert(QStringLiteral("chunkCount"), source.chunkCount);
        sourceArray.push_back(obj);
    }
    root.insert(QStringLiteral("sources"), sourceArray);

    QSaveFile file(m_cachePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

bool RagIndexer::cacheNeedsRefresh() const
{
    if (m_docsRoot.trimmed().isEmpty()) {
        return false;
    }

    QHash<QString, SourceInfo> cachedSourcesByPath;
    cachedSourcesByPath.reserve(m_sources.size());
    for (const SourceInfo &source : m_sources) {
        cachedSourcesByPath.insert(source.filePath, source);
    }

    QSet<QString> currentPaths;
    QDirIterator it(m_docsRoot, extensions(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        if (isReservedKnowledgeMetadataFile(info)) {
            continue;
        }
        currentPaths.insert(path);
        const auto sourceIt = cachedSourcesByPath.constFind(path);
        if (sourceIt == cachedSourcesByPath.cend()) {
            return true;
        }
        if (!sourceMatchesFile(sourceIt.value(), info)) {
            return true;
        }
    }

    if (currentPaths.size() != m_sources.size()) {
        return true;
    }

    for (const SourceInfo &source : m_sources) {
        if (!currentPaths.contains(source.filePath)) {
            return true;
        }
    }

    if (m_semanticEnabled) {
        for (const Chunk &chunk : m_chunks) {
            if (chunk.embedding.isEmpty()) {
                return true;
            }
        }
    }

    return false;
}

QVector<RagHit> RagIndexer::searchHits(const QString &query,
                                       int limit,
                                       RetrievalIntent intent,
                                       const QStringList &preferredRoles) const
{
    return searchHitsInFiles(query, QStringList(), limit, intent, preferredRoles);
}

QVector<RagHit> RagIndexer::searchHitsInFiles(const QString &query,
                                              const QStringList &preferredPaths,
                                              int limit,
                                              RetrievalIntent intent,
                                              const QStringList &preferredRoles) const
{
    const QStringList terms = queryTerms(query);

    int expectedEmbeddingDims = 0;
    for (const Chunk &chunk : m_chunks) {
        if (!chunk.embedding.isEmpty()) {
            expectedEmbeddingDims = chunk.embedding.size();
            break;
        }
    }

    QVector<float> queryEmbedding;
    bool neuralSemantic = false;
    bool semanticActive = false;
    if (m_semanticEnabled && expectedEmbeddingDims > 0) {
        queryEmbedding = m_embeddingClient.embedText(query);
        neuralSemantic = m_embeddingClient.lastRequestUsedNeural();
        if (queryEmbedding.size() != expectedEmbeddingDims && expectedEmbeddingDims == m_embeddingClient.localFallbackDimensions()) {
            queryEmbedding = m_embeddingClient.embedTextLocalFallback(query);
            neuralSemantic = false;
        }
        semanticActive = !queryEmbedding.isEmpty() && queryEmbedding.size() == expectedEmbeddingDims;
    }

    const double lexicalWeight = neuralSemantic ? 0.85 : 0.98;
    const double semanticWeight = neuralSemantic ? 1.55 : 0.28;
    const double threshold = neuralSemantic ? 0.92 : 1.00;

    QSet<QString> pathFilter;
    for (const QString &path : preferredPaths) {
        const QString trimmed = path.trimmed();
        if (!trimmed.isEmpty()) {
            pathFilter.insert(QDir::cleanPath(trimmed));
        }
    }

    QVector<RagHit> ranked;
    ranked.reserve(m_chunks.size());

    for (const Chunk &chunk : m_chunks) {
        if (!pathFilter.isEmpty() && !pathFilter.contains(QDir::cleanPath(chunk.filePath))) {
            continue;
        }

        const double lexical = lexicalScoreChunk(chunk.text, chunk.fileName, terms, query);
        const double semantic = semanticActive
                ? qMax(0.0f, EmbeddingClient::cosineSimilarity(queryEmbedding, chunk.embedding))
                : 0.0;
        const double role = roleBias(intent, chunk.sourceRole, preferredRoles);
        const bool strongPureSemanticHit = neuralSemantic && lexical <= 0.0 && semantic >= 0.72;
        if (!strongPureSemanticHit && lexical <= 0.0 && (!semanticActive || semantic < 0.38)) {
            continue;
        }

        const double finalScore = lexical * lexicalWeight + semantic * semanticWeight + role;
        if (finalScore <= threshold) {
            continue;
        }

        RagHit hit;
        hit.filePath = chunk.filePath;
        hit.fileName = chunk.fileName;
        hit.sourceType = chunk.sourceType;
        hit.sourceRole = chunk.sourceRole;
        hit.excerpt = makeExcerpt(chunk.text, terms);
        hit.chunkText = chunk.text;
        hit.matchReason = matchReason(lexical, semantic, chunk.sourceRole, neuralSemantic);
        hit.lexicalScore = lexical;
        hit.semanticScore = semantic;
        hit.rerankScore = finalScore;
        hit.score = qRound(finalScore * 100.0);
        hit.chunkIndex = chunk.chunkIndex;
        ranked.push_back(hit);
    }

    std::sort(ranked.begin(), ranked.end(), [](const RagHit &a, const RagHit &b) {
        if (!qFuzzyCompare(a.rerankScore + 1.0, b.rerankScore + 1.0)) {
            return a.rerankScore > b.rerankScore;
        }
        if (!qFuzzyCompare(a.lexicalScore + 1.0, b.lexicalScore + 1.0)) {
            return a.lexicalScore > b.lexicalScore;
        }
        if (!qFuzzyCompare(a.semanticScore + 1.0, b.semanticScore + 1.0)) {
            return a.semanticScore > b.semanticScore;
        }
        if (a.fileName != b.fileName) {
            return a.fileName < b.fileName;
        }
        return a.chunkIndex < b.chunkIndex;
    });

    QVector<RagHit> hits;
    QHash<QString, int> fileCounts;
    int maxPerFile = (intent == RetrievalIntent::DocumentGeneration || intent == RetrievalIntent::Architecture) ? 4 : 3;
    if (!pathFilter.isEmpty()) {
        maxPerFile = qMax(maxPerFile, limit);
    }
    for (const RagHit &candidate : ranked) {
        const QString fileKey = candidate.filePath;
        if (fileCounts.value(fileKey) >= maxPerFile) {
            continue;
        }
        hits.push_back(candidate);
        fileCounts[fileKey] += 1;
        if (hits.size() >= limit) {
            break;
        }
    }

    return hits;
}

QString RagIndexer::formatHitsForPrompt(const QVector<RagHit> &hits) const
{
    QStringList lines;
    for (const RagHit &hit : hits) {
        // Explicit source attribution on its own line so the model can cite
        // the file name in its answer rather than guessing.
        const QString promptText = hit.chunkText.trimmed().isEmpty() ? hit.excerpt : hit.chunkText;
        lines << QStringLiteral("--- Source: %1 | role=%2 | type=%3 | chunk=%4 | rerank=%5 ---\n%6")
                     .arg(hit.fileName,
                          hit.sourceRole,
                          hit.sourceType,
                          QString::number(hit.chunkIndex),
                          QString::number(hit.rerankScore, 'f', 2),
                          promptText);
    }
    return lines.join(QStringLiteral("\n\n"));
}

QString RagIndexer::formatHitsForUi(const QVector<RagHit> &hits) const
{
    if (hits.isEmpty()) {
        return QStringLiteral("<none>");
    }

    QStringList lines;
    for (const RagHit &hit : hits) {
        lines << QStringLiteral("File: %1").arg(hit.filePath);
        lines << QStringLiteral("Role: %1 | Type: %2").arg(hit.sourceRole, hit.sourceType);
        lines << QStringLiteral("Chunk: %1 | Rerank: %2 | Lexical: %3 | Embedding: %4")
                     .arg(hit.chunkIndex)
                     .arg(QString::number(hit.rerankScore, 'f', 2))
                     .arg(QString::number(hit.lexicalScore, 'f', 2))
                     .arg(QString::number(hit.semanticScore, 'f', 2));
        lines << QStringLiteral("Reason: %1").arg(hit.matchReason);
        lines << QStringLiteral("Excerpt: %1").arg(hit.excerpt);
        lines << QString();
    }
    return lines.join(QStringLiteral("\n"));
}

QString RagIndexer::embeddingBackendName() const
{
    return m_embeddingClient.backendName();
}

QString RagIndexer::formatInventoryForUi() const
{
    if (m_sources.isEmpty()) {
        return QStringLiteral("<none>");
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("amelia-kb-inventory-v5"));
    root.insert(QStringLiteral("knowledgeRoot"), m_docsRoot);
    root.insert(QStringLiteral("collectionsRoot"), collectionsRootFor(m_docsRoot));
    root.insert(QStringLiteral("workspaceJailRoot"), QFileInfo(m_docsRoot).dir().absolutePath());
    root.insert(QStringLiteral("sources"), m_sources.size());
    root.insert(QStringLiteral("chunks"), m_chunks.size());
    qint64 totalBytes = 0;
    for (const SourceInfo &source : m_sources) {
        totalBytes += source.fileSizeBytes;
    }
    root.insert(QStringLiteral("totalBytes"), static_cast<double>(totalBytes));
    root.insert(QStringLiteral("semanticEnabled"), m_semanticEnabled);
    root.insert(QStringLiteral("embeddingBackend"), m_embeddingClient.backendName());
    root.insert(QStringLiteral("chunkingStrategy"), chunkingStrategyName());

    QJsonArray collections;
    QHash<QString, int> collectionIndexes;
    QHash<QString, QHash<QString, int>> groupIndexesByCollection;

    for (const SourceInfo &source : m_sources) {
        const QString collectionId = source.collectionId.trimmed().isEmpty() ? QStringLiteral("legacy") : source.collectionId;
        const QString collectionLabel = source.collectionLabel.trimmed().isEmpty() ? QStringLiteral("Legacy imports") : source.collectionLabel;
        int collectionIndex = collectionIndexes.value(collectionId, -1);
        if (collectionIndex < 0) {
            QJsonObject collection;
            collection.insert(QStringLiteral("collectionId"), collectionId);
            collection.insert(QStringLiteral("label"), collectionLabel);
            collection.insert(QStringLiteral("fileCount"), 0);
            collection.insert(QStringLiteral("chunkCount"), 0);
            collection.insert(QStringLiteral("totalBytes"), 0.0);
            collection.insert(QStringLiteral("groups"), QJsonArray());
            collections.push_back(collection);
            collectionIndex = collections.size() - 1;
            collectionIndexes.insert(collectionId, collectionIndex);
        }

        QJsonObject collection = collections.at(collectionIndex).toObject();
        QJsonArray groups = collection.value(QStringLiteral("groups")).toArray();

        const QString groupId = source.groupId.trimmed().isEmpty()
                ? stableHashHex(collectionId + QStringLiteral("|") + source.groupLabel)
                : source.groupId;
        const QString groupLabel = source.groupLabel.trimmed().isEmpty()
                ? QStringLiteral("(root)")
                : source.groupLabel;

        int groupIndex = groupIndexesByCollection[collectionId].value(groupId, -1);
        if (groupIndex < 0) {
            QJsonObject group;
            group.insert(QStringLiteral("groupId"), groupId);
            group.insert(QStringLiteral("label"), groupLabel);
            group.insert(QStringLiteral("fileCount"), 0);
            group.insert(QStringLiteral("chunkCount"), 0);
            group.insert(QStringLiteral("totalBytes"), 0.0);
            group.insert(QStringLiteral("files"), QJsonArray());
            groups.push_back(group);
            groupIndex = groups.size() - 1;
            groupIndexesByCollection[collectionId].insert(groupId, groupIndex);
        }

        QJsonObject group = groups.at(groupIndex).toObject();
        QJsonArray files = group.value(QStringLiteral("files")).toArray();

        QJsonObject file;
        file.insert(QStringLiteral("filePath"), source.filePath);
        file.insert(QStringLiteral("fileName"), source.fileName);
        file.insert(QStringLiteral("relativePath"), source.relativePath);
        file.insert(QStringLiteral("originalPath"), source.originalPath);
        file.insert(QStringLiteral("sourceRole"), source.sourceRole);
        file.insert(QStringLiteral("sourceType"), source.sourceType);
        file.insert(QStringLiteral("extractor"), source.extractor);
        file.insert(QStringLiteral("chunkCount"), source.chunkCount);
        file.insert(QStringLiteral("fileSizeBytes"), static_cast<double>(source.fileSizeBytes));
        file.insert(QStringLiteral("extension"), QFileInfo(source.fileName).suffix().toLower());
        files.push_back(file);

        group.insert(QStringLiteral("files"), files);
        group.insert(QStringLiteral("fileCount"), files.size());
        group.insert(QStringLiteral("chunkCount"), group.value(QStringLiteral("chunkCount")).toInt() + source.chunkCount);
        group.insert(QStringLiteral("totalBytes"), group.value(QStringLiteral("totalBytes")).toDouble() + static_cast<double>(source.fileSizeBytes));
        groups.replace(groupIndex, group);

        collection.insert(QStringLiteral("groups"), groups);
        collection.insert(QStringLiteral("fileCount"), collection.value(QStringLiteral("fileCount")).toInt() + 1);
        collection.insert(QStringLiteral("chunkCount"), collection.value(QStringLiteral("chunkCount")).toInt() + source.chunkCount);
        collection.insert(QStringLiteral("totalBytes"), collection.value(QStringLiteral("totalBytes")).toDouble() + static_cast<double>(source.fileSizeBytes));
        collections.replace(collectionIndex, collection);
    }

    root.insert(QStringLiteral("collections"), collections);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

int RagIndexer::chunkCount() const
{
    return m_chunks.size();
}

int RagIndexer::sourceCount() const
{
    return m_sources.size();
}

QStringList RagIndexer::supportedExtensions() const
{
    return extensions();
}

int RagIndexer::importPaths(const QStringList &paths, const QString &destinationRoot, const QString &label, QString *message) const
{
    if (paths.isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("No knowledge assets were selected for import.");
        }
        return 0;
    }

    QString normalizedLabel = label.trimmed();
    if (normalizedLabel.isEmpty()) {
        normalizedLabel = QFileInfo(paths.constFirst()).completeBaseName().trimmed();
        if (normalizedLabel.isEmpty()) {
            normalizedLabel = QFileInfo(paths.constFirst()).fileName().trimmed();
        }
        if (normalizedLabel.isEmpty()) {
            normalizedLabel = QStringLiteral("Imported collection");
        }
    }

    QVector<ManifestCollection> collections = loadManifestCollections(destinationRoot);
    if (labelExistsInManifest(collections, normalizedLabel)) {
        if (message != nullptr) {
            *message = QStringLiteral("Knowledge Base label '%1' already exists. Choose a unique label.").arg(normalizedLabel);
        }
        return 0;
    }

    QStringList canonicalInputs;
    for (const QString &path : paths) {
        const QString canonicalPath = canonicalPathFor(path);
        if (!canonicalPath.isEmpty() && !canonicalInputs.contains(canonicalPath)) {
            canonicalInputs.push_back(canonicalPath);
        }
    }
    canonicalInputs.sort();

    const QString createdAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const QString collectionId = stableHashHex(normalizedLabel + QStringLiteral("|") + createdAt + QStringLiteral("|") + canonicalInputs.join(QStringLiteral("|")));
    const QString collectionRoot = QDir(collectionsRootFor(destinationRoot)).filePath(collectionId);

    QDir().mkpath(collectionRoot);

    QVector<ManifestEntry> entries;
    QSet<QString> usedRelativePaths;
    int copied = 0;
    for (const QString &path : paths) {
        copied += importPathIntoCollection(path, collectionId, collectionRoot, &entries, &usedRelativePaths);
    }

    if (copied <= 0 || entries.isEmpty()) {
        QDir(collectionRoot).removeRecursively();
        if (message != nullptr) {
            *message = QStringLiteral("No supported files were imported into the Knowledge Base.");
        }
        return 0;
    }

    ManifestCollection collection;
    collection.collectionId = collectionId;
    collection.label = normalizedLabel;
    collection.createdAt = createdAt;
    collection.entries = entries;
    collections.push_back(collection);

    if (!saveManifestCollections(destinationRoot, collections)) {
        QDir(collectionRoot).removeRecursively();
        if (message != nullptr) {
            *message = QStringLiteral("Failed to update the Knowledge Base manifest for label '%1'.").arg(normalizedLabel);
        }
        return 0;
    }

    if (message != nullptr) {
        *message = QStringLiteral("Imported %1 file(s) into Knowledge Base label '%2'.").arg(copied).arg(normalizedLabel);
    }
    return copied;
}

int RagIndexer::removeKnowledgePaths(const QStringList &paths, const QString &destinationRoot, QString *message) const
{
    if (paths.isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("No Knowledge Base assets were selected.");
        }
        return 0;
    }

    QVector<ManifestCollection> collections = loadManifestCollections(destinationRoot);
    const QString canonicalRoot = canonicalPathFor(destinationRoot);
    const QString canonicalCollectionsRoot = canonicalPathFor(collectionsRootFor(destinationRoot));

    QSet<QString> removedCanonicalPaths;
    int removed = 0;
    for (const QString &path : paths) {
        const QString canonicalPath = canonicalPathFor(path);
        if (canonicalPath.isEmpty() || canonicalRoot.isEmpty()) {
            continue;
        }
        if (!(canonicalPath == canonicalRoot || canonicalPath.startsWith(canonicalRoot + QDir::separator()))) {
            continue;
        }
        if (QFile::remove(canonicalPath)) {
            ++removed;
            removedCanonicalPaths.insert(canonicalPath);
            if (!canonicalCollectionsRoot.isEmpty()) {
                pruneEmptyKnowledgeDirectories(canonicalPath, canonicalCollectionsRoot);
            }
        }
    }

    if (!removedCanonicalPaths.isEmpty()) {
        QVector<ManifestCollection> filteredCollections;
        filteredCollections.reserve(collections.size());
        for (const ManifestCollection &collection : collections) {
            ManifestCollection filtered = collection;
            filtered.entries.clear();
            for (const ManifestEntry &entry : collection.entries) {
                if (!removedCanonicalPaths.contains(canonicalPathFor(entry.internalPath))) {
                    filtered.entries.push_back(entry);
                }
            }
            if (!filtered.entries.isEmpty()) {
                filteredCollections.push_back(filtered);
            }
        }
        saveManifestCollections(destinationRoot, filteredCollections);
    }

    if (message != nullptr) {
        *message = removed > 0
                ? QStringLiteral("Removed %1 Knowledge Base asset(s).").arg(removed)
                : QStringLiteral("No Knowledge Base assets were removed.");
    }
    return removed;
}


int RagIndexer::moveKnowledgePaths(const QStringList &paths,
                                   const QString &destinationRoot,
                                   const QString &targetCollectionId,
                                   const QString &targetGroupLabel,
                                   QString *message) const
{
    if (paths.isEmpty() || targetCollectionId.trimmed().isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("Move request is incomplete.");
        }
        return 0;
    }

    QVector<ManifestCollection> collections = loadManifestCollections(destinationRoot);
    int targetCollectionIndex = -1;
    for (int i = 0; i < collections.size(); ++i) {
        if (collections.at(i).collectionId == targetCollectionId) {
            targetCollectionIndex = i;
            break;
        }
    }
    if (targetCollectionIndex < 0) {
        if (message != nullptr) {
            *message = QStringLiteral("Destination Knowledge Base collection was not found.");
        }
        return 0;
    }

    QSet<QString> selectedPaths;
    for (const QString &path : paths) {
        const QString canonical = canonicalPathFor(path);
        if (!canonical.isEmpty()) {
            selectedPaths.insert(canonical);
        }
    }
    if (selectedPaths.isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("No valid Knowledge Base assets were selected for moving.");
        }
        return 0;
    }

    QString normalizedTargetGroupLabel = targetGroupLabel.trimmed();
    if (normalizedTargetGroupLabel == QStringLiteral("(root)")) {
        normalizedTargetGroupLabel.clear();
    }
    if (!normalizedTargetGroupLabel.isEmpty()) {
        normalizedTargetGroupLabel = QDir::cleanPath(QDir::fromNativeSeparators(normalizedTargetGroupLabel));
    }

    QSet<QString> targetUsedRelativePaths;
    for (const ManifestEntry &entry : collections.at(targetCollectionIndex).entries) {
        targetUsedRelativePaths.insert(entry.relativePath);
    }
    for (const ManifestEntry &entry : collections.at(targetCollectionIndex).entries) {
        if (selectedPaths.contains(canonicalPathFor(entry.internalPath))) {
            targetUsedRelativePaths.remove(entry.relativePath);
        }
    }

    const QString targetCollectionRoot = QDir(collectionsRootFor(destinationRoot)).filePath(targetCollectionId);
    QDir().mkpath(targetCollectionRoot);

    QVector<ManifestEntry> movedEntries;
    QStringList movedSourcePaths;
    int moved = 0;
    int failed = 0;

    for (ManifestCollection &collection : collections) {
        QVector<ManifestEntry> remainingEntries;
        remainingEntries.reserve(collection.entries.size());

        for (const ManifestEntry &entry : std::as_const(collection.entries)) {
            const QString canonicalInternalPath = canonicalPathFor(entry.internalPath);
            if (!selectedPaths.contains(canonicalInternalPath)) {
                remainingEntries.push_back(entry);
                continue;
            }

            const QFileInfo entryInfo(entry.internalPath);
            QString desiredRelativePath = normalizedTargetGroupLabel.isEmpty()
                    ? entry.relativePath
                    : QDir(normalizedTargetGroupLabel).filePath(entryInfo.fileName());
            if (desiredRelativePath.trimmed().isEmpty()) {
                desiredRelativePath = entryInfo.fileName();
            }
            desiredRelativePath = QDir::cleanPath(QDir::fromNativeSeparators(desiredRelativePath));

            if (collection.collectionId == targetCollectionId && entry.relativePath == desiredRelativePath) {
                remainingEntries.push_back(entry);
                continue;
            }

            const QString uniqueRelativePath = ensureUniqueRelativePath(desiredRelativePath, &targetUsedRelativePaths);
            const QString destinationPath = QDir(targetCollectionRoot).filePath(uniqueRelativePath);
            if (!moveStoredKnowledgeFile(entry.internalPath, destinationPath)) {
                remainingEntries.push_back(entry);
                ++failed;
                continue;
            }

            ManifestEntry movedEntry = entry;
            movedEntry.internalPath = canonicalPathFor(destinationPath);
            movedEntry.relativePath = uniqueRelativePath;
            movedEntry.groupLabel = groupLabelFromRelativePath(uniqueRelativePath);
            movedEntry.groupId = stableHashHex(targetCollectionId + QStringLiteral("|") + movedEntry.groupLabel);
            movedEntries.push_back(movedEntry);
            movedSourcePaths.push_back(canonicalInternalPath);
            ++moved;
        }

        collection.entries = remainingEntries;
    }

    if (movedEntries.isEmpty()) {
        if (message != nullptr) {
            *message = failed > 0
                    ? QStringLiteral("Failed to move the selected Knowledge Base asset(s).")
                    : QStringLiteral("No Knowledge Base assets were moved.");
        }
        return 0;
    }

    collections[targetCollectionIndex].entries += movedEntries;

    QVector<ManifestCollection> filteredCollections;
    filteredCollections.reserve(collections.size());
    for (const ManifestCollection &collection : std::as_const(collections)) {
        if (!collection.entries.isEmpty()) {
            filteredCollections.push_back(collection);
        }
    }

    if (!saveManifestCollections(destinationRoot, filteredCollections)) {
        if (message != nullptr) {
            *message = QStringLiteral("Failed to persist the Knowledge Base move operation.");
        }
        return 0;
    }

    const QString canonicalCollectionsRoot = canonicalPathFor(collectionsRootFor(destinationRoot));
    for (const QString &path : std::as_const(movedSourcePaths)) {
        if (!canonicalCollectionsRoot.isEmpty()) {
            pruneEmptyKnowledgeDirectories(path, canonicalCollectionsRoot);
        }
    }

    QString destinationLabel = targetCollectionId;
    for (const ManifestCollection &collection : std::as_const(filteredCollections)) {
        if (collection.collectionId == targetCollectionId) {
            destinationLabel = collection.label;
            break;
        }
    }

    if (message != nullptr) {
        *message = failed > 0
                ? QStringLiteral("Moved %1 Knowledge Base asset(s) to '%2' (%3 failed).")
                      .arg(moved)
                      .arg(destinationLabel)
                      .arg(failed)
                : QStringLiteral("Moved %1 Knowledge Base asset(s) to '%2'.")
                      .arg(moved)
                      .arg(destinationLabel);
    }

    return moved;
}


bool RagIndexer::clearKnowledgeLibrary(const QString &destinationRoot, QString *message) const
{
    QDir rootDir(destinationRoot);
    const bool existed = rootDir.exists();
    const bool removed = !existed || rootDir.removeRecursively();
    QDir().mkpath(destinationRoot);
    QDir().mkpath(collectionsRootFor(destinationRoot));

    if (message != nullptr) {
        *message = removed
                ? QStringLiteral("Knowledge Base cleared.")
                : QStringLiteral("Failed to clear the Knowledge Base.");
    }
    return removed;
}


bool RagIndexer::renameCollectionLabel(const QString &destinationRoot, const QString &collectionId, const QString &newLabel, QString *message)
{
    const QString normalizedLabel = newLabel.trimmed();
    if (collectionId.trimmed().isEmpty() || normalizedLabel.isEmpty()) {
        if (message != nullptr) {
            *message = QStringLiteral("Collection id or label is empty.");
        }
        return false;
    }

    QVector<ManifestCollection> collections = loadManifestCollections(destinationRoot);
    if (labelExistsInManifest(collections, normalizedLabel, collectionId)) {
        if (message != nullptr) {
            *message = QStringLiteral("Knowledge Base label '%1' already exists. Choose a unique label.").arg(normalizedLabel);
        }
        return false;
    }

    bool updated = false;
    for (ManifestCollection &collection : collections) {
        if (collection.collectionId == collectionId) {
            collection.label = normalizedLabel;
            updated = true;
            break;
        }
    }

    if (!updated) {
        if (message != nullptr) {
            *message = QStringLiteral("Knowledge Base collection was not found.");
        }
        return false;
    }

    if (!saveManifestCollections(destinationRoot, collections)) {
        if (message != nullptr) {
            *message = QStringLiteral("Failed to persist the updated Knowledge Base label.");
        }
        return false;
    }

    for (SourceInfo &source : m_sources) {
        if (source.collectionId == collectionId) {
            source.collectionLabel = normalizedLabel;
        }
    }

    if (message != nullptr) {
        *message = QStringLiteral("Renamed Knowledge Base label to '%1'.").arg(normalizedLabel);
    }
    return true;
}

