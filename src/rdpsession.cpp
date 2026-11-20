#include "rdpsession.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QMutexLocker>
#include <QThread>
#include <cstring>
#include <vector>

#include <freerdp/channels/cliprdr.h>
#include <freerdp/channels/disp.h>
#include <freerdp/client.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/disp.h>
#include <freerdp/codec/color.h>
#include <freerdp/constants.h>
#include <freerdp/event.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/graphics.h>
#include <freerdp/input.h>
#include <freerdp/locale/locale.h>
#include <freerdp/scancode.h>
#include <freerdp/settings.h>
#include <winpr/input.h>
#include <winpr/synch.h>
#include <winpr/user.h>

quint32 rdpScancode(quint32 xkbKeycode) {
    const DWORD vk = GetVirtualKeyCodeFromKeycode(xkbKeycode, WINPR_KEYCODE_TYPE_XKB);
    if (vk == 0)
        return 0;
    return GetVirtualScanCodeFromVirtualKeyCode(vk, WINPR_KBD_TYPE_IBM_ENHANCED);
}

QRectF fitFrame(const QSize &frame, const QSizeF &box) {
    if (frame.isEmpty() || box.isEmpty())
        return {};
    const qreal scale = qMin(box.width() / frame.width(), box.height() / frame.height());
    const QSizeF size(frame.width() * scale, frame.height() * scale);
    return QRectF(QPointF((box.width() - size.width()) / 2, (box.height() - size.height()) / 2), size);
}

namespace {

// libfreerdp's context, with room for the session it belongs to.
struct OrdpContext {
    rdpClientContext common;
    RdpSession *session;
};

struct OrdpPointer {
    rdpPointer pointer;
    QImage *image;
};

RdpSession *sessionOf(rdpContext *context) { return reinterpret_cast<OrdpContext *>(context)->session; }

} // namespace

// libfreerdp's callbacks; a friend, so they reach the session's insides.
struct RdpCallbacks {
    static BOOL clientNew(freerdp *instance, rdpContext *) {
        instance->PreConnect = preConnect;
        instance->PostConnect = postConnect;
        instance->PostDisconnect = postDisconnect;
        instance->AuthenticateEx = authenticate;
        instance->VerifyCertificateEx = verifyCertificate;
        instance->VerifyChangedCertificateEx = verifyChangedCertificate;
        instance->LoadChannels = freerdp_client_load_channels;
        return TRUE;
    }

