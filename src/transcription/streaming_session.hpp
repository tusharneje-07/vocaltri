#pragma once

#include <QObject>

namespace vocaltri::audio {
class AudioRecorder;
}

namespace vocaltri::ui {
class SessionViewModel;
}

namespace vocaltri::input {
class TextInjector;
}

class QElapsedTimer;
class QTimer;

namespace vocaltri::transcription {
class DeepgramClient;

class StreamingSession final : public QObject {
    Q_OBJECT

public:
    explicit StreamingSession(ui::SessionViewModel &viewModel,
                              input::TextInjector &textInjector,
                              QObject *parent = nullptr);

    bool start(const QString &apiKey);
    void stop(bool cancel);

signals:
    void finished();

private:
    void finish();
    void appendFinal(QString text);

    ui::SessionViewModel &m_viewModel;
    input::TextInjector &m_textInjector;
    audio::AudioRecorder *m_recorder = nullptr;
    DeepgramClient *m_client = nullptr;
    QTimer *m_elapsedTimer = nullptr;
    QTimer *m_silenceTimer = nullptr;
    qint64 m_lastSpeechMs = 0;
    bool m_speechStarted = false;
    QString m_finalText;
};

} // namespace vocaltri::transcription
