#include "cli/cli_client.hpp"

#include "common/socket_path.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTextStream>

namespace vocaltri::cli {

int Client::run(const ipc::Command command) {
    QLocalSocket socket;
    socket.connectToServer(socketPath());
    if (!socket.waitForConnected(1500)) {
        QTextStream(stderr) << "vocaltri: daemon is not running (" << socket.errorString() << ")\n";
        return 1;
    }

    const auto request = QJsonDocument(QJsonObject{
        {QStringLiteral("command"), ipc::commandName(command)},
    }).toJson(QJsonDocument::Compact) + '\n';

    if (socket.write(request) != request.size() || !socket.waitForBytesWritten(1500)) {
        QTextStream(stderr) << "vocaltri: failed to send command: " << socket.errorString() << '\n';
        return 1;
    }

    QByteArray response;
    while (!response.contains('\n')) {
        if (!socket.waitForReadyRead(3000)) {
            QTextStream(stderr) << "vocaltri: daemon did not reply: " << socket.errorString() << '\n';
            return 1;
        }
        response += socket.readAll();
    }

    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(response.left(response.indexOf('\n')), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        QTextStream(stderr) << "vocaltri: received an invalid daemon response\n";
        return 1;
    }

    const auto object = document.object();
    const auto message = object.value(QStringLiteral("message")).toString();
    if (!object.value(QStringLiteral("ok")).toBool()) {
        QTextStream(stderr) << "vocaltri: " << message << '\n';
        return 1;
    }

    QTextStream(stdout) << message << " (" << object.value(QStringLiteral("state")).toString() << ")\n";
    return 0;
}

} // namespace vocaltri::cli
