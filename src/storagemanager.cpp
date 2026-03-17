#include "storagemanager.h"

#include <algorithm>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace {
QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QJsonObject messageToJson(const StoredMessage &message)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("role"), message.role);
    obj.insert(QStringLiteral("content"), message.content);
    obj.insert(QStringLiteral("createdAt"), message.createdAt);
    return obj;
}

StoredMessage messageFromJson(const QJsonObject &obj)
{
    StoredMessage message;
    message.role = obj.value(QStringLiteral("role")).toString();
    message.content = obj.value(QStringLiteral("content")).toString();
    message.createdAt = obj.value(QStringLiteral("createdAt")).toString();
    return message;
}

QJsonObject memoryToJson(const MemoryRecord &memory)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), memory.id);
    obj.insert(QStringLiteral("key"), memory.key);
    obj.insert(QStringLiteral("value"), memory.value);
    obj.insert(QStringLiteral("category"), memory.category);
    obj.insert(QStringLiteral("createdAt"), memory.createdAt);
    obj.insert(QStringLiteral("updatedAt"), memory.updatedAt);
    obj.insert(QStringLiteral("confidence"), memory.confidence);
    obj.insert(QStringLiteral("pinned"), memory.pinned);
    return obj;
}

MemoryRecord memoryFromJson(const QJsonObject &obj)
{
    MemoryRecord memory;
    memory.id = obj.value(QStringLiteral("id")).toString();
    memory.key = obj.value(QStringLiteral("key")).toString();
    memory.value = obj.value(QStringLiteral("value")).toString();
    memory.category = obj.value(QStringLiteral("category")).toString();
    memory.createdAt = obj.value(QStringLiteral("createdAt")).toString();
    memory.updatedAt = obj.value(QStringLiteral("updatedAt")).toString();
    memory.confidence = obj.value(QStringLiteral("confidence")).toDouble(1.0);
    memory.pinned = obj.value(QStringLiteral("pinned")).toBool(false);
    return memory;
}
}

bool StorageManager::initialize(const QString &dataRoot, const QString &knowledgeRoot, QString *errorMessage)
{
    m_dataRoot = QDir::cleanPath(QDir::fromNativeSeparators(dataRoot.trimmed()));
    if (m_dataRoot.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Data root is empty.");
        }
        return false;
    }

    m_conversationsRoot = QDir(m_dataRoot).filePath(QStringLiteral("conversations"));
    m_knowledgeRoot = knowledgeRoot.trimmed().isEmpty()
            ? QDir(m_dataRoot).filePath(QStringLiteral("knowledge"))
            : QDir::cleanPath(QDir::fromNativeSeparators(knowledgeRoot.trimmed()));

    for (const QString &dirPath : {m_dataRoot, m_conversationsRoot, m_knowledgeRoot}) {
        QDir dir;
        if (!dir.mkpath(dirPath)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Failed to create directory: %1").arg(QDir::toNativeSeparators(dirPath));
            }
            return false;
        }
    }

    if (!QFile::exists(conversationsIndexPath())
        && !writeJsonFile(conversationsIndexPath(), QJsonDocument(QJsonArray()).toJson(QJsonDocument::Indented), errorMessage)) {
        return false;
    }

    if (!QFile::exists(memoriesPath())
        && !writeJsonFile(memoriesPath(), QJsonDocument(QJsonArray()).toJson(QJsonDocument::Indented), errorMessage)) {
        return false;
    }

    if (!QFile::exists(statePath())) {
        QJsonObject state;
        state.insert(QStringLiteral("lastConversationId"), QString());
        if (!writeJsonFile(statePath(), QJsonDocument(state).toJson(QJsonDocument::Indented), errorMessage)) {
            return false;
        }
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}

QString StorageManager::dataRoot() const
{
    return m_dataRoot;
}

QString StorageManager::conversationsRoot() const
{
    return m_conversationsRoot;
}

QString StorageManager::knowledgeRoot() const
{
    return m_knowledgeRoot;
}

QString StorageManager::ragCachePath() const
{
    return QDir(m_dataRoot).filePath(QStringLiteral("rag_cache.json"));
}

QString StorageManager::createConversation(const QString &title, QString *errorMessage)
{
    ConversationRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.title = title.trimmed().isEmpty() ? QStringLiteral("Untitled conversation") : title.trimmed();
    record.createdAt = nowIso();
    record.updatedAt = record.createdAt;

    if (!saveConversation(record, errorMessage)) {
        return QString();
    }

    if (!setLastConversationId(record.id, errorMessage)) {
        return QString();
    }

    return record.id;
}

bool StorageManager::conversationExists(const QString &conversationId) const
{
    return QFile::exists(conversationFilePath(conversationId));
}

QVector<ConversationRecord> StorageManager::listConversations() const
{
    return readConversationIndex(nullptr);
}