    static BOOL preConnect(freerdp *instance) {
        rdpContext *context = instance->context;
        rdpSettings *settings = context->settings;
        RdpSession *s = sessionOf(context);
        const QSize size(qBound(200, s->m_wanted.width() & ~1, 8192), qBound(200, s->m_wanted.height(), 8192));
        if (!freerdp_settings_set_uint32(settings, FreeRDP_OsMajorType, OSMAJORTYPE_UNIX) ||
            !freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, UINT32(size.width())) ||
            !freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, UINT32(size.height())))
            return FALSE;
        if (s->m_wantedScale > 100) {
            if (!freerdp_settings_set_uint32(settings, FreeRDP_DesktopScaleFactor, UINT32(s->m_wantedScale)) ||
                !freerdp_settings_set_uint32(settings, FreeRDP_DeviceScaleFactor, 100))
                return FALSE;
        }
        s->m_sent = size;
        s->m_sentScale = s->m_wantedScale;
        // The keyboard layout the PC should use: /kbd: in $RDP_OPTS, or the
        // one that goes with this computer's language.
        if (freerdp_settings_get_uint32(settings, FreeRDP_KeyboardLayout) == 0) {
            DWORD layout = 0;
            if (freerdp_detect_keyboard_layout_from_system_locale(&layout) < 0 || layout == 0)
                layout = 0x409;
            if (!freerdp_settings_set_uint32(settings, FreeRDP_KeyboardLayout, layout))
                return FALSE;
        }
        PubSub_SubscribeChannelConnected(context->pubSub, channelConnected);
        PubSub_SubscribeChannelDisconnected(context->pubSub, channelDisconnected);
        return TRUE;
    }

    static BOOL postConnect(freerdp *instance) {
        rdpContext *context = instance->context;
        if (!gdi_init(instance, PIXEL_FORMAT_BGRX32))
            return FALSE;
        rdpPointer pointer{};
        pointer.size = sizeof(OrdpPointer);
        pointer.New = pointerNew;
        pointer.Free = pointerFree;
        pointer.Set = pointerSet;
        pointer.SetNull = pointerSetNull;
        pointer.SetDefault = pointerSetDefault;
        pointer.SetPosition = pointerSetPosition;
        graphics_register_pointer(context->graphics, &pointer);
        context->update->BeginPaint = beginPaint;
        context->update->EndPaint = endPaint;
        context->update->DesktopResize = desktopResize;
        RdpSession *s = sessionOf(context);
        s->m_everConnected = true;
        s->copyFrame();
        s->setState(RdpSession::Connected);
        return TRUE;
    }

    static void postDisconnect(freerdp *instance) {
        rdpContext *context = instance->context;
        PubSub_UnsubscribeChannelConnected(context->pubSub, channelConnected);
        PubSub_UnsubscribeChannelDisconnected(context->pubSub, channelDisconnected);
        gdi_free(instance);
    }

    // Asked for only when credentials are missing: the window always gives
    // a user, so that is the password left empty.
    static BOOL authenticate(freerdp *instance, char **, char **password, char **, rdp_auth_reason reason) {
        if (reason == GW_AUTH_HTTP || reason == GW_AUTH_RDG || reason == GW_AUTH_RPC)
            return TRUE;
        if (!password || !*password || !**password) {
            sessionOf(instance->context)->m_passwordMissing = true;
            return FALSE;
        }
        return TRUE;
    }

    // /cert:tofu accepts and stores a PC's first certificate without asking;
    // should this be called anyway, the same is done.
    static DWORD verifyCertificate(freerdp *, const char *, UINT16, const char *, const char *, const char *,
                                   const char *, DWORD) {
        return 1;
    }

    // A certificate other than the stored one: refused, and said so.
    static DWORD verifyChangedCertificate(freerdp *instance, const char *, UINT16, const char *, const char *,
                                          const char *, const char *, const char *, const char *, const char *,
                                          DWORD) {
        sessionOf(instance->context)->m_certificateChanged = true;
        return 0;
    }

    static BOOL beginPaint(rdpContext *context) {
        HGDI_WND window = context->gdi->primary->hdc->hwnd;
        window->invalid->null = TRUE;
        window->ninvalid = 0;
        return TRUE;
    }

    static BOOL endPaint(rdpContext *context) {
        sessionOf(context)->copyFrame();
        return TRUE;
    }

    static BOOL desktopResize(rdpContext *context) {
        rdpSettings *settings = context->settings;
        if (!gdi_resize(context->gdi, freerdp_settings_get_uint32(settings, FreeRDP_DesktopWidth),
                        freerdp_settings_get_uint32(settings, FreeRDP_DesktopHeight)))
            return FALSE;
        sessionOf(context)->copyFrame();
        return TRUE;
    }

    // The PC's mouse pointer, drawn by this computer.
    static BOOL pointerNew(rdpContext *context, rdpPointer *pointer) {
        auto *own = reinterpret_cast<OrdpPointer *>(pointer);
        own->image = nullptr;
        if (pointer->width == 0 || pointer->height == 0)
            return TRUE;
        QImage image(int(pointer->width), int(pointer->height), QImage::Format_ARGB32);
        if (!freerdp_image_copy_from_pointer_data(image.bits(), PIXEL_FORMAT_BGRA32, UINT32(image.bytesPerLine()), 0,
                                                  0, pointer->width, pointer->height, pointer->xorMaskData,
                                                  pointer->lengthXorMask, pointer->andMaskData,
                                                  pointer->lengthAndMask, pointer->xorBpp, &context->gdi->palette))
            return FALSE;
        own->image = new QImage(std::move(image));
        return TRUE;
    }
    static void pointerFree(rdpContext *, rdpPointer *pointer) {
        auto *own = reinterpret_cast<OrdpPointer *>(pointer);
        delete own->image;
        own->image = nullptr;
    }
    static BOOL pointerSet(rdpContext *context, rdpPointer *pointer) {
        auto *own = reinterpret_cast<OrdpPointer *>(pointer);
        RdpSession *s = sessionOf(context);
        if (own->image)
            emit s->cursorChanged(RdpSession::ImageCursor, *own->image, QPoint(int(pointer->xPos), int(pointer->yPos)));
        else
            emit s->cursorChanged(RdpSession::HiddenCursor, QImage(), QPoint());
        return TRUE;
    }
    static BOOL pointerSetNull(rdpContext *context) {
        emit sessionOf(context)->cursorChanged(RdpSession::HiddenCursor, QImage(), QPoint());
        return TRUE;
    }
    static BOOL pointerSetDefault(rdpContext *context) {
        emit sessionOf(context)->cursorChanged(RdpSession::DefaultCursor, QImage(), QPoint());
        return TRUE;
    }
    static BOOL pointerSetPosition(rdpContext *, UINT32, UINT32) { return TRUE; }

    static void channelConnected(void *context, const ChannelConnectedEventArgs *e) {
        freerdp_client_OnChannelConnectedEventHandler(context, e);
        RdpSession *s = sessionOf(static_cast<rdpContext *>(context));
        if (strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0) {
            auto *disp = static_cast<DispClientContext *>(e->pInterface);
            disp->custom = s;
            disp->DisplayControlCaps = displayControlCaps;
            s->m_disp = disp;
        } else if (strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME) == 0) {
            auto *cliprdr = static_cast<CliprdrClientContext *>(e->pInterface);
            cliprdr->custom = s;
            cliprdr->MonitorReady = clipboardReady;
            cliprdr->ServerCapabilities = clipboardCapabilities;
            cliprdr->ServerFormatList = clipboardFormats;
            cliprdr->ServerFormatListResponse = clipboardFormatsAnswered;
            cliprdr->ServerFormatDataRequest = clipboardWanted;
            cliprdr->ServerFormatDataResponse = clipboardData;
            s->m_cliprdr = cliprdr;
        }
    }

    static void channelDisconnected(void *context, const ChannelDisconnectedEventArgs *e) {
        RdpSession *s = sessionOf(static_cast<rdpContext *>(context));
        if (strcmp(e->name, DISP_DVC_CHANNEL_NAME) == 0)
            s->m_disp = nullptr;
        else if (strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME) == 0)
            s->m_cliprdr = nullptr;
        freerdp_client_OnChannelDisconnectedEventHandler(context, e);
    }

    // The PC can take a new size from now on.
    static UINT displayControlCaps(DispClientContext *disp, UINT32, UINT32, UINT32) {
        auto *s = static_cast<RdpSession *>(disp->custom);
        s->post([s] {
            s->m_dispReady = true;
            s->sendLayout();
        });
        return CHANNEL_RC_OK;
    }

    // Clipboard text, both ways (CF_UNICODETEXT only).
    static UINT clipboardReady(CliprdrClientContext *cliprdr, const CLIPRDR_MONITOR_READY *) {
        CLIPRDR_GENERAL_CAPABILITY_SET general{};
        general.capabilitySetType = CB_CAPSTYPE_GENERAL;
        general.capabilitySetLength = CB_CAPSTYPE_GENERAL_LEN;
        general.version = CB_CAPS_VERSION_2;
        general.generalFlags = CB_USE_LONG_FORMAT_NAMES;
        CLIPRDR_CAPABILITIES capabilities{};
        capabilities.common.msgType = CB_CLIP_CAPS;
        capabilities.cCapabilitiesSets = 1;
        capabilities.capabilitySets = reinterpret_cast<CLIPRDR_CAPABILITY_SET *>(&general);
        const UINT status = cliprdr->ClientCapabilities(cliprdr, &capabilities);
        if (status != CHANNEL_RC_OK)
            return status;
        static_cast<RdpSession *>(cliprdr->custom)->offerClipboard();
        return CHANNEL_RC_OK;
    }
    static UINT clipboardCapabilities(CliprdrClientContext *, const CLIPRDR_CAPABILITIES *) { return CHANNEL_RC_OK; }
    static UINT clipboardFormats(CliprdrClientContext *cliprdr, const CLIPRDR_FORMAT_LIST *list) {
        CLIPRDR_FORMAT_LIST_RESPONSE answer{};
        answer.common.msgType = CB_FORMAT_LIST_RESPONSE;
        answer.common.msgFlags = CB_RESPONSE_OK;
        const UINT status = cliprdr->ClientFormatListResponse(cliprdr, &answer);
        if (status != CHANNEL_RC_OK)
            return status;
        for (UINT32 i = 0; i < list->numFormats; ++i) {
            if (list->formats[i].formatId == CF_UNICODETEXT) {
                CLIPRDR_FORMAT_DATA_REQUEST request{};
                request.common.msgType = CB_FORMAT_DATA_REQUEST;
                request.requestedFormatId = CF_UNICODETEXT;
                return cliprdr->ClientFormatDataRequest(cliprdr, &request);
            }
        }
        return CHANNEL_RC_OK;
    }
    static UINT clipboardFormatsAnswered(CliprdrClientContext *, const CLIPRDR_FORMAT_LIST_RESPONSE *) {
        return CHANNEL_RC_OK;
    }
    static UINT clipboardWanted(CliprdrClientContext *cliprdr, const CLIPRDR_FORMAT_DATA_REQUEST *request) {
        auto *s = static_cast<RdpSession *>(cliprdr->custom);
        CLIPRDR_FORMAT_DATA_RESPONSE answer{};
        answer.common.msgType = CB_FORMAT_DATA_RESPONSE;
        QByteArray data;
        if (request->requestedFormatId == CF_UNICODETEXT) {
            QString text;
            {
                QMutexLocker lock(&s->m_clipboardLock);
                text = s->m_localText;
            }
            text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace('\n', QStringLiteral("\r\n"));
            data = QByteArray(reinterpret_cast<const char *>(text.utf16()), text.size() * 2);
            data.append(2, '\0');
            answer.common.msgFlags = CB_RESPONSE_OK;
            answer.common.dataLen = UINT32(data.size());
            answer.requestedFormatData = reinterpret_cast<const BYTE *>(data.constData());
        } else {
            answer.common.msgFlags = CB_RESPONSE_FAIL;
        }
        return cliprdr->ClientFormatDataResponse(cliprdr, &answer);
    }
    static UINT clipboardData(CliprdrClientContext *cliprdr, const CLIPRDR_FORMAT_DATA_RESPONSE *answer) {
        if (!(answer->common.msgFlags & CB_RESPONSE_OK) || !answer->requestedFormatData)
            return CHANNEL_RC_OK;
        auto *s = static_cast<RdpSession *>(cliprdr->custom);
        const auto *units = reinterpret_cast<const char16_t *>(answer->requestedFormatData);
        qsizetype length = 0;
        const qsizetype most = qsizetype(answer->common.dataLen / 2);
        while (length < most && units[length] != 0)
            ++length;
        QString text = QString::fromUtf16(units, length);
        text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        {
            QMutexLocker lock(&s->m_clipboardLock);
            s->m_localText = text; // what this computer's clipboard is about to hold
        }
        emit s->remoteClipboard(text);
        return CHANNEL_RC_OK;
    }

    static BOOL noWindowEvents(freerdp *) { return TRUE; }
};

