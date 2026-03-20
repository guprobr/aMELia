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

int chooseChunkEnd(const QString &text, int offset, int targetSize)
{
    const int hardEnd = qMin(offset + targetSize, text.size());
    if (hardEnd >= text.size()) {
        return text.size();
    }

    const int minimumSplit = offset + qMax(900, targetSize / 2);
    int split = text.lastIndexOf(QStringLiteral("\n[[PAGE "), hardEnd);
    if (split >= minimumSplit) {
        return split;
    }

    split = text.lastIndexOf(QStringLiteral("\n\n"), hardEnd);
    if (split >= minimumSplit) {
        return split;
    }

    split = text.lastIndexOf(QLatin1Char('\n'), hardEnd);
    if (split >= minimumSplit) {
        return split;
    }

    return hardEnd;
}

int chooseNextChunkOffset(const QString &text, int chunkEnd, int overlap)
{
    if (chunkEnd >= text.size()) {
        return text.size();
    }

    int nextOffset = qMax(0, chunkEnd - overlap);
    const int nextPageMarker = text.indexOf(QStringLiteral("\n[[PAGE "), nextOffset);
    const int nextBlankBlock = text.indexOf(QStringLiteral("\n\n"), nextOffset);
    if (nextPageMarker >= 0 && nextPageMarker < chunkEnd) {
        nextOffset = nextPageMarker + 1;
    } else if (nextBlankBlock >= 0 && nextBlankBlock < chunkEnd) {
        nextOffset = nextBlankBlock + 2;
    }

    while (nextOffset < text.size() && text.at(nextOffset) == QLatin1Char('\n')) {
        ++nextOffset;
    }
    return nextOffset;
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
    normalized.replace(QRegularExpression(QStringLiteral("[^a-z0-9._+-]+")), QStringLiteral(" "));
    const QStringList parts = normalized.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    static const QSet<QString> stopWords = {
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"), QStringLiteral("with"),
        QStringLiteral("from"), QStringLiteral("into"), QStringLiteral("that"), QStringLiteral("this"),
        QStringLiteral("what"), QStringLiteral("when"), QStringLiteral("where"), QStringLiteral("which"),
        QStringLiteral("your"), QStringLiteral("through"), QStringLiteral("create"), QStringLiteral("write"),
        QStringLiteral("format"), QStringLiteral("whole"), QStringLiteral("deployment")
    };

    QStringList terms;
    QSet<QString> seen;
    for (const QString &part : parts) {
        if (part.size() < 2 || stopWords.contains(part) || seen.contains(part)) {
            continue;
        }
        seen.insert(part);
        terms.push_back(part);
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
            score += 0.9 + qMin(1.1, 0.18 * static_cast<double>(bodyCount - 1));
        }
        if (fileCount > 0) {
            score += 0.35 + qMin(0.6, 0.20 * static_cast<double>(fileCount - 1));
        }
    }

    const double coverage = static_cast<double>(matched) / static_cast<double>(terms.size());
    score += coverage * 1.6;

    const QString queryLower = query.toLower().trimmed();
    if (!queryLower.isEmpty() && lower.contains(queryLower)) {
        score += 1.1;
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

QString matchReason(double lexical, double semantic, const QString &role)
{
    QStringList reasons;
    if (lexical >= 1.5) {
        reasons << QStringLiteral("strong lexical overlap");
    } else if (lexical > 0.0) {
        reasons << QStringLiteral("keyword overlap");
    }
    if (semantic >= 0.55) {
        reasons << QStringLiteral("strong semantic similarity");
    } else if (semantic >= 0.30) {
        reasons << QStringLiteral("semantic match");
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

void RagIndexer::requestCancel()
{
    m_cancelRequested.store(true, std::memory_order_relaxed);
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

    EmbeddingClient embedder;
    for (Chunk &chunk : chunks) {
        if (chunk.embedding.isEmpty()) {
            chunk.embedding = embedder.embedText(chunk.text);
        }
    }
}

bool RagIndexer::sourceMatchesFile(const SourceInfo &source, const QFileInfo &info)
{
    return source.fileModifiedMs == info.lastModified().toMSecsSinceEpoch()
            && source.fileSizeBytes == info.size();
}

int RagIndexer::reindex(const std::function<void(int, int, const QString &)> &progressCallback)
{
    m_cancelRequested.store(false, std::memory_order_relaxed);

    auto cancelEarly = [this, &progressCallback]() -> int {
        if (progressCallback) {
            progressCallback(0, 0, QStringLiteral("Indexing canceled."));
        }
        return m_chunks.size();
    };

    if (m_docsRoot.trimmed().isEmpty()) {
        m_chunks.clear();
        m_sources.clear();
        return 0;
    }

    const QVector<ManifestCollection> manifestCollections = loadManifestCollections(m_docsRoot);
    const QHash<QString, SourceMetadata> metadataByPath = buildMetadataByInternalPath(manifestCollections);

    QStringList paths;
    QDirIterator gatherIt(m_docsRoot, extensions(), QDir::Files, QDirIterator::Subdirectories);
    while (gatherIt.hasNext()) {
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            return cancelEarly();
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

    QVector<Chunk> newChunks;
    QVector<SourceInfo> newSources;
    newSources.reserve(totalFiles);
    int reusedFiles = 0;
    int rebuiltFiles = 0;

    EmbeddingClient embedder;

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

    for (int i = 0; i < totalFiles; ++i) {
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            return cancelEarly();
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
            if (m_semanticEnabled) {
                for (Chunk &chunk : reusedChunks) {
                    if (m_cancelRequested.load(std::memory_order_relaxed)) {
                        return cancelEarly();
                    }
                    if (chunk.embedding.isEmpty()) {
                        chunk.embedding = embedder.embedText(chunk.text);
                    }
                }
            } else {
                for (Chunk &chunk : reusedChunks) {
                    if (m_cancelRequested.load(std::memory_order_relaxed)) {
                        return cancelEarly();
                    }
                    chunk.embedding.clear();
                }
            }

            source.chunkCount = reusedChunks.size();
            newSources.push_back(source);
            for (const Chunk &chunk : reusedChunks) {
                newChunks.push_back(chunk);
            }
            ++reusedFiles;

            if (progressCallback) {
                progressCallback(i + 1,
                                 totalFiles > 0 ? totalFiles : 1,
                                 QStringLiteral("Using cached index %1 / %2: %3").arg(i + 1).arg(totalFiles).arg(info.fileName()));
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

        if (ok && !textValue.trimmed().isEmpty()) {
            constexpr int chunkSize = 2800;
            constexpr int overlap = 420;
            int offset = 0;
            int chunkIndex = 0;
            while (offset < textValue.size()) {
                if (m_cancelRequested.load(std::memory_order_relaxed)) {
                    return cancelEarly();
                }

                const int end = chooseChunkEnd(textValue, offset, chunkSize);
                const QString chunkText = textValue.mid(offset, end - offset).trimmed();
                if (chunkText.isEmpty()) {
                    if (end <= offset) {
                        break;
                    }
                    offset = chooseNextChunkOffset(textValue, end, overlap);
                    continue;
                }

                Chunk chunk;
                chunk.filePath = path;
                chunk.fileName = info.fileName();
                chunk.sourceType = sourceType;
                chunk.sourceRole = sourceRole;
                chunk.text = chunkText;
                chunk.chunkIndex = chunkIndex++;
                chunk.fileModifiedMs = source.fileModifiedMs;
                if (m_semanticEnabled) {
                    if (m_cancelRequested.load(std::memory_order_relaxed)) {
                        return cancelEarly();
                    }
                    chunk.embedding = embedder.embedText(chunk.text);
                }
                newChunks.push_back(chunk);
                ++source.chunkCount;

                if (end >= textValue.size()) {
                    break;
                }
                const int nextOffset = chooseNextChunkOffset(textValue, end, overlap);
                if (nextOffset <= offset) {
                    break;
                }
                offset = nextOffset;
            }
        }

        newSources.push_back(source);
        ++rebuiltFiles;

        if (progressCallback) {
            progressCallback(i + 1,
                             totalFiles > 0 ? totalFiles : 1,
                             QStringLiteral("Indexed %1 / %2: %3").arg(i + 1).arg(totalFiles).arg(info.fileName()));
        }
    }

    m_chunks = std::move(newChunks);
    m_sources = std::move(newSources);

    std::sort(m_sources.begin(), m_sources.end(), [](const SourceInfo &a, const SourceInfo &b) {
        if (a.collectionLabel != b.collectionLabel) {
            return a.collectionLabel.toLower() < b.collectionLabel.toLower();
        }
        if (a.groupLabel != b.groupLabel) {
            return a.groupLabel.toLower() < b.groupLabel.toLower();
        }
        return a.fileName.toLower() < b.fileName.toLower();
    });

    saveCache();

    if (progressCallback) {
        progressCallback(totalFiles > 0 ? totalFiles : 1,
                         totalFiles > 0 ? totalFiles : 1,
                         QStringLiteral("Index ready: %1 files (%2 reused, %3 rebuilt, %4 chunks)")
                             .arg(totalFiles)
                             .arg(reusedFiles)
                             .arg(rebuiltFiles)
                             .arg(m_chunks.size()));
    }

    return m_chunks.size();
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
    if (root.value(QStringLiteral("docsRoot")).toString() != m_docsRoot) {
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
    root.insert(QStringLiteral("docsRoot"), m_docsRoot);
    root.insert(QStringLiteral("semanticEnabled"), m_semanticEnabled);
    root.insert(QStringLiteral("embeddingBackend"), EmbeddingClient().backendName());

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
    const EmbeddingClient embedder;
    const QVector<float> queryEmbedding = m_semanticEnabled ? embedder.embedText(query) : QVector<float>();

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
        const double semantic = m_semanticEnabled ? qMax(0.0f, EmbeddingClient::cosineSimilarity(queryEmbedding, chunk.embedding)) : 0.0;
        const double role = roleBias(intent, chunk.sourceRole, preferredRoles);
        const double finalScore = lexical * 0.55 + semantic * 2.8 + role;
        const double threshold = m_semanticEnabled ? 0.45 : 0.90;
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
        hit.matchReason = matchReason(lexical, semantic, chunk.sourceRole);
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
        lines << QStringLiteral("Chunk: %1 | Rerank: %2 | Lexical: %3 | Semantic: %4")
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

QString RagIndexer::formatInventoryForUi() const
{
    if (m_sources.isEmpty()) {
        return QStringLiteral("<none>");
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("amelia-kb-inventory-v3"));
    root.insert(QStringLiteral("knowledgeRoot"), m_docsRoot);
    root.insert(QStringLiteral("collectionsRoot"), collectionsRootFor(m_docsRoot));
    root.insert(QStringLiteral("workspaceJailRoot"), QFileInfo(m_docsRoot).dir().absolutePath());
    root.insert(QStringLiteral("sources"), m_sources.size());
    root.insert(QStringLiteral("chunks"), m_chunks.size());
    root.insert(QStringLiteral("semanticEnabled"), m_semanticEnabled);

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
        groups.replace(groupIndex, group);

        collection.insert(QStringLiteral("groups"), groups);
        collection.insert(QStringLiteral("fileCount"), collection.value(QStringLiteral("fileCount")).toInt() + 1);
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

