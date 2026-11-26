#include "common/deepgram_key.hpp"

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStringList>

namespace vocaltri {

namespace {

bool isOwnerOnly(const QFileDevice::Permissions permissions) {
    constexpr auto forbidden = QFileDevice::ReadGroup | QFileDevice::WriteGroup
        | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther
        | QFileDevice::ExeOther;
    return (permissions & forbidden) == 0;
}

} // namespace

QString deepgramApiKey() {
    const auto environmentKey = qEnvironmentVariable("DEEPGRAM_API_KEY").trimmed();
    if (!environmentKey.isEmpty()) {
        return environmentKey;
    }

    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/config.toml");
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    if (!isOwnerOnly(file.permissions())) {
        return {};
    }

    const auto lines = QString::fromUtf8(file.readAll()).split('\n');
    for (const auto &line : lines) {
        const auto trimmed = line.trimmed();
        if (!trimmed.startsWith(QStringLiteral("deepgram_api_key"))) {
            continue;
        }
        const auto separator = trimmed.indexOf('=');
        if (separator < 0) {
            continue;
        }
        auto value = trimmed.sliced(separator + 1).trimmed();
        if (value.startsWith('"') && value.endsWith('"') && value.size() >= 2) {
            value = value.sliced(1, value.size() - 2);
        }
        return value;
    }
    return {};
}

} // namespace vocaltri
