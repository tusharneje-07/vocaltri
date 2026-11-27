#pragma once

#include <QObject>
#include <QUrl>

namespace vocaltri {
struct AppSettings;
}

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class QWebSocket;

namespace vocaltri::transcription {

class DeepgramClient final : public QObject {
    Q_OBJECT

public:
    explicit DeepgramClient(QObject *parent = nullptr);

    bool start(const AppSettings &settings);
    void sendAudio(const QByteArray &chunk);
    void closeStream();
    bool isOpen() const;

signals:
    void connected();
    void transcript(QString text, bool isFinal, bool speechFinal);
    void finished();
    void errorOccurred(QString message, bool retryable);

private:
    void beginAuthCheck();
    void openSocket();
    void handleText(QString message);
    void scheduleReconnect();
    void emitFinished();

    QString m_apiKey;
    QUrl m_url;
    QNetworkAccessManager *m_network = nullptr;
    QNetworkReply *m_authReply = nullptr;
    QWebSocket *m_socket = nullptr;
    QTimer *m_keepAlive = nullptr;
    QTimer *m_reconnect = nullptr;
    QTimer *m_closeTimeout = nullptr;
    int m_reconnectAttempts = 0;
    int m_authAttempts = 0;
    bool m_closing = false;
    bool m_finishedEmitted = false;
};

} // namespace vocaltri::transcription
