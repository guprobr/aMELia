#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QVector>

#include <atomic>

class QFileInfo;

// Resolves symlinks and normalizes separators/`.`/`..` segments. Used throughout
// Knowledge Base path handling (manifest matching, path-jail checks) so paths that
// point at the same file always compare equal regardless of how they were spelled.
QString canonicalPathFor(const QString &path);

QString manifestPathForRoot(const QString &destinationRoot);
QString collectionsRootFor(const QString &destinationRoot);

QString stableHashHex(const QString &value);

// Streams the file in 256KB chunks (rather than loading it whole) so large PDFs/DOCX
// don't spike memory during cache-invalidation hashing. Returns false on read error or
// cancellation, leaving *hashHex untouched.
bool computeFileContentHash(const QString &path,
                            QString *hashHex,
                            std::atomic_bool *cancelRequested = nullptr);

// Strips a value down to filesystem/URL-safe characters for use as a collection or
// group label component (e.g. in generated directory names).
QString sanitizeLabelComponent(QString value);

QString groupLabelFromRelativePath(const QString &relativePath);

// Appends a numeric suffix if relativePath collides with something already in
// usedRelativePaths, so multiple imported files that would otherwise land on the same
// stored name each get a distinct one.
QString ensureUniqueRelativePath(QString relativePath, QSet<QString> *usedRelativePaths);

// Relative storage path for a file imported without an explicit collection: derived
// from a hash of its parent directory so files from different source folders that
// happen to share a name don't collide.
QString standaloneRelativePathFor(const QFileInfo &info);

// One imported file's identity inside a collection: where it's stored internally, its
// path relative to the collection root, where it came from, and which import batch
// (group) it belongs to.
struct ManifestEntry {
    QString internalPath;
    QString relativePath;
    QString originalPath;
    QString groupId;
    QString groupLabel;
};

// A named, user-visible grouping of imported knowledge (e.g. "Runbooks", "Vendor
// docs"), persisted as one entry in the manifest JSON.
struct ManifestCollection {
    QString collectionId;
    QString label;
    QString createdAt;
    QVector<ManifestEntry> entries;
};

QJsonObject manifestEntryToJson(const ManifestEntry &entry);
ManifestEntry manifestEntryFromJson(const QJsonObject &obj);
QJsonObject manifestCollectionToJson(const ManifestCollection &collection);
ManifestCollection manifestCollectionFromJson(const QJsonObject &obj);

// Reads .amelia_kb_manifest.json from destinationRoot. Returns an empty list (not an
// error) if the manifest doesn't exist yet or fails to parse -- a knowledge root with
// no manifest is just a knowledge root with no collections yet.
QVector<ManifestCollection> loadManifestCollections(const QString &destinationRoot);

// Writes the manifest atomically via QSaveFile (temp file + commit), so a crash or
// power loss mid-write can't leave a corrupt/partial manifest.json behind.
bool saveManifestCollections(const QString &destinationRoot, const QVector<ManifestCollection> &collections);

bool labelExistsInManifest(const QVector<ManifestCollection> &collections,
                           const QString &label,
                           const QString &excludedCollectionId = QString());

// Denormalized per-file view of manifest data (which collection/group a file belongs
// to, its display path), looked up by canonical internal path during indexing so each
// chunk/source can carry its collection context without re-walking the manifest.
struct SourceMetadata {
    QString collectionId;
    QString collectionLabel;
    QString groupId;
    QString groupLabel;
    QString relativePath;
    QString originalPath;
};

QHash<QString, SourceMetadata> buildMetadataByInternalPath(const QVector<ManifestCollection> &collections);
