#pragma once

#include <QByteArray>
#include <QString>

namespace vocaltri::ipc {

enum class Command {
    Toggle,
    Start,
    Stop,
    Cancel,
};

enum class SessionState {
    Idle,
    Connecting,
    Listening,
    Finalizing,
    Done,
    Error,
};

struct Request {
    Command command;
};

struct Response {
    bool ok = false;
    Command command = Command::Toggle;
    SessionState state = SessionState::Idle;
    QString message;
};

QString commandName(Command command);
QString stateName(SessionState state);
bool parseCommand(const QString &value, Command &command);
bool parseRequest(const QByteArray &line, Request &request, QString &error);
QByteArray encodeResponse(const Response &response);

} // namespace vocaltri::ipc
