#include "ui/overlay_window.hpp"

#include "ui/session_view_model.hpp"

#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QTimer>
#include <QtGlobal>

namespace vocaltri::ui {

OverlayWindow::OverlayWindow(QObject *parent)
    : QObject(parent) {}

OverlayWindow::~OverlayWindow() {
    delete m_window;
}

void OverlayWindow::setModel(SessionViewModel *model) {
    if (m_model == model) {
        return;
    }
    m_model = model;
    if (m_window) {
        m_window->rootContext()->setContextProperty(QStringLiteral("session"), m_model);
    }
}

void OverlayWindow::ensureWindow() {
    if (m_window) {
        return;
    }

    if (!m_model) {
        m_model = new SessionViewModel(this);
    }
    m_window = new QQuickView;
    m_window->setResizeMode(QQuickView::SizeRootObjectToView);
    m_window->setColor(Qt::transparent);
    m_window->setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                       | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
    m_window->setTitle(QStringLiteral("vocaltri"));
    m_window->rootContext()->setContextProperty(QStringLiteral("session"), m_model);
    m_window->setSource(QUrl(QStringLiteral("qrc:/qt/qml/Vocaltri/OverlayRoot.qml")));

    if (auto *root = qobject_cast<QQuickItem *>(m_window->rootObject())) {
        connect(root, &QQuickItem::heightChanged, this, &OverlayWindow::updateGeometry);
    }
    connect(m_model, &SessionViewModel::cancelRequested, this, &OverlayWindow::cancelRequested);
    connect(qApp, &QGuiApplication::primaryScreenChanged, this, [this] {
        updateGeometry();
    });
    updateGeometry();
}

void OverlayWindow::updateGeometry() {
    if (!m_window) {
        return;
    }

    auto *screen = m_window->screen();
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        return;
    }

    const auto geometry = screen->geometry();
    const auto width = qMax(1, qRound(geometry.width() * 0.60));
    const auto sideMargin = qRound(geometry.width() * 0.20);
    const auto bottomMargin = qRound(geometry.height() * 0.05);
    const auto root = qobject_cast<QQuickItem *>(m_window->rootObject());
    const auto height = qBound(72, qRound(root ? root->implicitHeight() : 96),
                               qRound(geometry.height() * 0.35));
    m_window->setGeometry(geometry.x() + sideMargin,
                          geometry.y() + geometry.height() - bottomMargin - height,
                          width,
                          height);
}

void OverlayWindow::setState(const ipc::SessionState state) {
    ensureWindow();
    m_model->setState(state);
    if (state == ipc::SessionState::Idle || state == ipc::SessionState::Done) {
        m_window->hide();
        return;
    }

    updateGeometry();
    m_window->show();
    m_window->raise();
}

} // namespace vocaltri::ui
