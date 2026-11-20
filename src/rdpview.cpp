#include "rdpview.h"

#include <QCursor>
#include <QPixmap>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>

#include <freerdp/input.h>

RdpView::RdpView(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    // Sizes settle while a window is dragged larger; the PC is asked for
    // the new one when they stop.
    m_resize.setSingleShot(true);
    m_resize.setInterval(250);
    connect(&m_resize, &QTimer::timeout, this, &RdpView::sendSize);
}

void RdpView::setSession(QObject *session) {
    auto *next = qobject_cast<RdpSession *>(session);
    if (next == m_session)
        return;
    releaseKeys();
    if (m_session)
        disconnect(m_session, nullptr, this, nullptr);
    m_session = next;
    unsetCursor();
    if (m_session) {
        connect(m_session, &RdpSession::frameChanged, this, [this] {
            m_dirty = true;
            update();
        });
        connect(m_session, &RdpSession::cursorChanged, this, &RdpView::showCursor);
        sendSize();
    }
    m_dirty = true;
    update();
    emit sessionChanged();
}

void RdpView::sendCtrlAltDel() {
    if (m_session)
        m_session->sendCtrlAltDel();
}

void RdpView::sendSize() {
    if (!m_session || !window() || width() < 1 || height() < 1)
        return;
    const qreal ratio = window()->effectiveDevicePixelRatio();
    m_session->resize(QSize(qRound(width() * ratio), qRound(height() * ratio)), qRound(ratio * 100));
}

void RdpView::sizeChanged() {
    if (m_session && m_session->state() == RdpSession::Waiting)
        sendSize(); // the first size connects, without waiting
    else
        m_resize.start();
    update();
}

void RdpView::geometryChange(const QRectF &now, const QRectF &before) {
    QQuickItem::geometryChange(now, before);
    if (now.size() != before.size())
        sizeChanged();
}

void RdpView::itemChange(ItemChange change, const ItemChangeData &value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemDevicePixelRatioHasChanged || change == ItemSceneChange)
        sizeChanged();
}

QSGNode *RdpView::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    auto *node = static_cast<QSGSimpleTextureNode *>(old);
    if (!m_session) {
        delete node;
        return nullptr;
    }
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        m_dirty = true;
    }
    if (m_dirty) {
        m_session->withFrame([&](const QImage &frame) {
            if (!frame.isNull())
                node->setTexture(window()->createTextureFromImage(frame));
        });
        m_dirty = false;
    }
    if (!node->texture()) {
        delete node;
        return nullptr;
    }
    const QSize frame = node->texture()->textureSize();
    const QRectF rect = fitFrame(frame, size());
    node->setRect(rect);
    // One pixel of the PC's to one of the screen's stays sharp.
    const qreal ratio = window()->effectiveDevicePixelRatio();
    const bool exact = qAbs(rect.width() * ratio - frame.width()) < 0.5 && qAbs(rect.height() * ratio - frame.height()) < 0.5;
    node->setFiltering(exact ? QSGTexture::Nearest : QSGTexture::Linear);
    return node;
}

QPoint RdpView::remote(const QPointF &position) const {
    const QSize frame = m_session ? m_session->frameSize() : QSize();
    const QRectF rect = fitFrame(frame, size());
    if (rect.isEmpty())
        return {};
    const int x = int((position.x() - rect.x()) * frame.width() / rect.width());
    const int y = int((position.y() - rect.y()) * frame.height() / rect.height());
    return {qBound(0, x, frame.width() - 1), qBound(0, y, frame.height() - 1)};
}

