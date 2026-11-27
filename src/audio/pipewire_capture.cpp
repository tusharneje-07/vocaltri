#include "audio/pipewire_capture.hpp"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw.h>

#include <QMetaObject>

#include <cstring>
#include <mutex>

namespace vocaltri::audio {

namespace {

std::once_flag g_initOnce;

void initPipeWire() {
    std::call_once(g_initOnce, [] { pw_init(nullptr, nullptr); });
}

} // namespace

PipeWireCapture::PipeWireCapture(QObject *parent)
    : QObject(parent) {}

PipeWireCapture::~PipeWireCapture() {
    stop();
}

bool PipeWireCapture::start(QString &error) {
    if (m_stream) {
        return true;
    }
    initPipeWire();

    m_loop = pw_main_loop_new(nullptr);
    if (!m_loop) {
        error = QStringLiteral("could not create a PipeWire main loop");
        return false;
    }

    static const pw_stream_events events = [] {
        pw_stream_events value{};
        value.version = PW_VERSION_STREAM_EVENTS;
        value.process = &PipeWireCapture::onProcess;
        value.state_changed = &PipeWireCapture::onStateChanged;
        return value;
    }();

    m_stream = pw_stream_new_simple(
        pw_main_loop_get_loop(m_loop),
        "vocaltri-capture",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                          PW_KEY_MEDIA_CATEGORY, "Capture",
                          PW_KEY_MEDIA_ROLE, "Communication",
                          PW_KEY_NODE_NAME, "vocaltri-capture",
                          PW_KEY_NODE_DESCRIPTION, "vocaltri dictation capture",
                          nullptr),
        &events,
        this);
    if (!m_stream) {
        pw_main_loop_destroy(m_loop);
        m_loop = nullptr;
        error = QStringLiteral("could not create a PipeWire capture stream");
        return false;
    }

    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    spa_audio_info_raw info = SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_S16,
                                                      .rate = 16000,
                                                      .channels = 1);
    const spa_pod *params[1] = {
        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info),
    };

    const auto result = pw_stream_connect(m_stream,
                                          PW_DIRECTION_INPUT,
                                          PW_ID_ANY,
                                          static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT
                                                                       | PW_STREAM_FLAG_MAP_BUFFERS),
                                          params,
                                          1);
    if (result < 0) {
        error = QStringLiteral("could not connect the PipeWire capture stream: %1")
                    .arg(QString::fromUtf8(strerror(-result)));
        pw_stream_destroy(m_stream);
        m_stream = nullptr;
        pw_main_loop_destroy(m_loop);
        m_loop = nullptr;
        return false;
    }

    m_stopping = false;
    m_thread = std::thread([this] { pw_main_loop_run(m_loop); });
    return true;
}

void PipeWireCapture::stop() {
    if (!m_loop) {
        return;
    }
    m_stopping = true;
    pw_main_loop_quit(m_loop);
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_stream) {
        pw_stream_destroy(m_stream);
        m_stream = nullptr;
    }
    pw_main_loop_destroy(m_loop);
    m_loop = nullptr;
}

void PipeWireCapture::onProcess(void *userdata) {
    auto *self = static_cast<PipeWireCapture *>(userdata);
    if (!self->m_stream) {
        return;
    }

    pw_buffer *buffer = pw_stream_dequeue_buffer(self->m_stream);
    if (!buffer) {
        return;
    }

    spa_buffer *spaBuffer = buffer->buffer;
    if (spaBuffer && spaBuffer->n_datas > 0 && spaBuffer->datas[0].data) {
        auto *data = &spaBuffer->datas[0];
        const auto offset = data->chunk ? data->chunk->offset : 0;
        const auto size = data->chunk ? data->chunk->size : 0;
        if (size > 0) {
            QByteArray chunk(reinterpret_cast<const char *>(
                                 static_cast<const uint8_t *>(data->data) + offset),
                             static_cast<qsizetype>(size));
            QMetaObject::invokeMethod(
                self,
                [self, chunk = std::move(chunk)]() mutable {
                    self->deliver(std::move(chunk));
                },
                Qt::QueuedConnection);
        }
    }
    pw_stream_queue_buffer(self->m_stream, buffer);
}

void PipeWireCapture::onStateChanged(void *userdata,
                                     pw_stream_state oldState,
                                     pw_stream_state state,
                                     const char *error) {
    Q_UNUSED(oldState)
    auto *self = static_cast<PipeWireCapture *>(userdata);
    if (state != PW_STREAM_STATE_ERROR || self->m_stopping) {
        return;
    }

    const auto message = error && *error
        ? QStringLiteral("PipeWire capture failed: %1").arg(QString::fromUtf8(error))
        : QStringLiteral("PipeWire capture failed");
    QMetaObject::invokeMethod(
        self, [self, message] { emit self->errorOccurred(message); }, Qt::QueuedConnection);
}

void PipeWireCapture::deliver(QByteArray chunk) {
    if (chunk.isEmpty()) {
        return;
    }
    emit audioChunk(std::move(chunk));
}

} // namespace vocaltri::audio
