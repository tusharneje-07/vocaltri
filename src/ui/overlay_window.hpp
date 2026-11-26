#pragma once

#include "common/ipc_protocol.hpp"

#include <QObject>
#include <memory>

class QQuickView;

namespace vocaltri::ui {

class SessionViewModel;

class OverlayWindow final : public QObject {
    Q_OBJECT

public:
    explicit OverlayWindow(QObject *parent = nullptr);
    ~OverlayWindow() override;

    void setState(ipc::SessionState state);

signals:
    void cancelRequested();

private:
    void ensureWindow();
    void updateGeometry();

    QQuickView *m_window = nullptr;
    SessionViewModel *m_model = nullptr;
};

} // namespace vocaltri::ui
