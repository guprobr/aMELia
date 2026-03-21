#pragma once

#include <QString>
#include <QVector>

struct StoredMessage {
    QString role;
    QString content;
    QString createdAt;
};

struct ConversationRecord {
    QString id;
    QString title;
    QString createdAt;
    QString updatedAt;
    QVector<StoredMessage> messages;
    QString summary;
};

struct MemoryRecord {
    QString id;
    QString key;
    QString value;
    QString category;
    QString createdAt;
    QString updatedAt;
    double confidence = 1.0;
    bool pinned = false;
};

class StorageManager {
public:
    bool initialize(const QString &dataRoot, const QString &knowledgeRoot = QString(), QString *errorMessage = nullptr);

    QString dataRoot() const;
    QString conversationsRoot() const;
    QString knowledgeRoot() const;
    QString workspaceRoot() const;
    QString runtimeWorkspaceRoot() const;
    QString ragCachePath() const;
    bool isPathInsideDataRoot(const QString &path) const;
    bool isPathInsideKnowledgeRoot(const QString &path) const;

    QString createConversation(const QString &title, QString *errorMessage = nullptr);
    bool conversationExists(const QString &conversationId) const;
    QVector<ConversationRecord> listConversations() const;
    ConversationRecord loadConversation(const QString &conversationId, QString *errorMessage = nullptr) const;
    bool saveConversation(const ConversationRecord &record, QString *errorMessage = nullptr);
    bool appendMessage(const QString &conversationId, const StoredMessage &message, QString *errorMessage = nullptr);
    bool updateSummary(const QString &conversationId, const QString &summary, QString *errorMessage = nullptr);
    bool renameConversation(const QString &conversationId, const QString &title, QString *errorMessage = nullptr);
    bool deleteConversation(const QString &conversationId, QString *errorMessage = nullptr);

    QString lastConversationId() const;
    bool setLastConversationId(const QString &conversationId, QString *errorMessage = nullptr);

    QVector<MemoryRecord> loadMemories(QString *errorMessage = nullptr) const;
    bool saveMemory(const MemoryRecord &memory, QString *errorMessage = nullptr);
    bool deleteMemoryById(const QString &memoryId, QString *errorMessage = nullptr);
    bool clearMemories(QString *errorMessage = nullptr);

private:
    QString conversationFilePath(const QString &conversationId) const;
    QString conversationsIndexPath() const;
    QString memoriesPath() const;
    QString statePath() const;
    QString canonicalRootPath(const QString &path) const;
    bool isPathInsideRoot(const QString &path, const QString &root) const;

    QVector<ConversationRecord> readConversationIndex(QString *errorMessage = nullptr) const;
    bool writeConversationIndex(const QVector<ConversationRecord> &records, QString *errorMessage = nullptr) const;
    bool writeJsonFile(const QString &path, const QByteArray &data, QString *errorMessage = nullptr) const;

    QString m_dataRoot;
    QString m_conversationsRoot;
    QString m_knowledgeRoot;
    QString m_workspaceRoot;
    QString m_runtimeWorkspaceRoot;
};