RdpSession::RdpSession(const QString &address, const QStringList &arguments, const QString &password, QObject *parent)
    : QObject(parent), m_address(address), m_arguments(arguments), m_password(password) {
    m_wake = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    RDP_CLIENT_ENTRY_POINTS entry{};
    entry.Size = sizeof(entry);
    entry.Version = RDP_CLIENT_INTERFACE_VERSION;
    entry.ContextSize = sizeof(OrdpContext);
    entry.ClientNew = RdpCallbacks::clientNew;
    m_context = freerdp_client_context_new(&entry);
    if (m_context)
        reinterpret_cast<OrdpContext *>(m_context)->session = this;
}

RdpSession::~RdpSession() {
    stop();
    if (m_thread.joinable())
        m_thread.join();
    if (m_context)
        freerdp_client_context_free(m_context);
    if (m_wake)
        CloseHandle(m_wake);
}

void RdpSession::setState(State state) {
    if (m_state.exchange(state) == state)
        return;
    // QML binds to the state, so the change is announced on the GUI thread.
    if (QThread::currentThread() == thread())
        emit stateChanged();
    else
        QMetaObject::invokeMethod(this, &RdpSession::stateChanged, Qt::QueuedConnection);
}

void RdpSession::resize(const QSize &pixels, int scalePercent) {
    if (pixels.isEmpty())
        return;
    const int scale = qBound(100, scalePercent, 500);
    if (m_state == Waiting) {
        m_wanted = pixels;
        m_wantedScale = scale;
        setState(Connecting);
        m_thread = std::thread([this] { run(); });
        return;
    }
    post([this, pixels, scale] {
        m_wanted = pixels;
        m_wantedScale = scale;
        sendLayout();
    });
}

