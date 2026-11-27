#include "input/text_injector.hpp"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QSocketNotifier>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <memory>
#include <string>

#if defined(VOCALTRI_HAS_LIBEI) && VOCALTRI_HAS_LIBEI
#include <libei.h>
#include <liboeffis.h>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include <cerrno>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>
#endif

namespace vocaltri::input {

#if defined(VOCALTRI_HAS_LIBEI) && VOCALTRI_HAS_LIBEI

class NativeEisSession final : public QObject {
public:
    using FailureHandler = std::function<void(const QStringList &)>;

    static constexpr qsizetype kMaxPendingSegments = 128;
    static constexpr qsizetype kMaxPendingCharacters = 256 * 1024;
    static constexpr qsizetype kMaxPendingUtf8Bytes = 1024 * 1024;

    NativeEisSession(QObject *parent, FailureHandler failureHandler)
        : QObject(parent)
        , m_failureHandler(std::move(failureHandler)) {}

    ~NativeEisSession() override { releaseSession(); }

    bool insert(const QString &text) {
        if (m_failed) {
            return false;
        }

        if (m_pending.size() >= kMaxPendingSegments
            || text.size() > kMaxPendingCharacters - m_pendingCharacters) {
            fail(QStringLiteral("The native input queue exceeded its safety limit"), &text);
            return false;
        }

        // Bound UTF-8 conversion work by checking the character limit first.
        const auto textBytes = text.toUtf8().size();
        if (textBytes > kMaxPendingUtf8Bytes - m_pendingUtf8Bytes) {
            fail(QStringLiteral("The native input queue exceeded its safety limit"), &text);
            return false;
        }
        m_pending.append(text);
        m_pendingCharacters += text.size();
        m_pendingUtf8Bytes += textBytes;
        if (!m_oeffis) {
            beginPortalSession();
        }
        flushPending();
        return !m_failed;
    }

private:
    void beginPortalSession() {
        m_oeffis = oeffis_new(this);
        if (!m_oeffis) {
            fail(QStringLiteral("Could not create the RemoteDesktop portal session"));
            return;
        }

        const auto fd = oeffis_get_fd(m_oeffis);
        if (fd < 0) {
            fail(QStringLiteral("Could not monitor the RemoteDesktop portal session"));
            return;
        }
        m_portalNotifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(m_portalNotifier, &QSocketNotifier::activated, this,
                [this] { dispatchPortal(); });

        // The portal request is intentionally created only after the user asks
        // to insert text. A CONNECTED_TO_EIS event is the permission boundary.
        oeffis_create_session(m_oeffis, OEFFIS_DEVICE_KEYBOARD);
    }

    void dispatchPortal() {
        if (!m_oeffis || m_failed) {
            return;
        }
        oeffis_dispatch(m_oeffis);
        for (auto event = oeffis_get_event(m_oeffis);
             event != OEFFIS_EVENT_NONE;
             event = oeffis_get_event(m_oeffis)) {
            switch (event) {
            case OEFFIS_EVENT_CONNECTED_TO_EIS:
                if (!setupEis()) {
                    fail(QStringLiteral("Could not initialize the approved EIS connection"));
                    return;
                }
                break;
            case OEFFIS_EVENT_CLOSED:
                fail(QStringLiteral("RemoteDesktop permission was closed"));
                return;
            case OEFFIS_EVENT_DISCONNECTED: {
                const auto *message = oeffis_get_error_message(m_oeffis);
                fail(message && *message
                         ? QString::fromUtf8(message)
                         : QStringLiteral("RemoteDesktop portal disconnected"));
                return;
            }
            case OEFFIS_EVENT_NONE:
                break;
            }
        }
    }

    bool setupEis() {
        const auto fd = oeffis_get_eis_fd(m_oeffis);
        if (fd < 0) {
            return false;
        }
        m_ei = ei_new_sender(this);
        if (!m_ei) {
            close(fd);
            return false;
        }
        ei_configure_name(m_ei, "vocaltri");
        if (ei_setup_backend_fd(m_ei, fd) < 0) {
            // ei_setup_backend_fd takes ownership of fd on invocation.
            return false;
        }
        const auto eiFd = ei_get_fd(m_ei);
        if (eiFd < 0) {
            return false;
        }
        m_eisNotifier = new QSocketNotifier(eiFd, QSocketNotifier::Read, this);
        connect(m_eisNotifier, &QSocketNotifier::activated, this,
                [this] { dispatchEis(); });
        return true;
    }

