#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>

class QClipboard;

namespace vocaltri::input {

class NativeEisSession;

class TextInjector final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString backendName READ backendName NOTIFY backendNameChanged)

public:
    explicit TextInjector(QObject *parent = nullptr);
    ~TextInjector() override;

    QString backendName() const { return m_backendName; }
    bool insert(const QString &text);

signals:
    void backendNameChanged();
    void notificationRequested(QString title, QString message);

private:
    enum class Backend {
        ClipboardOnly,
        ClipboardYdotool,
        NativeEis,
        Wtype,
        Xdotool,
    };

    static Backend detectBackend();
    static Backend detectFallbackBackend();
    static QString backendLabel(Backend backend);
    bool insertWithBackend(Backend backend, const QString &text);
    void nativeSessionFailed(const QStringList &pendingTexts);
    bool setClipboardText(const QString &text);
    bool insertClipboardPaste(const QString &text);
    bool insertDirect(const QString &program, const QStringList &arguments, const QString &text);
    void notifyClipboardOnly();

    Backend m_backend;
    Backend m_fallbackBackend;
    QString m_backendName;
    QClipboard *m_clipboard = nullptr;
    std::unique_ptr<NativeEisSession> m_eisSession;
};

} // namespace vocaltri::input