void RdpSession::stop() {
    m_stopping = true;
    if (m_state == Waiting) {
        setState(Ended);
        QMetaObject::invokeMethod(this, [this] { emit finished(0, QString()); }, Qt::QueuedConnection);
        return;
    }
    if (m_context)
        freerdp_abort_connect_context(m_context);
    if (m_wake)
        SetEvent(m_wake);
}

void RdpSession::post(std::function<void()> job) {
    {
        QMutexLocker lock(&m_jobsLock);
        m_jobs.push_back(std::move(job));
    }
    SetEvent(m_wake);
}

void RdpSession::drain() {
    ResetEvent(m_wake);
    std::deque<std::function<void()>> jobs;
    {
        QMutexLocker lock(&m_jobsLock);
        jobs.swap(m_jobs);
    }
    for (auto &job : jobs)
        job();
}

void RdpSession::run() {
    int code = 0;
    QString output;
    auto failure = [this](const QString &what) {
        const UINT32 error = freerdp_get_last_error(m_context);
        QString text = QStringLiteral("%1 %2").arg(QString::fromUtf8(freerdp_get_last_error_name(error)),
                                                   QString::fromUtf8(freerdp_get_last_error_string(error)));
        if (m_certificateChanged)
            text = QStringLiteral("The certificate changed: ") + text;
        if (!what.isEmpty())
            text = what + ' ' + text;
        // Shaped like FreeRDP's own log lines, which connectionProblem() reads.
        return QStringLiteral("[ERROR][o-rdp]: ") + text.trimmed();
    };

    if (!m_context) {
        code = 3;
        output = QStringLiteral("[ERROR] libfreerdp could not make a connection context");
    } else {
        // The arguments as FreeRDP's own clients take them; the password goes
        // straight into the settings.
        std::vector<QByteArray> storage{QByteArrayLiteral("o-rdp")};
        for (const QString &argument : m_arguments)
            storage.push_back(argument.toUtf8());
        std::vector<char *> argv;
        for (QByteArray &argument : storage)
            argv.push_back(argument.data());
        rdpSettings *settings = m_context->settings;
        if (freerdp_client_settings_parse_command_line(settings, int(argv.size()), argv.data(), FALSE) < 0) {
            code = 3;
            output = QStringLiteral("[ERROR] FreeRDP did not take the arguments: %1").arg(m_arguments.join(' '));
        } else {
            const QByteArray password = m_password.toUtf8();
            m_password.fill(QChar(0));
            m_password.clear();
            if (!password.isEmpty() && !freerdp_settings_set_string(settings, FreeRDP_Password, password.constData())) {
                code = 3;
                output = QStringLiteral("[ERROR] The password could not be set");
            }
        }
    }

    if (code == 0) {
        freerdp *instance = m_context->instance;
        if (!freerdp_connect(instance)) {
            if (!m_stopping) {
                code = 3;
                output = m_passwordMissing ? QStringLiteral("PASSWORD_MISSING") : failure(QString());
            }
        } else {
            // Reconnects after the network drops; one the PC keeps dropping
            // at once (three within half a minute) is given up.
            QList<qint64> reconnects;
            QElapsedTimer clock;
            clock.start();
            while (!freerdp_shall_disconnect_context(m_context)) {
                HANDLE handles[MAXIMUM_WAIT_OBJECTS] = {};
                DWORD count = freerdp_get_event_handles(m_context, handles, MAXIMUM_WAIT_OBJECTS - 1);
                if (count == 0)
                    break;
                handles[count++] = m_wake;
                if (WaitForMultipleObjects(count, handles, FALSE, 100) == WAIT_FAILED)
                    break;
                drain();
                if (m_stopping)
                    break;
                if (!freerdp_check_event_handles(m_context)) {
                    while (!reconnects.isEmpty() && clock.elapsed() - reconnects.first() > 30000)
                        reconnects.removeFirst();
                    if (!m_stopping && reconnects.size() < 3 &&
                        client_auto_reconnect_ex(instance, RdpCallbacks::noWindowEvents)) {
                        reconnects.append(clock.elapsed());
                        continue;
                    }
                    // The PC ending the session (logged off, disconnected from
                    // its Start menu, another connection) is a normal end.
                    if (!m_stopping && freerdp_error_info(instance) == 0 && freerdp_get_last_error(m_context) != 0) {
                        code = 3;
                        output = failure(QString());
                    }
                    break;
                }
            }
            freerdp_disconnect(instance);
        }
    }
    setState(Ended);
    emit finished(code, output);
}