    void dispatchEis() {
        if (!m_ei || m_failed) {
            return;
        }
        ei_dispatch(m_ei);
        for (auto *event = ei_get_event(m_ei); event; event = ei_get_event(m_ei)) {
            const auto type = ei_event_get_type(event);
            auto *device = ei_event_get_device(event);
            switch (type) {
            case EI_EVENT_CONNECT:
                break;
            case EI_EVENT_DISCONNECT:
                m_deviceReady = false;
                m_emulating = false;
                m_pressedKeys.clear();
                ei_event_unref(event);
                fail(QStringLiteral("EIS disconnected"));
                return;
            case EI_EVENT_SEAT_ADDED: {
                auto *seat = ei_event_get_seat(event);
                if (seat) {
#if defined(VOCALTRI_HAS_EI_TEXT) && VOCALTRI_HAS_EI_TEXT
                    ei_seat_bind_capabilities(seat, EI_DEVICE_CAP_KEYBOARD,
                                              EI_DEVICE_CAP_TEXT, nullptr);
#else
                    ei_seat_bind_capabilities(seat, EI_DEVICE_CAP_KEYBOARD, nullptr);
#endif
                }
                break;
            }
            case EI_EVENT_DEVICE_ADDED:
                if (device && canTypeWithDevice(device) && !m_keyboard) {
                    m_keyboard = ei_device_ref(device);
                }
                break;
            case EI_EVENT_DEVICE_PAUSED:
                if (device == m_keyboard) {
                    // A pause resets the server-side keyboard state. Do not
                    // send anything until the corresponding resume event.
                    m_deviceReady = false;
                    m_emulating = false;
                    m_pressedKeys.clear();
                }
                break;
            case EI_EVENT_DEVICE_RESUMED:
                if (device && canTypeWithDevice(device)) {
                    if (!m_keyboard) {
                        m_keyboard = ei_device_ref(device);
                    }
                    if (device == m_keyboard) {
                        m_deviceReady = true;
                        ++m_sequence;
                        if (m_sequence == 0) {
                            ++m_sequence;
                        }
                        ei_device_start_emulating(m_keyboard, m_sequence);
                        m_emulating = true;
                    }
                }
                break;
            case EI_EVENT_DEVICE_REMOVED:
                if (device == m_keyboard) {
                    m_emulating = false;
                    m_pressedKeys.clear();
                    ei_device_unref(m_keyboard);
                    m_keyboard = nullptr;
                    m_deviceReady = false;
                }
                break;
            default:
                break;
            }
            ei_event_unref(event);
            if (m_failed) {
                return;
            }
        }
        flushPending();
    }

    struct KeyStroke {
        uint32_t evdevCode = 0;
        bool shift = false;
        bool levelThree = false;
    };

    static bool canTypeWithDevice(struct ei_device *device) {
        if (ei_device_has_capability(device, EI_DEVICE_CAP_KEYBOARD)) {
            return true;
        }
#if defined(VOCALTRI_HAS_EI_TEXT) && VOCALTRI_HAS_EI_TEXT
        return ei_device_has_capability(device, EI_DEVICE_CAP_TEXT);
#else
        return false;
#endif
    }

    static xkb_keysym_t keysymForCodepoint(uint32_t codepoint) {
        switch (codepoint) {
        case '\n':
        case '\r':
            return XKB_KEY_Return;
        case '\t':
            return XKB_KEY_Tab;
        case '\b':
            return XKB_KEY_BackSpace;
        default:
            return xkb_utf32_to_keysym(codepoint);
        }
    }

