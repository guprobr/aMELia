#include "ui/notificationsound.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSoundEffect>
#include <QStandardPaths>

NotificationSound::NotificationSound(const AppConfig &config, QObject *parent)
    : QObject(parent)
    , m_enabled(config.enableNotificationSounds)
{
    m_answerStartEffect = new QSoundEffect(this);
    m_answerStartEffect->setSource(extractSoundToDisk(QStringLiteral(":/sounds/answer_start.wav"),
                                                        QStringLiteral("answer_start.wav")));
    //m_answerStartEffect->setVolume(0.5);

    m_answerCompleteEffect = new QSoundEffect(this);
    m_answerCompleteEffect->setSource(extractSoundToDisk(QStringLiteral(":/sounds/answer_complete.wav"),
                                                           QStringLiteral("answer_complete.wav")));
    //m_answerCompleteEffect->setVolume(0.5);
}

// QSoundEffect's default backend (FFmpeg, both here and on the mainstream
// Windows/macOS Qt distributions) opens its source through FFmpeg's own I/O
// layer, which has no notion of Qt's "qrc:" resource scheme and fails to
// decode it. Bundled chimes are copied out to a real file on disk once so
// QSoundEffect always gets a plain file:// URL it can actually decode.
QUrl NotificationSound::extractSoundToDisk(const QString &qrcPath, const QString &fileName)
{
    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
            + QStringLiteral("/sounds");
    const QString destPath = cacheDir + QLatin1Char('/') + fileName;

    QFileInfo destInfo(destPath);
    QFileInfo srcInfo(qrcPath);
    if (!srcInfo.exists()) {
        qWarning() << "NotificationSound: bundled resource missing:" << qrcPath;
        return QUrl();
    }
    if (!destInfo.exists() || destInfo.size() != srcInfo.size()) {
        QDir().mkpath(cacheDir);
        QFile::remove(destPath);
        if (!QFile::copy(qrcPath, destPath)) {
            qWarning() << "NotificationSound: failed to extract" << qrcPath << "to" << destPath;
            return QUrl();
        }
        QFile::setPermissions(destPath, QFile::ReadOwner | QFile::WriteOwner);
    }

    return QUrl::fromLocalFile(destPath);
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
