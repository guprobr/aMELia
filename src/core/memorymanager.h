#pragma once

#include <QString>
#include <QVector>

#include "core/storagemanager.h"

class StorageManager;

class MemoryManager {
public:
    explicit MemoryManager(StorageManager *storage = nullptr);

    void setStorage(StorageManager *storage);

    QVector<MemoryRecord> loadAll(QString *errorMessage = nullptr) const;
    QVector<MemoryRecord> findRelevant(const QString &query, int limit = 6) const;
    QVector<MemoryRecord> findRelevantForPrompt(const QString &query, int limit = 6) const;
    QString formatForPrompt(const QVector<MemoryRecord> &memories) const;
    QString formatForUi(const QVector<MemoryRecord> &memories) const;
    QString formatForUiJson(const QVector<MemoryRecord> &memories) const;

    bool saveExplicitNote(const QString &text, QString *savedDescription = nullptr, QString *errorMessage = nullptr) const;
    bool deleteMemoryById(const QString &memoryId, QString *deletedDescription = nullptr, QString *errorMessage = nullptr) const;
    bool clearAll(QString *errorMessage = nullptr) const;
    QVector<MemoryRecord> extractAutoMemories(const QString &userText) const;
    QString persistAutoMemories(const QString &userText, QString *errorMessage = nullptr) const;

private:
    static QString normalizeKey(const QString &text);
    static int scoreMemory(const MemoryRecord &memory, const QStringList &terms);
    static QString normalizedCandidateValue(QString text);
    static bool isPromptSafeCandidate(const QString &value);
    static bool isPromptSafeMemory(const MemoryRecord &memory, const QString &query);

    StorageManager *m_storage = nullptr;
};
