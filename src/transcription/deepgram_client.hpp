#pragma once

#include <QObject>
#include <QUrl>

class QTimer;
class QWebSocket;

namespace vocaltri::transcription {

class DeepgramClient final : public QObject {
    Q_OBJECT

public:
    explicit DeepgramClient(QObject *parent = nullptr);

    bool start(const QString &apiKey);
    void sendAudio(const QByteArray &chunk);
    void closeStream();
    bool isOpen() const;

signals:
    void connected();
    void transcript(QString text, bool isFinal, bool speechFinal);
    void finished();
    void errorOccurred(QString message, bool retryable);

private:
    void openSocket();
    void handleText(QString message);
    void scheduleReconnect();

    QString m_apiKey;
    QUrl m_url;
    QWebSocket *m_socket = nullptr;
    QTimer *m_keepAlive = nullptr;
    QTimer *m_reconnect = nullptr;
    int m_reconnectAttempts = 0;
    bool m_closing = false;
};

} // namespace vocaltri::transcription