void RdpSession::copyFrame() {
    rdpGdi *gdi = m_context->gdi;
    if (!gdi || !gdi->primary_buffer)
        return;
    HGDI_WND window = gdi->primary->hdc->hwnd;
    const QSize size(gdi->width, gdi->height);
    {
        QMutexLocker lock(&m_frameLock);
        QRect dirty;
        if (m_frame.size() != size) {
            m_frame = QImage(size, QImage::Format_RGB32);
            dirty = QRect(QPoint(), size);
        } else if (!window->invalid->null) {
            dirty = QRect(window->invalid->x, window->invalid->y, window->invalid->w, window->invalid->h) &
                    QRect(QPoint(), size);
        }
        for (int y = dirty.top(); !dirty.isEmpty() && y <= dirty.bottom(); ++y)
            memcpy(m_frame.scanLine(y) + dirty.left() * 4, gdi->primary_buffer + size_t(y) * gdi->stride + dirty.left() * 4,
                   size_t(dirty.width()) * 4);
        if (dirty.isEmpty())
            return;
    }
    window->invalid->null = TRUE;
    window->ninvalid = 0;
    if (!m_frameQueued.exchange(true))
        emit frameChanged();
}

void RdpSession::withFrame(const std::function<void(const QImage &)> &f) const {
    QMutexLocker lock(&m_frameLock);
    const_cast<RdpSession *>(this)->m_frameQueued = false;
    f(m_frame);
}

