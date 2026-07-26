#include "rag/kbmanifest.h"

#include "rag/cancellation.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

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

bool computeFileContentHash(const QString &path,
                            QString *hashHex,
                            std::atomic_bool *cancelRequested)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha1);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(256 * 1024);
        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            file.close();
            return false;
        }
        if (!chunk.isEmpty()) {
            hash.addData(chunk);
        }
        if (isCancelRequested(cancelRequested)) {
            file.close();
            return false;
        }
    }
    file.close();

    if (hashHex != nullptr) {
        *hashHex = QString::fromLatin1(hash.result().toHex());
    }
    return true;
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
                           const QString &excludedCollectionId)
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
