#include "audio/audio_recorder.hpp"

#include <QAudioSource>
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

bool AudioRecorder::start() {
    if (m_source) {
        return false;
    }

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
    return true;
}

void AudioRecorder::stop() {
    if (!m_source) {
        return;
    }
    readAudio();
    m_source->stop();
    m_source->deleteLater();
    m_source = nullptr;
    m_device = nullptr;
}

void AudioRecorder::readAudio() {
    if (!m_device) {
        return;
    }
    const auto data = m_device->readAll();
    if (data.isEmpty()) {
        return;
    }

    const auto sampleCount = data.size() / static_cast<qsizetype>(sizeof(qint16));
    const auto *samples = reinterpret_cast<const qint16 *>(data.constData());
    double sum = 0;
    for (qsizetype index = 0; index < sampleCount; ++index) {
        const auto sample = static_cast<double>(samples[index]) / 32768.0;
        sum += sample * sample;
    }
    const auto level = sampleCount > 0
        ? qBound<qreal>(0, std::sqrt(sum / static_cast<double>(sampleCount)) * 4, 1)
        : 0;
    emit levelChanged(level);
    emit audioChunk(data);
}

} // namespace vocaltri::audio