    bool mapToKeyStrokes(const QString &text, std::vector<KeyStroke> &strokes) const {
        if (!m_keyboard) {
            return false;
        }
        auto *keymapHandle = ei_device_keyboard_get_keymap(m_keyboard);
        if (!keymapHandle || ei_keymap_get_type(keymapHandle) != EI_KEYMAP_TYPE_XKB) {
            return false;
        }
        const auto keymapFd = ei_keymap_get_fd(keymapHandle);
        const auto keymapSize = ei_keymap_get_size(keymapHandle);
        if (keymapFd < 0 || keymapSize == 0) {
            return false;
        }
        const auto *mapped = mmap(nullptr, keymapSize, PROT_READ, MAP_PRIVATE, keymapFd, 0);
        if (mapped == MAP_FAILED) {
            return false;
        }
        const std::string keymapText(static_cast<const char *>(mapped), keymapSize);
        munmap(const_cast<void *>(mapped), keymapSize);

        auto *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!context) {
            return false;
        }
        auto *keymap = xkb_keymap_new_from_string(context, keymapText.c_str(),
                                                  XKB_KEYMAP_FORMAT_TEXT_V1,
                                                  XKB_KEYMAP_COMPILE_NO_FLAGS);
        xkb_context_unref(context);
        if (!keymap) {
            return false;
        }
        auto *state = xkb_state_new(keymap);
        if (!state) {
            xkb_keymap_unref(keymap);
            return false;
        }

        const auto shiftIndex = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
        const auto levelThreeIndex = xkb_keymap_mod_get_index(keymap, "Mod5");
        const auto modifierMask = [](xkb_mod_index_t index) -> xkb_mod_mask_t {
            if (index == XKB_MOD_INVALID || index >= sizeof(xkb_mod_mask_t) * 8) {
                return 0;
            }
            return static_cast<xkb_mod_mask_t>(1) << index;
        };
        const auto shiftMask = modifierMask(shiftIndex);
        const auto levelThreeMask = modifierMask(levelThreeIndex);
        const auto minCode = xkb_keymap_min_keycode(keymap);
        const auto maxCode = xkb_keymap_max_keycode(keymap);
        const std::array<xkb_mod_mask_t, 4> masks = {
            0, shiftMask, levelThreeMask, static_cast<xkb_mod_mask_t>(shiftMask | levelThreeMask)};
        bool success = true;
        for (const auto codepoint : text.toUcs4()) {
            const auto target = keysymForCodepoint(codepoint);
            if (target == XKB_KEY_NoSymbol) {
                success = false;
                break;
            }
            bool found = false;
            // Do not synthesize a group change: the EI keyboard device uses
            // the compositor's active layout, which is not exposed as client
            // state. Map against group zero and let unsupported symbols fall
            // back to the clipboard path rather than typing the wrong glyph.
            for (const auto mask : masks) {
                xkb_state_update_mask(state, mask, 0, 0, 0, 0, 0);
                for (auto code = minCode; code <= maxCode; ++code) {
                    if (xkb_state_key_get_one_sym(state, code) == target && code >= 8) {
                        strokes.push_back({static_cast<uint32_t>(code - 8),
                                           (mask & shiftMask) != 0,
                                           (mask & levelThreeMask) != 0});
                        found = true;
                        break;
                    }
                }
                if (found) {
                    break;
                }
            }
            if (!found) {
                success = false;
                break;
            }
        }
        xkb_state_unref(state);
        xkb_keymap_unref(keymap);
        return success;
    }

    void sendKey(uint32_t evdevCode, bool pressed) {
        ei_device_keyboard_key(m_keyboard, evdevCode, pressed);
        ei_device_frame(m_keyboard, ei_now(m_ei));
        if (pressed) {
            m_pressedKeys.push_back(evdevCode);
        } else {
            const auto key = std::find(m_pressedKeys.rbegin(), m_pressedKeys.rend(), evdevCode);
            if (key != m_pressedKeys.rend()) {
                m_pressedKeys.erase(std::next(key).base());
            }
        }
    }

    bool sendText(const QString &text) {
#if defined(VOCALTRI_HAS_EI_TEXT) && VOCALTRI_HAS_EI_TEXT
        if (ei_device_has_capability(m_keyboard, EI_DEVICE_CAP_TEXT)) {
            std::vector<xkb_keysym_t> keysyms;
            for (const auto codepoint : text.toUcs4()) {
                const auto keysym = keysymForCodepoint(codepoint);
                if (keysym == XKB_KEY_NoSymbol) {
                    return false;
                }
                keysyms.push_back(keysym);
            }
            for (const auto keysym : keysyms) {
                ei_device_text_keysym(m_keyboard, keysym, true);
                ei_device_frame(m_keyboard, ei_now(m_ei));
                ei_device_text_keysym(m_keyboard, keysym, false);
                ei_device_frame(m_keyboard, ei_now(m_ei));
            }
            return true;
        }
#endif
        std::vector<KeyStroke> strokes;
        if (!mapToKeyStrokes(text, strokes)) {
            return false;
        }
        for (const auto &stroke : strokes) {
            if (stroke.levelThree) {
                sendKey(100, true); // Linux evdev KEY_RIGHTALT
            }
            if (stroke.shift) {
                sendKey(42, true); // Linux evdev KEY_LEFTSHIFT
            }
            sendKey(stroke.evdevCode, true);
            sendKey(stroke.evdevCode, false);
            if (stroke.shift) {
                sendKey(42, false);
            }
            if (stroke.levelThree) {
                sendKey(100, false);
            }
        }
        return true;
    }

