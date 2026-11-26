#include "common/ipc_protocol.hpp"

#include <QJsonDocument>
#include <QJsonObject>

#include <cassert>

int main() {
    using namespace vocaltri::ipc;

    Command command{};
    assert(parseCommand(QStringLiteral("toggle"), command));
    assert(command == Command::Toggle);
    assert(!parseCommand(QStringLiteral("launch"), command));

    Request request{};
    QString error;
    assert(parseRequest(R"({"command":"start"})", request, error));
    assert(request.command == Command::Start);
    assert(!parseRequest(R"({"command":"nope"})", request, error));
    assert(!error.isEmpty());

    const auto encoded = encodeResponse({
        .ok = true,
        .command = Command::Stop,
        .state = SessionState::Idle,
        .message = QStringLiteral("dictation stopped"),
    });
    const auto object = QJsonDocument::fromJson(encoded.trimmed()).object();
    assert(object.value(QStringLiteral("ok")).toBool());
    assert(object.value(QStringLiteral("command")).toString() == QStringLiteral("stop"));
    assert(object.value(QStringLiteral("state")).toString() == QStringLiteral("idle"));
    return 0;
}
