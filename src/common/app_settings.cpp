#include "common/app_settings.hpp"

#include <QFile>
#include <QHash>
#include <QStandardPaths>
#include <QtGlobal>

namespace vocaltri {

namespace {

bool ownerOnly(const QFileDevice::Permissions permissions) {
    constexpr auto forbidden = QFileDevice::ReadGroup | QFileDevice::WriteGroup
        | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther
        | QFileDevice::ExeOther;
    return (permissions & forbidden) == 0;
}

QHash<QString, QString> readConfig() {
    QHash<QString, QString> values;
    QFile file(appConfigPath());
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)
        || !ownerOnly(file.permissions())) {
        return values;
    }

    for (const auto &line : QString::fromUtf8(file.readAll()).split('\n')) {
        const auto trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#')) {
            continue;
        }
        const auto separator = trimmed.indexOf('=');
        if (separator <= 0) {
            continue;
        }
        auto value = trimmed.sliced(separator + 1).trimmed();
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"')) {
            value = value.sliced(1, value.size() - 2);
        }
        values.insert(trimmed.left(separator).trimmed(), value);
    }
    return values;
}

bool booleanValue(const QString &value, const bool fallback) {
    if (value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0
        || value == QStringLiteral("1")) {
        return true;
    }
    if (value.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0
        || value == QStringLiteral("0")) {
        return false;
    }
    return fallback;
}

int boundedInt(const QString &value, const int fallback, const int minimum, const int maximum) {
    bool ok = false;
    const auto parsed = value.toInt(&ok);
    return ok ? qBound(minimum, parsed, maximum) : fallback;
}

} // namespace

QString appConfigPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/config.toml");
}

AppSettings loadAppSettings() {
    const auto values = readConfig();
    AppSettings settings;
    settings.apiKey = qEnvironmentVariable("DEEPGRAM_API_KEY").trimmed();
    if (settings.apiKey.isEmpty()) {
        settings.apiKey = values.value(QStringLiteral("deepgram_api_key")).trimmed();
    }
    settings.model = values.value(QStringLiteral("model"), settings.model).trimmed();
    settings.language = values.value(QStringLiteral("language")).trimmed();
    settings.smartFormat = booleanValue(values.value(QStringLiteral("smart_format")),
                                        settings.smartFormat);
    settings.punctuate = booleanValue(values.value(QStringLiteral("punctuate")),
                                      settings.punctuate);
    settings.endpointingMs = boundedInt(values.value(QStringLiteral("endpointing_ms")),
                                        settings.endpointingMs, 10, 10000);
    settings.utteranceEndMs = boundedInt(values.value(QStringLiteral("utterance_end_ms")),
                                         settings.utteranceEndMs, 1000, 10000);
    return settings;
}

} // namespace vocaltri
