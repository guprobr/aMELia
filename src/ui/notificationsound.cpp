#include "ui/notificationsound.h"

#include <QSoundEffect>
#include <QUrl>

NotificationSound::NotificationSound(const AppConfig &config, QObject *parent)
    : QObject(parent)
    , m_enabled(config.enableNotificationSounds)
{
    m_answerStartEffect = new QSoundEffect(this);
    m_answerStartEffect->setSource(QUrl(QStringLiteral("qrc:/sounds/answer_start.wav")));
    m_answerStartEffect->setVolume(0.5);

    m_answerCompleteEffect = new QSoundEffect(this);
    m_answerCompleteEffect->setSource(QUrl(QStringLiteral("qrc:/sounds/answer_complete.wav")));
    m_answerCompleteEffect->setVolume(0.5);
}

NotificationSound::~NotificationSound() = default;

bool NotificationSound::isEnabled() const
{
    return m_enabled;
}

void NotificationSound::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

void NotificationSound::playAnswerStarted()
{
    if (!m_enabled || m_answerStartEffect == nullptr) {
        return;
    }
    m_answerStartEffect->play();
}

void NotificationSound::playAnswerCompleted()
{
    if (!m_enabled || m_answerCompleteEffect == nullptr) {
        return;
    }
    m_answerCompleteEffect->play();
}
