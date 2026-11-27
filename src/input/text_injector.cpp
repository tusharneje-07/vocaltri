#include "input/text_injector.hpp"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QtGlobal>

#include <memory>

namespace vocaltri::input {

namespace {

QMimeData *copyMimeData(const QMimeData *source) {
    if (!source) {
        return nullptr;
    }
    auto *copy = new QMimeData;
    for (const auto &format : source->formats()) {
        copy->setData(format, source->data(format));
    }
    return copy;
}

bool ydotoolDaemonReachable() {
    auto socket = qEnvironmentVariable("YDOTOOL_SOCKET").trimmed();
    if (socket.isEmpty()) {
        socket = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
            + QStringLiteral("/.ydotool_socket");
    }
    return QFileInfo::exists(socket) || QFileInfo::exists(QStringLiteral("/tmp/.ydotool_socket"));
}

} // namespace

TextInjector::TextInjector(QObject *parent)
    : QObject(parent)
    , m_backend(detectBackend())
    , m_backendName(backendLabel(m_backend))
    , m_clipboard(QGuiApplication::clipboard()) {
    qInfo() << "vocaltri: text insertion backend:" << m_backendName;
}

TextInjector::Backend TextInjector::detectBackend() {
    // Allow an explicit backend for tests and for users whose compositor
    // rejects an otherwise installed tool.
    const auto forced = qEnvironmentVariable("VOCALTRI_TEXT_BACKEND").trimmed().toLower();
    if (forced == QStringLiteral("clipboard")) {
        return Backend::ClipboardOnly;
    }
    if (forced == QStringLiteral("ydotool")) {
        return Backend::ClipboardYdotool;
    }
    if (forced == QStringLiteral("wtype")) {
        return Backend::Wtype;
    }
    if (forced == QStringLiteral("xdotool")) {
        return Backend::Xdotool;
    }

    // libei/RemoteDesktop portal is intentionally represented as a future
    // capability here; it needs an interactive portal session and permission
    // lifecycle rather than a command-line probe.
    if (!QStandardPaths::findExecutable(QStringLiteral("ydotool")).isEmpty()
        && ydotoolDaemonReachable()) {
        return Backend::ClipboardYdotool;
    }

    const auto wayland = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        || qEnvironmentVariable("XDG_SESSION_TYPE").compare(QStringLiteral("wayland"),
                                                            Qt::CaseInsensitive) == 0;
    if (wayland && !QStandardPaths::findExecutable(QStringLiteral("wtype")).isEmpty()) {
        return Backend::Wtype;
    }
    if (!wayland && !QStandardPaths::findExecutable(QStringLiteral("xdotool")).isEmpty()) {
        return Backend::Xdotool;
    }
    return Backend::ClipboardOnly;
}

QString TextInjector::backendLabel(const Backend backend) {
    switch (backend) {
    case Backend::ClipboardYdotool:
        return QStringLiteral("clipboard + ydotool");
    case Backend::Wtype:
        return QStringLiteral("wtype");
    case Backend::Xdotool:
        return QStringLiteral("xdotool");
    case Backend::ClipboardOnly:
        return QStringLiteral("clipboard only");
    }
    return QStringLiteral("clipboard only");
}

bool TextInjector::insert(const QString &text) {
    const auto trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }

    switch (m_backend) {
    case Backend::ClipboardYdotool:
        return insertClipboardPaste(trimmed);
    case Backend::Wtype:
        return insertDirect(QStringLiteral("wtype"), {QStringLiteral("--")}, trimmed);
    case Backend::Xdotool:
        return insertDirect(QStringLiteral("xdotool"),
                            {QStringLiteral("type"), QStringLiteral("--clearmodifiers"),
                             QStringLiteral("--delay"), QStringLiteral("0")},
                            trimmed);
    case Backend::ClipboardOnly:
        if (!setClipboardText(trimmed)) {
            qWarning() << "vocaltri: clipboard is unavailable";
            return false;
        }
        notifyClipboardOnly();
        return true;
    }
    return false;
}

bool TextInjector::setClipboardText(const QString &text) {
    // A background Wayland client cannot reliably own the compositor
    // selection through QClipboard, but wl-copy can.
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        const auto wlCopy = QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
        if (!wlCopy.isEmpty()) {
            QProcess process;
            process.start(wlCopy, {});
            if (process.waitForStarted(1000)) {
                process.write(text.toUtf8());
                process.closeWriteChannel();
                if (process.waitForFinished(2000)
                    && process.exitStatus() == QProcess::NormalExit
                    && process.exitCode() == 0) {
                    return true;
                }
            }
        }
    }

    if (!m_clipboard) {
        return false;
    }
    m_clipboard->setText(text, QClipboard::Clipboard);
    return true;
}

bool TextInjector::insertClipboardPaste(const QString &text) {
    auto *saved = m_clipboard
        ? copyMimeData(m_clipboard->mimeData(QClipboard::Clipboard))
        : nullptr;
    const auto savedText = m_clipboard ? m_clipboard->text(QClipboard::Clipboard) : QString{};
    if (!setClipboardText(text)) {
        delete saved;
        notifyClipboardOnly();
        return false;
    }

    auto *process = new QProcess(this);
    const auto restored = std::make_shared<bool>(false);
    const auto restore = [this, saved, savedText, restored] {
        if (*restored) {
            return;
        }
        *restored = true;
        if (!savedText.isEmpty()) {
            delete saved;
            setClipboardText(savedText);
        } else if (saved && m_clipboard) {
            m_clipboard->setMimeData(saved, QClipboard::Clipboard);
        }
    };
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError) {
                // Keep the transcript on the clipboard so the user can paste
                // it manually when the input backend is unavailable.
                notifyClipboardOnly();
                process->deleteLater();
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this, process, restore](const int exitCode, QProcess::ExitStatus) {
                if (exitCode != 0) {
                    notifyClipboardOnly();
                } else {
                    restore();
                }
                process->deleteLater();
            });
    process->start(QStringLiteral("ydotool"),
                   {QStringLiteral("key"), QStringLiteral("29:1"),
                    QStringLiteral("47:1"), QStringLiteral("47:0"),
                    QStringLiteral("29:0")});
    return true;
}

bool TextInjector::insertDirect(const QString &program,
                                const QStringList &arguments,
                                const QString &text) {
    auto processArguments = arguments;
    processArguments.append(text);

    auto *process = new QProcess(this);
    const auto handled = std::make_shared<bool>(false);
    const auto fallback = [this, text, handled] {
        if (*handled) {
            return;
        }
        *handled = true;
        if (setClipboardText(text)) {
            notifyClipboardOnly();
        }
    };
    connect(process, &QProcess::errorOccurred, this,
            [process, fallback](QProcess::ProcessError) {
                fallback();
                process->deleteLater();
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [process, fallback](const int exitCode, QProcess::ExitStatus) {
                if (exitCode != 0) {
                    fallback();
                }
                process->deleteLater();
            });
    process->start(program, processArguments);
    return true;
}

void TextInjector::notifyClipboardOnly() {
    emit notificationRequested(QStringLiteral("vocaltri"),
                               QStringLiteral("Transcript copied to the clipboard"));
    if (!QStandardPaths::findExecutable(QStringLiteral("notify-send")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("notify-send"),
                                {QStringLiteral("vocaltri"),
                                 QStringLiteral("Transcript copied to the clipboard")});
    }
}

} // namespace vocaltri::input
