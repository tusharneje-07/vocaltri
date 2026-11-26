#include "transcription/streaming_session.hpp"

#include "audio/audio_recorder.hpp"
#include "transcription/deepgram_client.hpp"
#include "input/text_injector.hpp"
#include "ui/session_view_model.hpp"

#include <QElapsedTimer>
#include <QTimer>

namespace vocaltri::transcription {

StreamingSession::StreamingSession(ui::SessionViewModel &viewModel,
                                   input::TextInjector &textInjector,
                                   QObject *parent)
    : QObject(parent)
    , m_viewModel(viewModel)
    , m_textInjector(textInjector)
    , m_recorder(new audio::AudioRecorder(this))
    , m_client(new DeepgramClient(this))
    , m_elapsedTimer(new QTimer(this))
    , m_silenceTimer(new QTimer(this)) {
    m_elapsedTimer->setInterval(100);
    m_silenceTimer->setInterval(250);
    connect(m_recorder, &audio::AudioRecorder::audioChunk, m_client, &DeepgramClient::sendAudio);
    connect(m_recorder, &audio::AudioRecorder::levelChanged, this, [this](const qreal level) {
        m_viewModel.setLevel(level);
        if (level > 0.05) {
            m_speechStarted = true;
            m_lastSpeechMs = m_viewModel.elapsedMs();
        }
    });
    connect(m_recorder, &audio::AudioRecorder::errorOccurred, this, [this](const QString &) {
        stop(true);
    });
    connect(m_client, &DeepgramClient::connected, this, [this] {
        if (!m_recorder->start()) {
            stop(true);
            return;
        }
        m_viewModel.setState(ipc::SessionState::Listening);
    });
    connect(m_client, &DeepgramClient::transcript, this,
            [this](const QString &text, const bool isFinal, const bool speechFinal) {
                if (isFinal) {
                    appendFinal(text);
                    m_viewModel.setTranscript(m_finalText, {});
                } else {
                    m_viewModel.setTranscript(m_finalText, text);
                }
                if (speechFinal && m_speechStarted) {
                    m_lastSpeechMs = m_viewModel.elapsedMs();
                }
            });
    connect(m_client, &DeepgramClient::finished, this, &StreamingSession::finish);
    connect(m_client, &DeepgramClient::errorOccurred, this,
            [this](const QString &, const bool retryable) {
                if (!retryable) {
                    m_viewModel.setState(ipc::SessionState::Error);
                    stop(true);
                }
            });
    connect(m_elapsedTimer, &QTimer::timeout, this, [this] {
        m_viewModel.setElapsedMs(m_viewModel.elapsedMs() + 100);
    });
    connect(m_silenceTimer, &QTimer::timeout, this, [this] {
        const auto elapsed = m_viewModel.elapsedMs();
        const auto sinceSpeech = m_speechStarted ? elapsed - m_lastSpeechMs : elapsed;
        if ((!m_speechStarted && elapsed >= 10000) || (m_speechStarted && sinceSpeech >= 3000)) {
            stop(false);
        }
    });
}

bool StreamingSession::start(const AppSettings &settings) {
    m_finalText.clear();
    m_speechStarted = false;
    m_lastSpeechMs = 0;
    m_viewModel.setElapsedMs(0);
    m_viewModel.setTranscript({}, {});
    m_viewModel.setState(ipc::SessionState::Connecting);
    if (!m_client->start(settings)) {
        return false;
    }
    m_elapsedTimer->start();
    m_silenceTimer->start();
    return true;
}

void StreamingSession::stop(const bool cancel) {
    if (!m_elapsedTimer->isActive()) {
        return;
    }
    m_elapsedTimer->stop();
    m_silenceTimer->stop();
    m_recorder->stop();
    if (cancel) {
        m_client->closeStream();
        m_viewModel.setTranscript({}, {});
        m_viewModel.setState(ipc::SessionState::Idle);
        return;
    }
    m_viewModel.setState(ipc::SessionState::Finalizing);
    m_client->closeStream();
}

void StreamingSession::finish() {
    m_viewModel.setState(ipc::SessionState::Done);
    emit finished();
}

void StreamingSession::appendFinal(QString text) {
    if (text.isEmpty()) {
        return;
    }
    if (!m_finalText.isEmpty()) {
        m_finalText += QLatin1Char(' ');
    }
    m_textInjector.insert(text);
    m_finalText += std::move(text);
}

} // namespace vocaltri::transcription
