#pragma once

#include "common/ipc_protocol.hpp"

#include <QObject>

namespace vocaltri::ui {

class SessionViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString finalText READ finalText NOTIFY transcriptChanged)
    Q_PROPERTY(QString interimText READ interimText NOTIFY transcriptChanged)
    Q_PROPERTY(qreal level READ level NOTIFY levelChanged)
    Q_PROPERTY(qint64 elapsedMs READ elapsedMs NOTIFY elapsedChanged)

public:
    explicit SessionViewModel(QObject *parent = nullptr)
        : QObject(parent) {}

    QString state() const { return ipc::stateName(m_state); }
    QString finalText() const { return m_finalText; }
    QString interimText() const { return m_interimText; }
    qreal level() const { return m_level; }
    qint64 elapsedMs() const { return m_elapsedMs; }

    void setState(ipc::SessionState state);
    void setTranscript(QString finalText, QString interimText);
    void setLevel(qreal level);
    void setElapsedMs(qint64 elapsedMs);
    Q_INVOKABLE void requestCancel() { emit cancelRequested(); }

signals:
    void stateChanged();
    void transcriptChanged();
    void levelChanged();
    void elapsedChanged();
    void cancelRequested();

private:
    ipc::SessionState m_state = ipc::SessionState::Idle;
    QString m_finalText;
    QString m_interimText;
    qreal m_level = 0;
    qint64 m_elapsedMs = 0;
};

} // namespace vocaltri::ui