QSize RdpSession::frameSize() const {
    QMutexLocker lock(&m_frameLock);
    return m_frame.size();
}

void RdpSession::sendLayout() {
    DispClientContext *disp = m_disp;
    if (!disp || !m_dispReady || !m_everConnected)
        return;
    // Even widths only, within what the display channel allows.
    const QSize size(qBound(DISPLAY_CONTROL_MIN_MONITOR_WIDTH, m_wanted.width() & ~1, DISPLAY_CONTROL_MAX_MONITOR_WIDTH),
                     qBound(DISPLAY_CONTROL_MIN_MONITOR_HEIGHT, m_wanted.height(), DISPLAY_CONTROL_MAX_MONITOR_HEIGHT));
    if (size == m_sent && m_wantedScale == m_sentScale)
        return;
    DISPLAY_CONTROL_MONITOR_LAYOUT layout{};
    layout.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
    layout.Width = UINT32(size.width());
    layout.Height = UINT32(size.height());
    layout.Orientation = ORIENTATION_LANDSCAPE;
    layout.DesktopScaleFactor = UINT32(m_wantedScale);
    layout.DeviceScaleFactor = 100;
    if (disp->SendMonitorLayout(disp, 1, &layout) == CHANNEL_RC_OK) {
        m_sent = size;
        m_sentScale = m_wantedScale;
    }
}

