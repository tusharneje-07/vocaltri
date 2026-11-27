#include "cli/cli_client.hpp"
#include "daemon/daemon.hpp"

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QTextStream>
#include <QTimer>
#include <memory>

#if VOCALTRI_HAS_STREAMING
#include "audio/audio_recorder.hpp"
#endif

namespace {

int runMicrophoneTest(QCoreApplication &app) {
#if VOCALTRI_HAS_STREAMING
    vocaltri::audio::AudioRecorder recorder;
    qint64 bytes = 0;
    qreal peak = 0;
    bool failed = false;

    QObject::connect(&recorder, &vocaltri::audio::AudioRecorder::audioChunk,
                     &app, [&bytes](const QByteArray &chunk) { bytes += chunk.size(); });
    QObject::connect(&recorder, &vocaltri::audio::AudioRecorder::levelChanged,
                     &app, [&peak](const qreal level) { peak = qMax(peak, level); });
    QObject::connect(&recorder, &vocaltri::audio::AudioRecorder::errorOccurred,
                     &app, [&failed](const QString &message) {
                         failed = true;
                         QTextStream(stderr) << "vocaltri: " << message << '\n';
                     });

    if (!recorder.start()) {
        QTextStream(stderr) << "vocaltri: could not open the default microphone\n";
        return 1;
    }

    QTimer::singleShot(3000, &app, &QCoreApplication::quit);
    app.exec();
    recorder.stop();

    QTextStream(stdout) << "microphone test: " << bytes << " bytes captured, peak level "
                        << QString::number(peak, 'f', 3) << '\n';
    return (!failed && bytes > 0) ? 0 : 1;
#else
    Q_UNUSED(app);
    QTextStream(stderr) << "vocaltri: built without Qt Multimedia; microphone test unavailable\n";
    return 1;
#endif
}

} // namespace

int main(int argc, char **argv) {
    const auto firstArgument = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QString{};
    const bool needsGui = firstArgument == QStringLiteral("server")
        || firstArgument == QStringLiteral("mic-test");
    std::unique_ptr<QCoreApplication> app = needsGui
        ? std::unique_ptr<QCoreApplication>(new QGuiApplication(argc, argv))
        : std::unique_ptr<QCoreApplication>(new QCoreApplication(argc, argv));
    QCoreApplication::setApplicationName(QStringLiteral("vocaltri"));
    QCoreApplication::setApplicationVersion(QStringLiteral(VOCALTRI_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Linux voice typing daemon and CLI"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QStringLiteral("command"),
        QStringLiteral("server, toggle, start, stop, cancel, status, or mic-test"),
        QStringLiteral("[command]"));
    parser.process(*app);

    const auto positional = parser.positionalArguments();
    if (positional.isEmpty()) {
        parser.showHelp(0);
    }

    if (positional.size() != 1) {
        QTextStream(stderr) << "vocaltri: expected exactly one command\n";
        return 2;
    }

    if (positional.first() == QStringLiteral("mic-test")) {
        return runMicrophoneTest(*app);
    }

    if (positional.first() == QStringLiteral("server")) {
        vocaltri::daemon::Daemon daemon;
        if (!daemon.start()) {
            return 1;
        }
        return app->exec();
    }

    vocaltri::ipc::Command command{};
    if (!vocaltri::ipc::parseCommand(positional.first(), command)) {
        QTextStream(stderr) << "vocaltri: unknown command '" << positional.first()
                            << "' (use --help for usage)\n";
        return 2;
    }
    return vocaltri::cli::Client::run(command);
}
