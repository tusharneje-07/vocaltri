#include "daemon/daemon.hpp"

#include "common/socket_path.hpp"

#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTextStream>

namespace vocaltri::daemon {

Daemon::Daemon(QObject *parent)
    : QObject(parent)
    , m_server(new QLocalServer(this)) {
    connect(m_server, &QLocalServer::newConnection, this, &Daemon::handleConnection);
}

Daemon::~Daemon() {
    if (m_server->isListening()) {
        m_server->close();
        QLocalServer::removeServer(socketPath());
    }
}

bool Daemon::start() {
    const auto path = socketPath();
    if (m_server->listen(path)) {
        return true;
    }

    // A crashed daemon can leave a filesystem socket behind. Only remove it
    // after proving that another daemon is not answering on it.
    QLocalSocket probe;
    probe.connectToServer(path);
    if (probe.waitForConnected(150)) {
        QTextStream(stderr) << "vocaltri: another daemon is already listening at " << path << '\n';
        return false;
    }

    QLocalServer::removeServer(path);
    return m_server->listen(path);
}

void Daemon::handleConnection() {
    while (m_server->hasPendingConnections()) {
        auto *socket = m_server->nextPendingConnection();
        m_buffers.insert(socket, {});
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { handleData(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
    }
}

void Daemon::handleData(QLocalSocket *socket) {
    if (!m_buffers.contains(socket)) {
        return;
    }

    auto &buffer = m_buffers[socket];
    buffer += socket->readAll();
    const auto newline = buffer.indexOf('\n');
    if (newline < 0) {
        if (buffer.size() > 64 * 1024) {
            socket->disconnectFromServer();
        }
        return;
    }

    const auto line = buffer.left(newline);
    m_buffers.remove(socket);

    ipc::Request request{};
    QString error;
    if (!ipc::parseRequest(line, request, error)) {
        socket->write(ipc::encodeResponse({
            .ok = false,
            .command = ipc::Command::Toggle,
            .state = ipc::SessionState::Error,
            .message = error,
        }));
        socket->disconnectFromServer();
        return;
    }

    socket->write(ipc::encodeResponse(handle(request)));
    socket->disconnectFromServer();
}

ipc::Response Daemon::errorResponse(const ipc::Command command, const QString &message) const {
    return {
        .ok = false,
        .command = command,
        .state = ipc::SessionState::Error,
        .message = message,
    };
}

ipc::Response Daemon::handle(const ipc::Request &request) {
    const auto command = request.command;
    switch (command) {
    case ipc::Command::Toggle:
        if (m_state == ipc::SessionState::Idle) {
            m_state = ipc::SessionState::Connecting;
            // The streaming session will replace this transition in milestone C.
            m_state = ipc::SessionState::Listening;
            return {true, command, m_state, QStringLiteral("dictation started")};
        }
        m_state = ipc::SessionState::Finalizing;
        m_state = ipc::SessionState::Idle;
        return {true, command, m_state, QStringLiteral("dictation stopped")};
    case ipc::Command::Start:
        if (m_state != ipc::SessionState::Idle) {
            return errorResponse(command, QStringLiteral("dictation is already active"));
        }
        m_state = ipc::SessionState::Connecting;
        m_state = ipc::SessionState::Listening;
        return {true, command, m_state, QStringLiteral("dictation started")};
    case ipc::Command::Stop:
        if (m_state == ipc::SessionState::Idle) {
            return errorResponse(command, QStringLiteral("dictation is not active"));
        }
        m_state = ipc::SessionState::Finalizing;
        m_state = ipc::SessionState::Idle;
        return {true, command, m_state, QStringLiteral("dictation stopped")};
    case ipc::Command::Cancel:
        if (m_state == ipc::SessionState::Idle) {
            return errorResponse(command, QStringLiteral("dictation is not active"));
        }
        m_state = ipc::SessionState::Idle;
        return {true, command, m_state, QStringLiteral("dictation cancelled")};
    }
    return errorResponse(command, QStringLiteral("unknown command"));
}

} // namespace vocaltri::daemon
