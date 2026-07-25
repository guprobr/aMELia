#pragma once

#include "core/appconfig.h"

#include <QObject>

class QSoundEffect;

// Plays short bundled chimes to mark the two audible moments in a model
// turn: when visible answer text starts streaming, and when the full answer
// has finished generating. Uses QSoundEffect (Qt Multimedia) so playback
// works the same way on Linux, Windows and macOS without shelling out to any
// platform-specific sound API.
class NotificationSound : public QObject {
    Q_OBJECT
public:
    explicit NotificationSound(const AppConfig &config, QObject *parent = nullptr);
    ~NotificationSound() override;

    bool isEnabled() const;
    void setEnabled(bool enabled);

public slots:
    void playAnswerStarted();
    void playAnswerCompleted();

private:
    QSoundEffect *m_answerStartEffect = nullptr;
    QSoundEffect *m_answerCompleteEffect = nullptr;
    bool m_enabled = true;
};
