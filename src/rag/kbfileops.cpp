#include "rag/kbfileops.h"

#include "rag/kbmanifest.h"
#include "rag/sourceclassifier.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

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
