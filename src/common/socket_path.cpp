#include "common/socket_path.hpp"

#include <QDir>
#include <QStandardPaths>

#include <unistd.h>

namespace vocaltri {

QString socketPath() {
    const auto runtimeDirectory = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (!runtimeDirectory.isEmpty()) {
        return QDir(runtimeDirectory).filePath(QStringLiteral("vocaltri.sock"));
    }

    return QDir(QDir::tempPath()).filePath(
        QStringLiteral("vocaltri-%1.sock").arg(static_cast<qulonglong>(::getuid())));
}

} // namespace vocaltri