    void flushPending() {
        while (!m_failed && m_deviceReady && m_emulating && !m_pending.isEmpty()) {
            const auto text = m_pending.takeFirst();
            const auto textBytes = text.toUtf8().size();
            m_pendingCharacters -= text.size();
            m_pendingUtf8Bytes -= textBytes;
            if (!sendText(text)) {
                m_pending.prepend(text);
                m_pendingCharacters += text.size();
                m_pendingUtf8Bytes += textBytes;
                fail(QStringLiteral("The EIS keyboard keymap could not represent the transcript"));
                return;
            }
        }
    }

    void releasePressedKeys() {
        while (m_keyboard && !m_pressedKeys.empty()) {
            sendKey(m_pressedKeys.back(), false);
        }
    }

    void stopEmulating() {
        if (m_keyboard && m_emulating) {
            releasePressedKeys();
            ei_device_stop_emulating(m_keyboard);
        }
        m_emulating = false;
    }

    void fail(const QString &reason, const QString *additionalText = nullptr) {
        if (m_failed) {
            return;
        }
        m_failed = true;
        qWarning() << "vocaltri: native Wayland input unavailable:" << reason;
        auto pending = m_pending;
        if (additionalText) {
            pending.append(*additionalText);
        }
        m_pending.clear();
        m_pendingCharacters = 0;
        m_pendingUtf8Bytes = 0;
        releaseSession();
        if (m_failureHandler) {
            m_failureHandler(pending);
        }
    }

    void releaseSession() {
        if (m_portalNotifier) {
            m_portalNotifier->setEnabled(false);
            m_portalNotifier->deleteLater();
            m_portalNotifier = nullptr;
        }
        if (m_eisNotifier) {
            m_eisNotifier->setEnabled(false);
            m_eisNotifier->deleteLater();
            m_eisNotifier = nullptr;
        }
        stopEmulating();
        if (m_keyboard) {
            ei_device_unref(m_keyboard);
            m_keyboard = nullptr;
        }
        m_deviceReady = false;
        if (m_ei) {
            ei_unref(m_ei);
            m_ei = nullptr;
        }
        if (m_oeffis) {
            oeffis_unref(m_oeffis);
            m_oeffis = nullptr;
        }
    }

    FailureHandler m_failureHandler;
    struct oeffis *m_oeffis = nullptr;
    struct ei *m_ei = nullptr;
    struct ei_device *m_keyboard = nullptr;
    QSocketNotifier *m_portalNotifier = nullptr;
    QSocketNotifier *m_eisNotifier = nullptr;
    QStringList m_pending;
    qsizetype m_pendingCharacters = 0;
    qsizetype m_pendingUtf8Bytes = 0;
    std::vector<uint32_t> m_pressedKeys;
    uint32_t m_sequence = 0;
    bool m_deviceReady = false;
    bool m_emulating = false;
    bool m_failed = false;
};

#else

class NativeEisSession final {
public:
    using FailureHandler = std::function<void(const QStringList &)>;
    NativeEisSession(QObject *, FailureHandler) {}
    bool insert(const QString &) { return false; }
};

#endif

namespace {

QMimeData *copyMimeData(const QMimeData *source) {
    if (!source) {
        return nullptr;
    }
    auto *copy = new QMimeData;
    for (const auto &format : source->formats()) {
        copy->setData(format, source->data(format));
    }
    return copy;
}

bool ydotoolDaemonReachable() {
    auto socket = qEnvironmentVariable("YDOTOOL_SOCKET").trimmed();
    if (socket.isEmpty()) {
        socket = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
            + QStringLiteral("/.ydotool_socket");
    }
    return QFileInfo::exists(socket) || QFileInfo::exists(QStringLiteral("/tmp/.ydotool_socket"));
}

} // namespace

