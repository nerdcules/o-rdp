#pragma once

#include <QImage>
#include <QMutex>
#include <QObject>
#include <QPoint>
#include <QRectF>
#include <QSize>
#include <QStringList>
#include <atomic>
#include <deque>
#include <functional>
#include <thread>

struct rdp_context;
struct s_disp_client_context;
struct s_cliprdr_client_context;

// The RDP scancode (with 0x100 for the extended ones) of the key an XKB
// keycode names, as Qt hands it over in QKeyEvent::nativeScanCode() on
// Wayland and X11; 0 for a key RDP has no code for.
quint32 rdpScancode(quint32 xkbKeycode);

// Where a frame of the given size is drawn inside a box: as large as fits,
// the aspect kept, centred.
QRectF fitFrame(const QSize &frame, const QSizeF &box);

// One remote desktop, run by libfreerdp on a thread of its own and drawn by
// RdpView inside the o-rdp window. Everything public is called from the GUI
// thread; input goes into a queue the session's thread plays.
class RdpSession : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString address READ address CONSTANT)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)

public:
    enum State { Waiting, Connecting, Connected, Ended };
    Q_ENUM(State)
    enum CursorKind { DefaultCursor, HiddenCursor, ImageCursor };

    // arguments: FreeRDP's, as freerdpArguments() writes them, without the
    // password, which is set on the connection's settings directly.
    RdpSession(const QString &address, const QStringList &arguments, const QString &password,
               QObject *parent = nullptr);
    ~RdpSession() override;

    QString address() const { return m_address; }
    State state() const { return m_state.load(); }

    // The size the desktop should have, in device pixels, and the scale
    // (100 = 100 %) Windows should draw it at. The first call connects.
    void resize(const QSize &pixels, int scalePercent);
    void stop();

    void sendKey(quint32 xkbKeycode, bool down, bool repeat);
    void sendMouse(quint16 flags, const QPoint &position);
    void sendExtendedMouse(quint16 flags, const QPoint &position);
    void sendWheel(int delta, bool horizontal, const QPoint &position);
    void sendCtrlAltDel();
    // Text on this computer's clipboard, offered to the PC.
    void setLocalClipboard(const QString &text);

    // Runs f with the last frame (Format_RGB32), under the frame's lock.
    void withFrame(const std::function<void(const QImage &)> &f) const;
    QSize frameSize() const;

signals:
    void stateChanged();
    void frameChanged();
    void cursorChanged(int kind, const QImage &image, const QPoint &hotspot);
    void remoteClipboard(const QString &text);
    // code: 0 for a normal end; output: what went wrong, for connectionProblem().
    void finished(int code, const QString &output);

private:
    friend struct RdpCallbacks;

    void run();
    void post(std::function<void()> job);
    void drain();
    void setState(State state);
    void copyFrame();
    void sendLayout();
    void offerClipboard();

    QString m_address;
    QStringList m_arguments;
    QString m_password;
    std::atomic<State> m_state{Waiting};
    std::thread m_thread;
    rdp_context *m_context = nullptr;
    void *m_wake = nullptr;
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_frameQueued{false};

    QMutex m_jobsLock;
    std::deque<std::function<void()>> m_jobs;

    mutable QMutex m_frameLock;
    QImage m_frame;

    // Set from the channels' threads as they come and go.
    std::atomic<s_disp_client_context *> m_disp{nullptr};
    std::atomic<s_cliprdr_client_context *> m_cliprdr{nullptr};
    std::atomic<bool> m_certificateChanged{false};
    std::atomic<bool> m_passwordMissing{false};

    // Session thread only (and the GUI thread before it starts).
    bool m_dispReady = false;
    QSize m_wanted;
    int m_wantedScale = 100;
    QSize m_sent;
    int m_sentScale = 100;
    bool m_everConnected = false;

    QMutex m_clipboardLock;
    QString m_localText;
};
