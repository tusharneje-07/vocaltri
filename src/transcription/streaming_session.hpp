#pragma once

#include <QObject>

namespace vocaltri::audio {
class AudioRecorder;
}

namespace vocaltri::ui {
class SessionViewModel;
}

namespace vocaltri {
struct AppSettings;
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

    bool start(const AppSettings &settings);
    void stop(bool cancel);

signals:
    void failed(QString message);
    void finished();

private:
    void finish(bool done = true);
    void appendFinal(QString text);

    ui::SessionViewModel &m_viewModel;
    input::TextInjector &m_textInjector;
    audio::AudioRecorder *m_recorder = nullptr;
    DeepgramClient *m_client = nullptr;
    QTimer *m_elapsedTimer = nullptr;
    bool m_finished = false;
    QString m_finalText;
};

} // namespace vocaltri::transcription
