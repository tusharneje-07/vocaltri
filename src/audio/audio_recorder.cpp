#include "audio/audio_recorder.hpp"

#ifdef VOCALTRI_HAS_PIPEWIRE
#include "audio/pipewire_capture.hpp"
#endif

#include <QAudioSource>
#include <QDebug>
#include <QMediaDevices>
#include <QtGlobal>

#include <cmath>

namespace vocaltri::audio {

AudioRecorder::AudioRecorder(QObject *parent)
    : QObject(parent) {
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);
}

AudioRecorder::~AudioRecorder() {
    stop();
}

bool AudioRecorder::start() {
    if (isRecording()) {
        return false;
    }

    const auto forced = qEnvironmentVariable("VOCALTRI_AUDIO_BACKEND").trimmed().toLower();
    if (forced != QStringLiteral("qt") && forced != QStringLiteral("multimedia")) {
        if (startPipeWire()) {
            return true;
        }
    }
    return startQtMultimedia();
}

bool AudioRecorder::startPipeWire() {
#ifdef VOCALTRI_HAS_PIPEWIRE
    auto *capture = new PipeWireCapture(this);
    connect(capture, &PipeWireCapture::audioChunk, this, &AudioRecorder::handleChunk);
    connect(capture, &PipeWireCapture::errorOccurred, this, [this](const QString &message) {
        qWarning() << "vocaltri: audio capture failed:" << message;
        emit errorOccurred(message);
    });

    QString error;
    if (!capture->start(error)) {
        qWarning() << "vocaltri: PipeWire capture unavailable:" << error;
        capture->deleteLater();
        return false;
    }
    m_pipewire = capture;
    m_backendName = QStringLiteral("pipewire");
    qInfo() << "vocaltri: capturing with PipeWire at 16 kHz mono";
    return true;
#else
    return false;
#endif
}

bool AudioRecorder::startQtMultimedia() {
    const auto device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        emit errorOccurred(QStringLiteral("no audio input device is available"));
        return false;
    }
    if (!device.isFormatSupported(m_format)) {
        emit errorOccurred(QStringLiteral("audio input does not support 16 kHz mono PCM"));
        return false;
    }

    m_source = new QAudioSource(device, m_format, this);
    connect(m_source, &QAudioSource::stateChanged, this, [this] {
        if (m_source && m_source->error() != QtAudio::NoError) {
            emit errorOccurred(QStringLiteral("audio input stopped unexpectedly"));
        }
    });
    m_device = m_source->start();
    if (!m_device) {
        emit errorOccurred(QStringLiteral("could not open the audio input"));
        m_source->deleteLater();
        m_source = nullptr;
        return false;
    }
    connect(m_device, &QIODevice::readyRead, this, &AudioRecorder::readAudio);
    m_backendName = QStringLiteral("qt multimedia");
    qInfo() << "vocaltri: capturing with Qt Multimedia at 16 kHz mono";
    return true;
}

void AudioRecorder::stop() {
#ifdef VOCALTRI_HAS_PIPEWIRE
    if (m_pipewire) {
        m_pipewire->stop();
        m_pipewire->deleteLater();
        m_pipewire = nullptr;
    }
#endif
    if (!m_source) {
        return;
    }
    readAudio();
    m_source->stop();
    m_source->deleteLater();
    m_source = nullptr;
    m_device = nullptr;
}

bool AudioRecorder::isRecording() const {
    return m_pipewire != nullptr || m_source != nullptr;
}

void AudioRecorder::readAudio() {
    if (!m_device) {
        return;
    }
    handleChunk(m_device->readAll());
}

void AudioRecorder::handleChunk(QByteArray chunk) {
    if (chunk.isEmpty()) {
        return;
    }

    const auto sampleCount = chunk.size() / static_cast<qsizetype>(sizeof(qint16));
    const auto *samples = reinterpret_cast<const qint16 *>(chunk.constData());
    double sum = 0;
    for (qsizetype index = 0; index < sampleCount; ++index) {
        const auto sample = static_cast<double>(samples[index]) / 32768.0;
        sum += sample * sample;
    }
    const auto level = sampleCount > 0
        ? qBound<qreal>(0, std::sqrt(sum / static_cast<double>(sampleCount)) * 4, 1)
        : 0;
    emit levelChanged(level);
    emit audioChunk(std::move(chunk));
}

} // namespace vocaltri::audio