TextInjector::TextInjector(QObject *parent)
    : QObject(parent)
    , m_backend(detectBackend())
    , m_fallbackBackend(detectFallbackBackend())
    , m_backendName(backendLabel(m_backend))
    , m_clipboard(QGuiApplication::clipboard()) {
#if defined(VOCALTRI_HAS_LIBEI) && VOCALTRI_HAS_LIBEI
    if (m_backend == Backend::NativeEis) {
        m_eisSession = std::make_unique<NativeEisSession>(
            this, [this](const QStringList &pendingTexts) {
                nativeSessionFailed(pendingTexts);
            });
    }
#endif
    qInfo() << "vocaltri: text insertion backend:" << m_backendName;
}

TextInjector::~TextInjector() = default;

TextInjector::Backend TextInjector::detectBackend() {
    // Allow an explicit backend for tests and for users whose compositor
    // rejects an otherwise installed tool.
    const auto forced = qEnvironmentVariable("VOCALTRI_TEXT_BACKEND").trimmed().toLower();
    if (forced == QStringLiteral("clipboard")) {
        return Backend::ClipboardOnly;
    }
    if (forced == QStringLiteral("ydotool")) {
        return Backend::ClipboardYdotool;
    }
    if (forced == QStringLiteral("wtype")) {
        return Backend::Wtype;
    }
    if (forced == QStringLiteral("xdotool")) {
        return Backend::Xdotool;
    }

#if defined(VOCALTRI_HAS_LIBEI) && VOCALTRI_HAS_LIBEI && defined(Q_OS_LINUX)
    const auto platform = QGuiApplication::platformName();
    const auto waylandPlatform = platform.startsWith(QStringLiteral("wayland"), Qt::CaseInsensitive);
    if (waylandPlatform
        && (forced.isEmpty() || forced == QStringLiteral("libei")
            || forced == QStringLiteral("eis"))) {
        return Backend::NativeEis;
    }
#endif
    return detectFallbackBackend();
}

TextInjector::Backend TextInjector::detectFallbackBackend() {
    if (!QStandardPaths::findExecutable(QStringLiteral("ydotool")).isEmpty()
        && ydotoolDaemonReachable()) {
        return Backend::ClipboardYdotool;
    }

    const auto wayland = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        || qEnvironmentVariable("XDG_SESSION_TYPE").compare(QStringLiteral("wayland"),
                                                            Qt::CaseInsensitive) == 0;
    if (wayland && !QStandardPaths::findExecutable(QStringLiteral("wtype")).isEmpty()) {
        return Backend::Wtype;
    }
    if (!wayland && !QStandardPaths::findExecutable(QStringLiteral("xdotool")).isEmpty()) {
        return Backend::Xdotool;
    }
    return Backend::ClipboardOnly;
}

QString TextInjector::backendLabel(const Backend backend) {
    switch (backend) {
    case Backend::ClipboardYdotool:
        return QStringLiteral("clipboard + ydotool");
    case Backend::NativeEis:
        return QStringLiteral("Wayland RemoteDesktop (libei)");
    case Backend::Wtype:
        return QStringLiteral("wtype");
    case Backend::Xdotool:
        return QStringLiteral("xdotool");
    case Backend::ClipboardOnly:
        return QStringLiteral("clipboard only");
    }
    return QStringLiteral("clipboard only");
}

bool TextInjector::insert(const QString &text) {
    const auto trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return true;
    }
    return insertWithBackend(m_backend, trimmed);
}

bool TextInjector::insertWithBackend(const Backend backend, const QString &text) {
    switch (backend) {
    case Backend::NativeEis:
        if (m_eisSession) {
            // The helper queues text until portal permission and a resumed
            // keyboard device are both available.
            m_eisSession->insert(text);
            return true;
        }
        return insertWithBackend(m_fallbackBackend, text);
    case Backend::ClipboardYdotool:
        return insertClipboardPaste(text);
    case Backend::Wtype:
        return insertDirect(QStringLiteral("wtype"), {QStringLiteral("--")}, text);
    case Backend::Xdotool:
        return insertDirect(QStringLiteral("xdotool"),
                            {QStringLiteral("type"), QStringLiteral("--clearmodifiers"),
                             QStringLiteral("--delay"), QStringLiteral("0")},
                            text);
    case Backend::ClipboardOnly:
        if (!setClipboardText(text)) {
            qWarning() << "vocaltri: clipboard is unavailable";
            return false;
        }
        notifyClipboardOnly();
        return true;
    }
    return false;
}

