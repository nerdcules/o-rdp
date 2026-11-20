#pragma once

#include <QPointer>
#include <QQuickItem>
#include <QSet>
#include <QTimer>

#include "rdpsession.h"

// Draws a session's desktop, fitted to the item, and sends it the keys and
// the mouse while it has them. Its size, in device pixels, becomes the
// desktop's size.
class RdpView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject *session READ session WRITE setSession NOTIFY sessionChanged)

public:
    explicit RdpView(QQuickItem *parent = nullptr);

    QObject *session() const { return m_session; }
    void setSession(QObject *session);

    Q_INVOKABLE void sendCtrlAltDel();

signals:
    void sessionChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void geometryChange(const QRectF &now, const QRectF &before) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void sizeChanged();
    void sendSize();
    void releaseKeys();
    void showCursor(int kind, const QImage &image, const QPoint &hotspot);
    QPoint remote(const QPointF &position) const;
    void button(Qt::MouseButton button, bool down, const QPointF &position);

    QPointer<RdpSession> m_session;
    QTimer m_resize;
    QSet<quint32> m_keys;
    bool m_dirty = true;
};
