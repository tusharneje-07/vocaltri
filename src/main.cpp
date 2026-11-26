#include "cli/cli_client.hpp"
#include "daemon/daemon.hpp"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTextStream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("vocaltri"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VOCALTRI_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Linux voice typing daemon and CLI"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("command"),
        QStringLiteral("server, toggle, start, stop, or cancel"),
        QStringLiteral("[command]"));
    parser.process(app);

    const auto positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        parser.showHelp(0);
    }

    if (positional.size() != 1) {
        QTextStream(stderr) << "vocaltri: expected exactly one command\n";
        return 2;
    }

    if (positional.first() == QStringLiteral("server")) {
        vocaltri::daemon::Daemon daemon;
        if (!daemon.start()) {
            return 1;
        }
        return app.exec();
    }

    vocaltri::ipc::Command command{};
    if (!vocaltri::ipc::parseCommand(positional.first(), command)) {
        QTextStream(stderr) << "vocaltri: unknown command '" << positional.first()
                            << "' (use --help for usage)\n";
        return 2;
    }
    return vocaltri::cli::Client::run(command);
}
