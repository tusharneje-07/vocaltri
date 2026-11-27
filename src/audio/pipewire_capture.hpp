#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <pipewire/main-loop.h>
#include <pipewire/stream.h>

#include <thread>

namespace vocaltri::audio {

// Captures the default PipeWire source as 16 kHz mono signed 16-bit PCM.
// The PipeWire main loop runs on its own thread; buffers are handed to the
// Qt thread before they are turned into signals.
class PipeWireCapture final : public QObject {
    Q_OBJECT

public:
    explicit PipeWireCapture(QObject *parent = nullptr);
    ~PipeWireCapture() override;

    bool start(QString &error);
    void stop();
    bool isRunning() const { return m_stream != nullptr; }

signals:
    void audioChunk(QByteArray chunk);
    void errorOccurred(QString message);

private:
    static void onProcess(void *userdata);
    static void onStateChanged(void *userdata,
                               pw_stream_state oldState,
                               pw_stream_state state,
                               const char *error);
    void deliver(QByteArray chunk);

    pw_main_loop *m_loop = nullptr;
    pw_stream *m_stream = nullptr;
    std::thread m_thread;
    bool m_stopping = false;
};

} // namespace vocaltri::audio