ConversationRecord StorageManager::loadConversation(const QString &conversationId, QString *errorMessage) const
{
    ConversationRecord record;
    if (conversationId.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Conversation id is empty.");
        }
        return record;
    }

    QFile file(conversationFilePath(conversationId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to open conversation file: %1").arg(QDir::toNativeSeparators(file.fileName()));
        }
        return record;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Invalid conversation JSON: %1").arg(QDir::toNativeSeparators(file.fileName()));
        }
        return record;
    }

    const QJsonObject obj = doc.object();
    record.id = obj.value(QStringLiteral("id")).toString(conversationId);
    record.title = obj.value(QStringLiteral("title")).toString(QStringLiteral("Untitled conversation"));
    record.createdAt = obj.value(QStringLiteral("createdAt")).toString();
    record.updatedAt = obj.value(QStringLiteral("updatedAt")).toString();
    record.summary = obj.value(QStringLiteral("summary")).toString();

    const QJsonArray messages = obj.value(QStringLiteral("messages")).toArray();
    record.messages.reserve(messages.size());
    for (const QJsonValue &value : messages) {
        if (value.isObject()) {
            record.messages.push_back(messageFromJson(value.toObject()));
        }
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return record;
}

bool StorageManager::saveConversation(const ConversationRecord &record, QString *errorMessage)
{
    if (record.id.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Conversation id is empty.");
        }
        return false;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("id"), record.id);
    obj.insert(QStringLiteral("title"), record.title.trimmed().isEmpty() ? QStringLiteral("Untitled conversation") : record.title.trimmed());
    obj.insert(QStringLiteral("createdAt"), record.createdAt.isEmpty() ? nowIso() : record.createdAt);
    obj.insert(QStringLiteral("updatedAt"), record.updatedAt.isEmpty() ? nowIso() : record.updatedAt);
    obj.insert(QStringLiteral("summary"), record.summary);

    QJsonArray messages;
    for (const StoredMessage &message : record.messages) {
        messages.push_back(messageToJson(message));
    }
    obj.insert(QStringLiteral("messages"), messages);

    if (!writeJsonFile(conversationFilePath(record.id), QJsonDocument(obj).toJson(QJsonDocument::Indented), errorMessage)) {
        return false;
    }

    QVector<ConversationRecord> records = readConversationIndex(errorMessage);
    bool found = false;
    for (ConversationRecord &item : records) {
        if (item.id == record.id) {
            item.title = obj.value(QStringLiteral("title")).toString();
            item.createdAt = obj.value(QStringLiteral("createdAt")).toString();
            item.updatedAt = obj.value(QStringLiteral("updatedAt")).toString();
            found = true;
            break;
        }
    }

    if (!found) {
        ConversationRecord indexRecord;
        indexRecord.id = record.id;
        indexRecord.title = obj.value(QStringLiteral("title")).toString();
        indexRecord.createdAt = obj.value(QStringLiteral("createdAt")).toString();
        indexRecord.updatedAt = obj.value(QStringLiteral("updatedAt")).toString();
        records.push_back(indexRecord);
    }

    std::sort(records.begin(), records.end(), [](const ConversationRecord &a, const ConversationRecord &b) {
        return a.updatedAt > b.updatedAt;
    });

    return writeConversationIndex(records, errorMessage);
}

bool StorageManager::appendMessage(const QString &conversationId, const StoredMessage &message, QString *errorMessage)
{
    ConversationRecord record = loadConversation(conversationId, errorMessage);
    if (record.id.isEmpty()) {
        return false;
    }

    record.messages.push_back(message);
    record.updatedAt = message.createdAt.isEmpty() ? nowIso() : message.createdAt;
    return saveConversation(record, errorMessage);
}

bool StorageManager::updateSummary(const QString &conversationId, const QString &summary, QString *errorMessage)
{
    ConversationRecord record = loadConversation(conversationId, errorMessage);
    if (record.id.isEmpty()) {
        return false;
    }

    record.summary = summary;
    record.updatedAt = nowIso();
    return saveConversation(record, errorMessage);
}

bool StorageManager::renameConversation(const QString &conversationId, const QString &title, QString *errorMessage)
{
    ConversationRecord record = loadConversation(conversationId, errorMessage);
    if (record.id.isEmpty()) {
        return false;
    }

    if (!title.trimmed().isEmpty()) {
        record.title = title.trimmed();
    }
    record.updatedAt = nowIso();
    return saveConversation(record, errorMessage);
}

QString StorageManager::lastConversationId() const
{
    QFile file(statePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return QString();
    }
    return doc.object().value(QStringLiteral("lastConversationId")).toString();
}

bool StorageManager::setLastConversationId(const QString &conversationId, QString *errorMessage)
{
    QJsonObject state;
    state.insert(QStringLiteral("lastConversationId"), conversationId);
    return writeJsonFile(statePath(), QJsonDocument(state).toJson(QJsonDocument::Indented), errorMessage);
}

