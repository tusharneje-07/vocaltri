#pragma once

#include <QObject>
#include <QString>

class QClipboard;

namespace vocaltri::input {

class TextInjector final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString backendName READ backendName CONSTANT)

public:
    explicit TextInjector(QObject *parent = nullptr);

    QString backendName() const { return m_backendName; }
    bool insert(const QString &text);

signals:
    void notificationRequested(QString title, QString message);

private:
    enum class Backend {
        ClipboardOnly,
        ClipboardYdotool,
        Wtype,
        Xdotool,
    };

    static Backend detectBackend();
    static QString backendLabel(Backend backend);
    bool setClipboardText(const QString &text);
    bool insertClipboardPaste(const QString &text);
    bool insertDirect(const QString &program, const QStringList &arguments, const QString &text);
    void notifyClipboardOnly();

    Backend m_backend;
    QString m_backendName;
    QClipboard *m_clipboard = nullptr;
};

} // namespace vocaltri::input
