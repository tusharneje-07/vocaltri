#pragma once

#include "common/ipc_protocol.hpp"

#include <QObject>
#include <QHash>
#include <memory>

class QLocalServer;
class QLocalSocket;

namespace vocaltri::ui {
class OverlayWindow;
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
};

} // namespace vocaltri::daemon
