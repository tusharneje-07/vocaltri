#pragma once

#include "common/ipc_protocol.hpp"

#include <QObject>
#include <QHash>
#include <memory>

class QLocalServer;
class QLocalSocket;

namespace vocaltri::ui {
class OverlayWindow;
class SessionViewModel;
}

namespace vocaltri::transcription {
class StreamingSession;
}

namespace vocaltri::input {
class TextInjector;
}

namespace vocaltri::daemon {

class Daemon final : public QObject {
    Q_OBJECT

public:
    explicit Daemon(QObject *parent = nullptr);
    ~Daemon() override;

    bool start();
    ipc::SessionState state() const { return m_state; }

private:
    void handleConnection();
    void handleData(QLocalSocket *socket);
    ipc::Response handle(const ipc::Request &request);
    ipc::Response errorResponse(ipc::Command command, const QString &message) const;

    QLocalServer *m_server = nullptr;
    QHash<QLocalSocket *, QByteArray> m_buffers;
    ipc::SessionState m_state = ipc::SessionState::Idle;
    std::unique_ptr<ui::OverlayWindow> m_overlay;
    std::unique_ptr<ui::SessionViewModel> m_sessionModel;
    std::unique_ptr<input::TextInjector> m_textInjector;
#if VOCALTRI_HAS_STREAMING
    std::unique_ptr<transcription::StreamingSession> m_streamingSession;
#endif
};

} // namespace vocaltri::daemon