void RdpSession::offerClipboard() {
    CliprdrClientContext *cliprdr = m_cliprdr;
    if (!cliprdr)
        return;
    bool any = false;
    {
        QMutexLocker lock(&m_clipboardLock);
        any = !m_localText.isEmpty();
    }
    CLIPRDR_FORMAT text{};
    text.formatId = CF_UNICODETEXT;
    CLIPRDR_FORMAT_LIST list{};
    list.common.msgType = CB_FORMAT_LIST;
    list.numFormats = any ? 1 : 0;
    list.formats = any ? &text : nullptr;
    (void)cliprdr->ClientFormatList(cliprdr, &list);
}

void RdpSession::setLocalClipboard(const QString &text) {
    {
        QMutexLocker lock(&m_clipboardLock);
        if (text == m_localText)
            return; // nothing new, or the PC's own text coming back
        m_localText = text;
    }
    post([this] { offerClipboard(); });
}

void RdpSession::sendKey(quint32 xkbKeycode, bool down, bool repeat) {
    const quint32 scancode = rdpScancode(xkbKeycode);
    if (scancode == 0)
        return;
    post([this, scancode, down, repeat] {
        if (m_state == Connected)
            (void)freerdp_input_send_keyboard_event_ex(m_context->input, down, repeat, scancode);
    });
}

void RdpSession::sendMouse(quint16 flags, const QPoint &position) {
    post([this, flags, position] {
        if (m_state == Connected)
            (void)freerdp_input_send_mouse_event(m_context->input, flags, UINT16(qMax(0, position.x())),
                                                 UINT16(qMax(0, position.y())));
    });
}

void RdpSession::sendExtendedMouse(quint16 flags, const QPoint &position) {
    post([this, flags, position] {
        if (m_state == Connected)
            (void)freerdp_input_send_extended_mouse_event(m_context->input, flags, UINT16(qMax(0, position.x())),
                                                          UINT16(qMax(0, position.y())));
    });
}

void RdpSession::sendWheel(int delta, bool horizontal, const QPoint &position) {
    // Turns of at most 255 each, the sign in the ninth bit.
    while (delta != 0) {
        const int step = qBound(-255, delta, 255);
        delta -= step;
        sendMouse(quint16((horizontal ? PTR_FLAGS_HWHEEL : PTR_FLAGS_WHEEL) | (step & WheelRotationMask)), position);
    }
}

void RdpSession::sendCtrlAltDel() {
    post([this] {
        if (m_state != Connected)
            return;
        rdpInput *input = m_context->input;
        const UINT32 keys[] = {RDP_SCANCODE_LCONTROL, RDP_SCANCODE_LMENU, RDP_SCANCODE_DELETE};
        for (UINT32 key : keys)
            (void)freerdp_input_send_keyboard_event_ex(input, TRUE, FALSE, key);
        for (int i = 2; i >= 0; --i)
            (void)freerdp_input_send_keyboard_event_ex(input, FALSE, FALSE, keys[i]);
    });
}
