#pragma once

#include <QAudioFormat>
#include <QObject>
#include <QString>

class QAudioSource;
class QIODevice;

namespace vocaltri::audio {

class PipeWireCapture;

// Captures 16 kHz mono signed 16-bit PCM for the streaming session.
// PipeWire is used directly when available; Qt Multimedia is the fallback
// for systems without a PipeWire daemon or with VOCALTRI_AUDIO_BACKEND=qt.
class AudioRecorder final : public QObject {
    Q_OBJECT

public:
    explicit AudioRecorder(QObject *parent = nullptr);
    ~AudioRecorder() override;

    bool start();
    void stop();
    bool isRecording() const;
    QString backendName() const { return m_backendName; }

signals:
    void audioChunk(QByteArray chunk);
    void levelChanged(qreal level);
    void errorOccurred(QString message);

private:
    bool startPipeWire();
    bool startQtMultimedia();
    void readAudio();
    void handleChunk(QByteArray chunk);

    QAudioFormat m_format;
    QAudioSource *m_source = nullptr;
    QIODevice *m_device = nullptr;
    PipeWireCapture *m_pipewire = nullptr;
    QString m_backendName;
};

} // namespace vocaltri::audio
