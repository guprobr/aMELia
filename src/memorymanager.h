#pragma once

#include <QString>
#include <QVector>

#include "storagemanager.h"

class StorageManager;

class MemoryManager {
public:
    explicit MemoryManager(StorageManager *storage = nullptr);

    void setStorage(StorageManager *storage);

    QVector<MemoryRecord> loadAll(QString *errorMessage = nullptr) const;
    QVector<MemoryRecord> findRelevant(const QString &query, int limit = 6) const;
    QString formatForPrompt(const QVector<MemoryRecord> &memories) const;
    QString formatForUi(const QVector<MemoryRecord> &memories) const;

    bool saveExplicitNote(const QString &text, QString *savedDescription = nullptr, QString *errorMessage = nullptr) const;
    bool clearAll(QString *errorMessage = nullptr) const;
    QVector<MemoryRecord> extractAutoMemories(const QString &userText) const;
    QString persistAutoMemories(const QString &userText, QString *errorMessage = nullptr) const;

private:
    static QString normalizeKey(const QString &text);
    static int scoreMemory(const MemoryRecord &memory, const QStringList &terms);

    StorageManager *m_storage = nullptr;
};
