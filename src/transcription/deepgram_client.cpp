#include "transcription/deepgram_client.hpp"

#include "common/app_settings.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>
#include <QWebSocket>

namespace vocaltri::transcription {

DeepgramClient::DeepgramClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
    , m_keepAlive(new QTimer(this))
    , m_reconnect(new QTimer(this)) {
    m_keepAlive->setInterval(10000);
    connect(m_keepAlive, &QTimer::timeout, this, [this] {
        if (isOpen()) {
            m_socket->sendTextMessage(QStringLiteral(R"({"type":"KeepAlive"})"));
        }
    });

    m_reconnect->setSingleShot(true);
    connect(m_reconnect, &QTimer::timeout, this, &DeepgramClient::openSocket);
    connect(m_socket, &QWebSocket::connected, this, [this] {
        m_reconnectAttempts = 0;
        m_keepAlive->start();
        emit connected();
    });
    connect(m_socket, &QWebSocket::textMessageReceived, this, &DeepgramClient::handleText);
    connect(m_socket, &QWebSocket::disconnected, this, [this] {
        m_keepAlive->stop();
        if (m_closing) {
            emit finished();
        } else {
            scheduleReconnect();
        }
    });
    connect(m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        const auto detail = m_socket->errorString();
        const auto authFailure = detail.contains(QStringLiteral("401"))
            || detail.contains(QStringLiteral("Unauthorized"), Qt::CaseInsensitive);
        const auto rateLimited = detail.contains(QStringLiteral("429"))
            || detail.contains(QStringLiteral("rate"), Qt::CaseInsensitive);
        emit errorOccurred(authFailure
                               ? QStringLiteral("Deepgram rejected the API key")
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
    m_reconnectAttempts = 0;

    QUrl url(QStringLiteral("wss://api.deepgram.com/v1/listen"));
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
    openSocket();
    return true;
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
    if (isOpen()) {
        m_socket->sendTextMessage(QStringLiteral(R"({"type":"CloseStream"})"));
        m_socket->close(QWebSocketProtocol::CloseCodeNormal, QStringLiteral("dictation complete"));
    } else {
        emit finished();
    }
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
