#include "transcription/deepgram_client.hpp"

#include "common/app_settings.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>
#include <QWebSocket>

namespace vocaltri::transcription {

DeepgramClient::DeepgramClient(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_keepAlive(new QTimer(this))
    , m_reconnect(new QTimer(this))
    , m_closeTimeout(new QTimer(this)) {
    m_keepAlive->setInterval(10000);
    connect(m_keepAlive, &QTimer::timeout, this, [this] {
        if (isOpen()) {
            m_socket->sendTextMessage(QStringLiteral(R"({"type":"KeepAlive"})"));
        }
    });

    m_reconnect->setSingleShot(true);
    // Re-validate the key before reconnecting. Qt's WebSocket handshake
    // handler crashes on a 401 response carrying a WWW-Authenticate header,
    // so never let an invalid key reach QWebSocket.
    connect(m_reconnect, &QTimer::timeout, this, &DeepgramClient::beginAuthCheck);
    m_closeTimeout->setSingleShot(true);
    m_closeTimeout->setInterval(2500);
    // CloseStream flushes buffered audio and Deepgram answers with the last
    // final Results before it closes. Give that reply time to arrive instead
    // of tearing the socket down immediately.
    connect(m_closeTimeout, &QTimer::timeout, this, [this] {
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->close(QWebSocketProtocol::CloseCodeNormal,
                            QStringLiteral("dictation complete"));
        }
        emitFinished();
    });
    connect(m_socket, &QWebSocket::connected, this, [this] {
        m_reconnectAttempts = 0;
        m_keepAlive->start();
        emit connected();
    });
    connect(m_socket, &QWebSocket::textMessageReceived, this, &DeepgramClient::handleText);
    connect(m_socket, &QWebSocket::disconnected, this, [this] {
        m_keepAlive->stop();
        if (m_closing) {
            emitFinished();
        } else {
            scheduleReconnect();
        }
    });
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        const auto detail = m_socket->errorString();
        const auto authFailure = detail.contains(QStringLiteral("401"))
            || detail.contains(QStringLiteral("Unauthorized"), Qt::CaseInsensitive)
            || detail.contains(QStringLiteral("WWW-Authenticate"), Qt::CaseInsensitive);
        const auto rateLimited = detail.contains(QStringLiteral("429"))
            || detail.contains(QStringLiteral("rate"), Qt::CaseInsensitive);
        emit errorOccurred(authFailure
                               ? QStringLiteral("Deepgram rejected the API key (invalid or expired)")
                               : rateLimited
                                   ? QStringLiteral("Deepgram rate limit reached")
                                   : detail,
                           !authFailure && !rateLimited);
    });
}

bool DeepgramClient::start(const AppSettings &settings) {
    if (settings.apiKey.isEmpty()) {
        return false;
    }
    m_apiKey = settings.apiKey;
    m_closing = false;
    m_finishedEmitted = false;
    m_reconnectAttempts = 0;
    m_authAttempts = 0;
    m_closeTimeout->stop();
    if (m_authReply) {
        auto *reply = m_authReply;
        m_authReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }

    const auto overrideUrl = qEnvironmentVariable("VOCALTRI_DEEPGRAM_WS_URL").trimmed();
    QUrl url(overrideUrl.isEmpty() ? QStringLiteral("wss://api.deepgram.com/v1/listen")
                                   : overrideUrl);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("model"), settings.model);
    query.addQueryItem(QStringLiteral("encoding"), QStringLiteral("linear16"));
    query.addQueryItem(QStringLiteral("sample_rate"), QStringLiteral("16000"));
    query.addQueryItem(QStringLiteral("channels"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("interim_results"), QStringLiteral("true"));
    query.addQueryItem(QStringLiteral("smart_format"), settings.smartFormat ? QStringLiteral("true")
                                                                            : QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("punctuate"), settings.punctuate ? QStringLiteral("true")
                                                                        : QStringLiteral("false"));
    query.addQueryItem(QStringLiteral("endpointing"), QString::number(settings.endpointingMs));
    query.addQueryItem(QStringLiteral("utterance_end_ms"), QString::number(settings.utteranceEndMs));
    if (!settings.language.isEmpty()) {
        query.addQueryItem(QStringLiteral("language"), settings.language);
    }
    query.addQueryItem(QStringLiteral("vad_events"), QStringLiteral("true"));
    url.setQuery(query);
    m_url = url;
    beginAuthCheck();
    return true;
}

