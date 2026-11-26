#include "input/text_injector.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

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

} // namespace

TextInjector::TextInjector(QObject *parent)
    : QObject(parent)
    , m_backend(detectBackend())
    , m_backendName(backendLabel(m_backend))
    , m_clipboard(QGuiApplication::clipboard()) {}

TextInjector::Backend TextInjector::detectBackend() {
    // libei/RemoteDesktop portal is intentionally represented as a future
    // capability here; it needs an interactive portal session and permission
    // lifecycle rather than a command-line probe.
    if (!QStandardPaths::findExecutable(QStringLiteral("ydotool")).isEmpty()) {
        return Backend::ClipboardYdotool;
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("wtype")).isEmpty()) {
        return Backend::Wtype;
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("xdotool")).isEmpty()) {
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
        m_clipboard->setText(trimmed, QClipboard::Clipboard);
        notifyClipboardOnly();
        return true;
    }
    return false;
}

bool TextInjector::insertClipboardPaste(const QString &text) {
    if (!m_clipboard) {
        return false;
    }

    auto *saved = copyMimeData(m_clipboard->mimeData(QClipboard::Clipboard));
    m_clipboard->setText(text, QClipboard::Clipboard);

    auto *process = new QProcess(this);
    const auto restored = std::make_shared<bool>(false);
    const auto restore = [this, saved, restored] {
        if (*restored) {
            return;
        }
        *restored = true;
        if (saved) {
            m_clipboard->setMimeData(saved, QClipboard::Clipboard);
        }
    };
    connect(process, &QProcess::errorOccurred, this,
            [this, process, restore](QProcess::ProcessError) {
                restore();
                notifyClipboardOnly();
                process->deleteLater();
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this, process, restore](const int exitCode, QProcess::ExitStatus) {
                if (exitCode != 0) {
                    notifyClipboardOnly();
                }
                restore();
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
    if (!QProcess::startDetached(program, processArguments)) {
        m_clipboard->setText(text, QClipboard::Clipboard);
        notifyClipboardOnly();
        return false;
    }
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