void TextInjector::nativeSessionFailed(const QStringList &pendingTexts) {
    // A denied or cancelled portal request must not be bypassed by launching
    // another key-injection tool. Preserve the transcript through the existing
    // clipboard-only path instead.
    m_backend = Backend::ClipboardOnly;
    m_backendName = backendLabel(m_backend);
    emit backendNameChanged();
    qInfo() << "vocaltri: using text insertion fallback:" << m_backendName;
    if (pendingTexts.isEmpty()) {
        return;
    }
    const auto payload = pendingTexts.join(QLatin1Char('\n'));
    if (!setClipboardText(payload)) {
        qWarning() << "vocaltri: clipboard is unavailable while preserving the transcript";
    }
    notifyClipboardOnly();
}

bool TextInjector::setClipboardText(const QString &text) {
    // A background Wayland client cannot reliably own the compositor
    // selection through QClipboard, but wl-copy can.
    if (!qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
        const auto wlCopy = QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
        if (!wlCopy.isEmpty()) {
            QProcess process;
            process.start(wlCopy, {});
            if (process.waitForStarted(1000)) {
                process.write(text.toUtf8());
                process.closeWriteChannel();
                if (process.waitForFinished(2000)
                    && process.exitStatus() == QProcess::NormalExit
                    && process.exitCode() == 0) {
                    return true;
                }
            }
        }
    }

    if (!m_clipboard) {
        return false;
    }
    m_clipboard->setText(text, QClipboard::Clipboard);
    return true;
}

bool TextInjector::insertClipboardPaste(const QString &text) {
    auto *saved = m_clipboard
        ? copyMimeData(m_clipboard->mimeData(QClipboard::Clipboard))
        : nullptr;
    const auto savedText = m_clipboard ? m_clipboard->text(QClipboard::Clipboard) : QString{};
    if (!setClipboardText(text)) {
        delete saved;
        notifyClipboardOnly();
        return false;
    }

    auto *process = new QProcess(this);
    const auto restored = std::make_shared<bool>(false);
    const auto restore = [this, saved, savedText, restored] {
        if (*restored) {
            return;
        }
        *restored = true;
        if (!savedText.isEmpty()) {
            delete saved;
            setClipboardText(savedText);
        } else if (saved && m_clipboard) {
            m_clipboard->setMimeData(saved, QClipboard::Clipboard);
        }
    };
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError) {
                // Keep the transcript on the clipboard so the user can paste
                // it manually when the input backend is unavailable.
                notifyClipboardOnly();
                process->deleteLater();
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this, process, restore](const int exitCode, QProcess::ExitStatus) {
                if (exitCode != 0) {
                    notifyClipboardOnly();
                } else {
                    restore();
                }
                process->deleteLater();
            });
    process->start(QStringLiteral("ydotool"),
                   {QStringLiteral("key"), QStringLiteral("29:1"),
                    QStringLiteral("47:1"), QStringLiteral("47:0"),
                    QStringLiteral("29:0")});
    return true;
}

bool TextInjector::insertDirect(const QString &program,
                                const QStringList &arguments,
                                const QString &text) {
    auto processArguments = arguments;
    processArguments.append(text);

    auto *process = new QProcess(this);
    const auto handled = std::make_shared<bool>(false);
    const auto fallback = [this, text, handled] {
        if (*handled) {
            return;
        }
        *handled = true;
        if (setClipboardText(text)) {
            notifyClipboardOnly();
        }
    };
    connect(process, &QProcess::errorOccurred, this,
            [process, fallback](QProcess::ProcessError) {
                fallback();
                process->deleteLater();
            });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [process, fallback](const int exitCode, QProcess::ExitStatus) {
                if (exitCode != 0) {
                    fallback();
                }
                process->deleteLater();
            });
    process->start(program, processArguments);
    return true;
}

void TextInjector::notifyClipboardOnly() {
    emit notificationRequested(QStringLiteral("vocaltri"),
                               QStringLiteral("Transcript copied to the clipboard"));
    if (!QStandardPaths::findExecutable(QStringLiteral("notify-send")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("notify-send"),
                                {QStringLiteral("vocaltri"),
                                 QStringLiteral("Transcript copied to the clipboard")});
    }
}

} // namespace vocaltri::input
