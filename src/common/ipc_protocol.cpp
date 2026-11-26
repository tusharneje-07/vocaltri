#include "common/ipc_protocol.hpp"

#include <QJsonDocument>
#include <QJsonObject>

namespace vocaltri::ipc {

QString commandName(const Command command) {
    switch (command) {
    case Command::Toggle:
        return QStringLiteral("toggle");
    case Command::Start:
        return QStringLiteral("start");
    case Command::Stop:
        return QStringLiteral("stop");
    case Command::Cancel:
        return QStringLiteral("cancel");
    }
    return QStringLiteral("unknown");
}

QString stateName(const SessionState state) {
    switch (state) {
    case SessionState::Idle:
        return QStringLiteral("idle");
    case SessionState::Connecting:
        return QStringLiteral("connecting");
    case SessionState::Listening:
        return QStringLiteral("listening");
    case SessionState::Finalizing:
        return QStringLiteral("finalizing");
    case SessionState::Done:
        return QStringLiteral("done");
    case SessionState::Error:
        return QStringLiteral("error");
    }
    return QStringLiteral("error");
}

bool parseCommand(const QString &value, Command &command) {
    if (value == QStringLiteral("toggle")) {
        command = Command::Toggle;
    } else if (value == QStringLiteral("start")) {
        command = Command::Start;
    } else if (value == QStringLiteral("stop")) {
        command = Command::Stop;
    } else if (value == QStringLiteral("cancel")) {
        command = Command::Cancel;
    } else {
        return false;
    }
    return true;
}

bool parseRequest(const QByteArray &line, Request &request, QString &error) {
    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(line.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("request must be a JSON object");
        return false;
    }

    const auto commandValue = document.object().value(QStringLiteral("command"));
    if (!commandValue.isString() || !parseCommand(commandValue.toString(), request.command)) {
        error = QStringLiteral("command must be one of: toggle, start, stop, cancel");
        return false;
    }
    return true;
}

QByteArray encodeResponse(const Response &response) {
    QJsonObject object{
        {QStringLiteral("ok"), response.ok},
        {QStringLiteral("command"), commandName(response.command)},
        {QStringLiteral("state"), stateName(response.state)},
        {QStringLiteral("message"), response.message},
    };
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

} // namespace vocaltri::ipc
