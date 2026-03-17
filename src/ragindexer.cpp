#include "ragindexer.h"

#include "embeddingclient.h"

#include <algorithm>

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

QString cleanedText(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("\\r\\n?")), QStringLiteral("\n"));
    text.replace(QRegularExpression(QStringLiteral("[\\x00-\\x08\\x0B\\x0C\\x0E-\\x1F]")), QStringLiteral(" "));
    return text.simplified();
}

bool readTextFile(const QString &path, QString *text, QString *extractor)
{
    QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    if (suffix == QStringLiteral("pdf")) {
        QProcess process;
        process.start(QStringLiteral("pdftotext"), {QStringLiteral("-nopgbrk"), QStringLiteral("-layout"), path, QStringLiteral("-")});
        if (!process.waitForStarted(1500)) {
            if (extractor != nullptr) {
                *extractor = QStringLiteral("pdf:pdftotext-not-found");
            }
            return false;
        }
        process.waitForFinished(30000);
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            if (extractor != nullptr) {
                *extractor = QStringLiteral("pdf:pdftotext-failed");
            }
            return false;
        }
        if (text != nullptr) {
            *text = cleanedText(QString::fromUtf8(process.readAllStandardOutput()));
        }
        if (extractor != nullptr) {
            *extractor = QStringLiteral("pdf:pdftotext");
        }
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    const QByteArray bytes = file.readAll();
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

QString uniqueDestinationFor(const QFileInfo &sourceInfo, const QString &destinationRoot)
{
    const QString baseName = sourceInfo.completeBaseName();
    const QString suffix = sourceInfo.suffix();
    QString candidate = QDir(destinationRoot).filePath(sourceInfo.fileName());
    if (!QFileInfo::exists(candidate)) {
        return candidate;
    }

    const QString stamp = QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_hhmmss_zzz"));
    const QString fileName = suffix.isEmpty()
            ? QStringLiteral("%1_%2").arg(baseName, stamp)
            : QStringLiteral("%1_%2.%3").arg(baseName, stamp, suffix);
    return QDir(destinationRoot).filePath(fileName);
}

int copyPathIntoLibrary(const QString &path, const QString &destinationRoot)
{
    QFileInfo info(path);
    if (!info.exists()) {
        return 0;
    }

    int copied = 0;
    QDir().mkpath(destinationRoot);

    if (info.isFile()) {
        const QString dest = uniqueDestinationFor(info, destinationRoot);
        if (QFile::copy(info.absoluteFilePath(), dest)) {
            ++copied;
        }
        return copied;
    }

    QDirIterator it(info.absoluteFilePath(), extensions(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString sourceFile = it.next();
        QFileInfo sourceInfo(sourceFile);
        QString relative = QDir(info.absoluteFilePath()).relativeFilePath(sourceFile);
        if (relative.startsWith(QStringLiteral("../"))) {
            relative = sourceInfo.fileName();
        }
        const QString dest = QDir(destinationRoot).filePath(relative);
        QDir().mkpath(QFileInfo(dest).dir().absolutePath());
        if (QFileInfo::exists(dest)) {
            QFile::remove(dest);
        }
        if (QFile::copy(sourceFile, dest)) {
            ++copied;
        }
    }

    return copied;
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

void RagIndexer::rebuildEmbeddings()
{
    EmbeddingClient embedder;
    for (Chunk &chunk : m_chunks) {
        chunk.embedding = m_semanticEnabled ? embedder.embedText(chunk.text) : QVector<float>();
    }
}

int RagIndexer::reindex()
{
    m_chunks.clear();
    m_sources.clear();

    if (m_docsRoot.trimmed().isEmpty()) {
        return 0;
    }

    QDirIterator it(m_docsRoot, extensions(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info(path);
        const QString sourceType = detectSourceType(info);
        QString text;
        QString extractor;
        const bool ok = readTextFile(path, &text, &extractor);
        const QString sourceRole = detectSourceRole(info, sourceType, text);

        SourceInfo source;
        source.filePath = path;
        source.fileName = info.fileName();
        source.sourceType = sourceType;
        source.sourceRole = sourceRole;
        source.extractor = extractor.isEmpty() ? QStringLiteral("unknown") : extractor;
        source.fileModifiedMs = info.lastModified().toMSecsSinceEpoch();
        source.fileSizeBytes = info.size();

        if (ok && !text.trimmed().isEmpty()) {
        // Larger chunks keep procedures and code blocks intact.
        // 2800 chars ≈ 500-600 tokens; overlap of 420 ensures boundary sentences
        // are not silently split across adjacent chunks.
        constexpr int chunkSize = 2800;
        constexpr int overlap = 420;
            int offset = 0;
            int chunkIndex = 0;
            while (offset < text.size()) {
                Chunk chunk;
                chunk.filePath = path;
                chunk.fileName = info.fileName();
                chunk.sourceType = sourceType;
                chunk.sourceRole = sourceRole;
                chunk.text = text.mid(offset, chunkSize);
                chunk.chunkIndex = chunkIndex++;
                chunk.fileModifiedMs = source.fileModifiedMs;
                m_chunks.push_back(chunk);
                ++source.chunkCount;

                if (offset + chunkSize >= text.size()) {
                    break;
                }
                offset += (chunkSize - overlap);
            }
        }

        m_sources.push_back(source);
    }

    rebuildEmbeddings();

    std::sort(m_sources.begin(), m_sources.end(), [](const SourceInfo &a, const SourceInfo &b) {
        if (a.sourceRole != b.sourceRole) {
            return a.sourceRole < b.sourceRole;
        }
        if (a.sourceType != b.sourceType) {
            return a.sourceType < b.sourceType;
        }
        return a.fileName < b.fileName;
    });

    saveCache();
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
        if (chunk.filePath.isEmpty() || chunk.text.isEmpty()) {
            continue;
        }
        const QFileInfo info(chunk.filePath);
        if (!info.exists() || info.lastModified().toMSecsSinceEpoch() != chunk.fileModifiedMs) {
            return false;
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
        source.fileModifiedMs = static_cast<qint64>(obj.value(QStringLiteral("fileModifiedMs")).toDouble());
        source.fileSizeBytes = static_cast<qint64>(obj.value(QStringLiteral("fileSizeBytes")).toDouble());
        source.chunkCount = obj.value(QStringLiteral("chunkCount")).toInt();
        if (source.filePath.isEmpty()) {
            continue;
        }
        const QFileInfo info(source.filePath);
        if (!info.exists() || info.lastModified().toMSecsSinceEpoch() != source.fileModifiedMs) {
            return false;
        }
        cachedSources.push_back(source);
    }

    m_chunks = cachedChunks;
    m_sources = cachedSources;
    rebuildEmbeddings();
    return !m_sources.isEmpty() || !m_chunks.isEmpty();
}

bool RagIndexer::saveCache() const
{
    if (m_cachePath.trimmed().isEmpty()) {
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("docsRoot"), m_docsRoot);

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

QVector<RagHit> RagIndexer::searchHits(const QString &query,
                                       int limit,
                                       RetrievalIntent intent,
                                       const QStringList &preferredRoles) const
{
    const QStringList terms = queryTerms(query);
    const EmbeddingClient embedder;
    const QVector<float> queryEmbedding = m_semanticEnabled ? embedder.embedText(query) : QVector<float>();

    QVector<RagHit> ranked;
    ranked.reserve(m_chunks.size());

    for (const Chunk &chunk : m_chunks) {
        const double lexical = lexicalScoreChunk(chunk.text, chunk.fileName, terms, query);
        const double semantic = m_semanticEnabled ? qMax(0.0f, EmbeddingClient::cosineSimilarity(queryEmbedding, chunk.embedding)) : 0.0;
        const double role = roleBias(intent, chunk.sourceRole, preferredRoles);
        const double finalScore = lexical * 0.55 + semantic * 2.8 + role;
        // When semantic retrieval is disabled the semantic score is always 0,
        // so the effective maximum is lexical * 0.55 + role.
        // Use a tighter threshold in lexical-only mode to reduce noise hits.
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
    const int maxPerFile = (intent == RetrievalIntent::DocumentGeneration || intent == RetrievalIntent::Architecture) ? 2 : 3;
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
        lines << QStringLiteral("--- Source: %1 | role=%2 | type=%3 | chunk=%4 | rerank=%5 ---\n%6")
                     .arg(hit.fileName,
                          hit.sourceRole,
                          hit.sourceType,
                          QString::number(hit.chunkIndex),
                          QString::number(hit.rerankScore, 'f', 2),
                          hit.excerpt);
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

    QStringList lines;
    lines << QStringLiteral("Knowledge root: %1").arg(m_docsRoot);
    lines << QStringLiteral("Sources: %1 | Chunks: %2 | Semantic retrieval: %3")
                 .arg(m_sources.size())
                 .arg(m_chunks.size())
                 .arg(m_semanticEnabled ? QStringLiteral("enabled") : QStringLiteral("disabled"));
    lines << QString();
    for (const SourceInfo &source : m_sources) {
        lines << QStringLiteral("File: %1").arg(source.filePath);
        lines << QStringLiteral("Role: %1 | Type: %2 | Extractor: %3").arg(source.sourceRole, source.sourceType, source.extractor);
        lines << QStringLiteral("Chunks: %1 | Size: %2 bytes").arg(source.chunkCount).arg(source.fileSizeBytes);
        lines << QString();
    }
    return lines.join(QStringLiteral("\n"));
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

int RagIndexer::importPaths(const QStringList &paths, const QString &destinationRoot, QString *message) const
{
    int copied = 0;
    for (const QString &path : paths) {
        copied += copyPathIntoLibrary(path, destinationRoot);
    }

    if (message != nullptr) {
        *message = copied > 0
                ? QStringLiteral("Imported %1 file(s) into %2").arg(copied).arg(destinationRoot)
                : QStringLiteral("No supported files were imported.");
    }
    return copied;
}
