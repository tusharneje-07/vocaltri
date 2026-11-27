#include "cli/cli_client.hpp"

#include "common/socket_path.hpp"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTextStream>
#include <QThread>

namespace vocaltri::cli {

namespace {

bool exchange(const ipc::Command command, QJsonObject &response, QString &error) {
    QLocalSocket socket;
    socket.connectToServer(socketPath());
    if (!socket.waitForConnected(1500)) {
        error = QStringLiteral("daemon is not running (%1)").arg(socket.errorString());
        return false;
    }

    const auto request = QJsonDocument(QJsonObject{
        {QStringLiteral("command"), ipc::commandName(command)},
    }).toJson(QJsonDocument::Compact) + '\n';

    if (socket.write(request) != request.size() || !socket.waitForBytesWritten(1500)) {
        error = QStringLiteral("failed to send command: %1").arg(socket.errorString());
        return false;
    }

    QByteArray payload;
    while (!payload.contains('\n')) {
        if (!socket.waitForReadyRead(3000)) {
            error = QStringLiteral("daemon did not reply: %1").arg(socket.errorString());
            return false;
        }
        payload += socket.readAll();
    }

    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(payload.left(payload.indexOf('\n')), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = QStringLiteral("received an invalid daemon response");
        return false;
    }
    response = document.object();
    return true;
}

} // namespace

int Client::run(const ipc::Command command) {
    QJsonObject response;
    QString error;
    if (!exchange(command, response, error)) {
        QTextStream(stderr) << "vocaltri: " << error << '\n';
        return 1;
    }

    const auto message = response.value(QStringLiteral("message")).toString();
    if (!response.value(QStringLiteral("ok")).toBool()) {
        QTextStream(stderr) << "vocaltri: " << message << '\n';
        return 1;
    }

    // Starting is asynchronous: the daemon validates the API key and opens
    // the microphone after this response. Wait briefly so the CLI can report
    // a rejected key or a missing microphone instead of a silent "connecting".
    if (command == ipc::Command::Start || command == ipc::Command::Toggle) {
        const auto state = response.value(QStringLiteral("state")).toString();
        if (state == QStringLiteral("connecting") || state == QStringLiteral("listening")) {
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 6000) {
                QThread::msleep(100);

                QJsonObject status;
                QString statusError;
                if (!exchange(ipc::Command::Status, status, statusError)) {
                    QTextStream(stderr) << "vocaltri: " << statusError << '\n';
                    return 1;
                }

                const auto statusState = status.value(QStringLiteral("state")).toString();
                if (statusState == QStringLiteral("listening")) {
                    QTextStream(stdout) << "dictation listening (listening)\n";
                    return 0;
                }
                if (statusState == QStringLiteral("error")) {
                    QTextStream(stderr) << "vocaltri: "
                                        << status.value(QStringLiteral("message")).toString()
                                        << '\n';
                    return 1;
                }
                if (statusState == QStringLiteral("idle")) {
                    const auto statusMessage = status.value(QStringLiteral("message")).toString();
                    if (!statusMessage.isEmpty()
                        && statusMessage != QStringLiteral("dictation idle")) {
                        QTextStream(stderr) << "vocaltri: " << statusMessage << '\n';
                        return 1;
                    }
                    break;
                }
            }
        }
    }

    QTextStream(stdout) << message << " ("
                        << response.value(QStringLiteral("state")).toString() << ")\n";
    return 0;
}

} // namespace vocaltri::cli