QVector<MemoryRecord> StorageManager::loadMemories(QString *errorMessage) const
{
    QVector<MemoryRecord> memories;
    QFile file(memoriesPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to open memories file: %1").arg(QDir::toNativeSeparators(file.fileName()));
        }
        return memories;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Invalid memories JSON: %1").arg(QDir::toNativeSeparators(file.fileName()));
        }
        return memories;
    }

    const QJsonArray array = doc.array();
    memories.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (value.isObject()) {
            memories.push_back(memoryFromJson(value.toObject()));
        }
    }

    std::sort(memories.begin(), memories.end(), [](const MemoryRecord &a, const MemoryRecord &b) {
        if (a.pinned != b.pinned) {
            return a.pinned && !b.pinned;
        }
        return a.updatedAt > b.updatedAt;
    });

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return memories;
}

bool StorageManager::saveMemory(const MemoryRecord &memory, QString *errorMessage)
{
    QVector<MemoryRecord> memories = loadMemories(errorMessage);

    MemoryRecord item = memory;
    const QString now = nowIso();
    if (item.id.trimmed().isEmpty()) {
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    if (item.createdAt.isEmpty()) {
        item.createdAt = now;
    }
    item.updatedAt = now;

    bool replaced = false;
    for (MemoryRecord &existing : memories) {
        if (!item.key.isEmpty() && existing.key == item.key && existing.category == item.category) {
            existing.value = item.value;
            existing.updatedAt = item.updatedAt;
            existing.confidence = item.confidence;
            existing.pinned = item.pinned;
            if (existing.createdAt.isEmpty()) {
                existing.createdAt = item.createdAt;
            }
            replaced = true;
            break;
        }
        if (!item.id.isEmpty() && existing.id == item.id) {
            existing = item;
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        memories.push_back(item);
    }

    QJsonArray array;
    for (const MemoryRecord &entry : memories) {
        array.push_back(memoryToJson(entry));
    }

    return writeJsonFile(memoriesPath(), QJsonDocument(array).toJson(QJsonDocument::Indented), errorMessage);
}

bool StorageManager::clearMemories(QString *errorMessage)
{
    return writeJsonFile(memoriesPath(), QJsonDocument(QJsonArray()).toJson(QJsonDocument::Indented), errorMessage);
}

QString StorageManager::conversationFilePath(const QString &conversationId) const
{
    return QDir(m_conversationsRoot).filePath(QStringLiteral("%1.json").arg(conversationId));
}

QString StorageManager::conversationsIndexPath() const
{
    return QDir(m_dataRoot).filePath(QStringLiteral("conversations_index.json"));
}

QString StorageManager::memoriesPath() const
{
    return QDir(m_dataRoot).filePath(QStringLiteral("memories.json"));
}

QString StorageManager::statePath() const
{
    return QDir(m_dataRoot).filePath(QStringLiteral("state.json"));
}

QVector<ConversationRecord> StorageManager::readConversationIndex(QString *errorMessage) const
{
    QVector<ConversationRecord> records;
    QFile file(conversationsIndexPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to open conversations index: %1").arg(QDir::toNativeSeparators(file.fileName()));
        }
        return records;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Invalid conversations index JSON: %1").arg(QDir::toNativeSeparators(file.fileName()));
        }
        return records;
    }

    const QJsonArray array = doc.array();
    records.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();
        ConversationRecord record;
        record.id = obj.value(QStringLiteral("id")).toString();
        record.title = obj.value(QStringLiteral("title")).toString();
        record.createdAt = obj.value(QStringLiteral("createdAt")).toString();
        record.updatedAt = obj.value(QStringLiteral("updatedAt")).toString();
        if (!record.id.isEmpty()) {
            records.push_back(record);
        }
    }

    std::sort(records.begin(), records.end(), [](const ConversationRecord &a, const ConversationRecord &b) {
        return a.updatedAt > b.updatedAt;
    });

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return records;
}

bool StorageManager::writeConversationIndex(const QVector<ConversationRecord> &records, QString *errorMessage) const
{
    QJsonArray array;
    for (const ConversationRecord &record : records) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), record.id);
        obj.insert(QStringLiteral("title"), record.title);
        obj.insert(QStringLiteral("createdAt"), record.createdAt);
        obj.insert(QStringLiteral("updatedAt"), record.updatedAt);
        array.push_back(obj);
    }
    return writeJsonFile(conversationsIndexPath(), QJsonDocument(array).toJson(QJsonDocument::Indented), errorMessage);
}

bool StorageManager::writeJsonFile(const QString &path, const QByteArray &data, QString *errorMessage) const
{
    QFileInfo info(path);
    QDir().mkpath(info.dir().absolutePath());

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to open file for writing: %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }

    if (file.write(data) < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to write file: %1").arg(QDir::toNativeSeparators(path));
        }
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Failed to commit file: %1").arg(QDir::toNativeSeparators(path));
        }
        return false;
    }

    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    return true;
}
