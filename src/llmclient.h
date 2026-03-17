#pragma once

#include <QObject>
#include <QString>
#include <QVector>

struct LlmChatMessage {
    QString role;
    QString content;
};

class LlmClient : public QObject {
    Q_OBJECT
public:
    explicit LlmClient(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~LlmClient() override = default;

    virtual void generate(const QString &baseUrl,
                          const QString &model,
                          const QVector<LlmChatMessage> &messages) = 0;

    virtual void stop() = 0;

signals:
    void responseStarted();
    void responseDelta(const QString &text);
    void responseFinished(const QString &fullText);
    void responseError(const QString &message);
};
