#pragma once

#include <QAudioFormat>
#include <QObject>

class QAudioSource;
class QIODevice;

namespace vocaltri::audio {

class AudioRecorder final : public QObject {
    Q_OBJECT

public:
    explicit AudioRecorder(QObject *parent = nullptr);

    bool start();
    void stop();
    bool isRecording() const { return m_source != nullptr; }

signals:
    void audioChunk(QByteArray chunk);
    void levelChanged(qreal level);
    void errorOccurred(QString message);

private:
    void readAudio();

    QAudioFormat m_format;
    QAudioSource *m_source = nullptr;
    QIODevice *m_device = nullptr;
};

} // namespace vocaltri::audio