void DeepgramClient::beginAuthCheck() {
    if (m_closing || m_apiKey.isEmpty()) {
        return;
    }

    const auto overrideAuthUrl = qEnvironmentVariable("VOCALTRI_DEEPGRAM_AUTH_URL").trimmed();
    QNetworkRequest request(overrideAuthUrl.isEmpty()
                                ? QUrl(QStringLiteral("https://api.deepgram.com/v1/auth/token"))
                                : QUrl(overrideAuthUrl));
    request.setRawHeader("Authorization", "Token " + m_apiKey.toUtf8());
    request.setRawHeader("User-Agent", "vocaltri/0.1");
    request.setTransferTimeout(5000);

    m_authReply = m_network->get(request);
    connect(m_authReply, &QNetworkReply::finished, this, [this, reply = m_authReply] {
        if (m_authReply == reply) {
            m_authReply = nullptr;
        }
        reply->deleteLater();
        if (m_closing) {
            return;
        }

        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
            m_authAttempts = 0;
            openSocket();
            return;
        }

        const auto authFailure = status == 401 || status == 403;
        if (authFailure) {
            emit errorOccurred(
                QStringLiteral("Deepgram rejected the API key (invalid or expired)"), false);
            return;
        }

        // Retry transient validation failures a few times before giving up.
        if (m_authAttempts < 2) {
            const auto delay = 250 * (1 << m_authAttempts++);
            QTimer::singleShot(delay, this, &DeepgramClient::beginAuthCheck);
            return;
        }
        emit errorOccurred(QStringLiteral("could not reach Deepgram to validate the API key"),
                           false);
    });
}

void DeepgramClient::openSocket() {
    if (m_closing || m_apiKey.isEmpty()) {
        return;
    }
    QNetworkRequest request(m_url);
    request.setRawHeader("Authorization", "Token " + m_apiKey.toUtf8());
    request.setRawHeader("User-Agent", "vocaltri/0.1");
    m_socket->open(request);
}

void DeepgramClient::sendAudio(const QByteArray &chunk) {
    if (isOpen() && !chunk.isEmpty()) {
        m_socket->sendBinaryMessage(chunk);
    }
}

void DeepgramClient::closeStream() {
    if (m_closing) {
        return;
    }
    m_closing = true;
    m_reconnect->stop();
    m_keepAlive->stop();
    if (m_authReply) {
        auto *reply = m_authReply;
        m_authReply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    if (!isOpen()) {
        emitFinished();
        return;
    }
    // Ask Deepgram to flush what is left and wait for the final Results.
    // The server closes the socket afterwards, which ends the session; the
    // timeout is only a safety net for a server that never answers.
    m_socket->sendTextMessage(QStringLiteral(R"({"type":"CloseStream"})"));
    m_closeTimeout->start();
}

void DeepgramClient::emitFinished() {
    if (m_finishedEmitted) {
        return;
    }
    m_finishedEmitted = true;
    m_closeTimeout->stop();
    emit finished();
}

bool DeepgramClient::isOpen() const {
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void DeepgramClient::handleText(const QString message) {
    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }
    const auto object = document.object();
    if (object.value(QStringLiteral("type")).toString() == QStringLiteral("Error")) {
        emit errorOccurred(object.value(QStringLiteral("description")).toString(), false);
        return;
    }

    const auto alternatives = object.value(QStringLiteral("channel"))
                                 .toObject()
                                 .value(QStringLiteral("alternatives"))
                                 .toArray();
    if (alternatives.isEmpty()) {
        return;
    }
    const auto text = alternatives.first().toObject().value(QStringLiteral("transcript")).toString().trimmed();
    emit transcript(text,
                    object.value(QStringLiteral("is_final")).toBool(),
                    object.value(QStringLiteral("speech_final")).toBool());
}

void DeepgramClient::scheduleReconnect() {
    if (m_closing) {
        return;
    }
    if (m_reconnectAttempts >= 3) {
        emit errorOccurred(QStringLiteral("Deepgram connection could not be re-established"), false);
        return;
    }
    const auto delay = 250 * (1 << m_reconnectAttempts++);
    m_reconnect->start(delay);
}

} // namespace vocaltri::transcription
