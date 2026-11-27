#include "ui/session_view_model.hpp"

namespace vocaltri::ui {

void SessionViewModel::setState(const ipc::SessionState state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
    emit sessionStateChanged(state);
}

void SessionViewModel::setTranscript(QString finalText, QString interimText) {
    if (m_finalText == finalText && m_interimText == interimText) {
        return;
    }
    m_finalText = std::move(finalText);
    m_interimText = std::move(interimText);
    emit transcriptChanged();
}

void SessionViewModel::setLevel(const qreal level) {
    const auto clamped = qBound<qreal>(0, level, 1);
    if (qFuzzyCompare(m_level, clamped)) {
        return;
    }
    m_level = clamped;
    emit levelChanged();
}

void SessionViewModel::setElapsedMs(const qint64 elapsedMs) {
    if (m_elapsedMs == elapsedMs) {
        return;
    }
    m_elapsedMs = elapsedMs;
    emit elapsedChanged();
}

} // namespace vocaltri::ui