void RdpView::showCursor(int kind, const QImage &image, const QPoint &hotspot) {
    if (kind == RdpSession::HiddenCursor) {
        setCursor(Qt::BlankCursor);
        return;
    }
    if (kind == RdpSession::DefaultCursor || image.isNull()) {
        unsetCursor();
        return;
    }
    // The pointer is in the PC's pixels; drawn at the size the desktop is.
    const QSize frame = m_session ? m_session->frameSize() : QSize();
    const QRectF rect = fitFrame(frame, size());
    const qreal ratio = rect.width() > 0 ? frame.width() / rect.width() : 1.0;
    QPixmap pixmap = QPixmap::fromImage(image);
    pixmap.setDevicePixelRatio(ratio);
    setCursor(QCursor(pixmap, qRound(hotspot.x() / ratio), qRound(hotspot.y() / ratio)));
}

void RdpView::keyPressEvent(QKeyEvent *event) {
    const quint32 code = event->nativeScanCode();
    if (!m_session || code == 0) {
        event->ignore();
        return;
    }
    m_keys.insert(code);
    m_session->sendKey(code, true, event->isAutoRepeat());
    event->accept();
}

void RdpView::keyReleaseEvent(QKeyEvent *event) {
    const quint32 code = event->nativeScanCode();
    if (!m_session || code == 0) {
        event->ignore();
        return;
    }
    // A held key repeats as presses; only the real release lets go.
    if (!event->isAutoRepeat()) {
        m_keys.remove(code);
        m_session->sendKey(code, false, false);
    }
    event->accept();
}

void RdpView::focusOutEvent(QFocusEvent *event) {
    releaseKeys();
    QQuickItem::focusOutEvent(event);
}

void RdpView::releaseKeys() {
    if (m_session)
        for (quint32 code : std::as_const(m_keys))
            m_session->sendKey(code, false, false);
    m_keys.clear();
}

void RdpView::button(Qt::MouseButton which, bool down, const QPointF &position) {
    if (!m_session)
        return;
    const QPoint at = remote(position);
    switch (which) {
    case Qt::LeftButton:
        m_session->sendMouse(PTR_FLAGS_BUTTON1 | (down ? PTR_FLAGS_DOWN : 0), at);
        break;
    case Qt::RightButton:
        m_session->sendMouse(PTR_FLAGS_BUTTON2 | (down ? PTR_FLAGS_DOWN : 0), at);
        break;
    case Qt::MiddleButton:
        m_session->sendMouse(PTR_FLAGS_BUTTON3 | (down ? PTR_FLAGS_DOWN : 0), at);
        break;
    case Qt::BackButton:
        m_session->sendExtendedMouse(PTR_XFLAGS_BUTTON1 | (down ? PTR_XFLAGS_DOWN : 0), at);
        break;
    case Qt::ForwardButton:
        m_session->sendExtendedMouse(PTR_XFLAGS_BUTTON2 | (down ? PTR_XFLAGS_DOWN : 0), at);
        break;
    default:
        break;
    }
}

void RdpView::mousePressEvent(QMouseEvent *event) {
    forceActiveFocus(Qt::MouseFocusReason);
    button(event->button(), true, event->position());
    event->accept();
}

void RdpView::mouseReleaseEvent(QMouseEvent *event) {
    button(event->button(), false, event->position());
    event->accept();
}

void RdpView::mouseMoveEvent(QMouseEvent *event) {
    if (m_session)
        m_session->sendMouse(PTR_FLAGS_MOVE, remote(event->position()));
    event->accept();
}

void RdpView::hoverMoveEvent(QHoverEvent *event) {
    if (m_session)
        m_session->sendMouse(PTR_FLAGS_MOVE, remote(event->position()));
    event->accept();
}

void RdpView::wheelEvent(QWheelEvent *event) {
    if (!m_session) {
        event->ignore();
        return;
    }
    const QPoint at = remote(event->position());
    const QPoint delta = event->angleDelta();
    if (delta.y() != 0)
        m_session->sendWheel(delta.y(), false, at);
    // Qt counts left as positive; RDP counts right.
    if (delta.x() != 0)
        m_session->sendWheel(-delta.x(), true, at);
    event->accept();
}
