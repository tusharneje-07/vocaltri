#pragma once

#include <QString>

namespace vocaltri {

struct AppSettings {
    QString apiKey;
    QString model = QStringLiteral("nova-3");
    QString language;
    bool smartFormat = true;
    bool punctuate = true;
    int endpointingMs = 300;
    int utteranceEndMs = 1000;
};

AppSettings loadAppSettings();
QString appConfigPath();

} // namespace vocaltri
