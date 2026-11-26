# vocaltri: Technical Brief, Stack and Agent Prompt

A Windows+H style voice typing tool for Linux. One CLI command, bound by the user to any key, shows a bottom-center overlay with live Deepgram transcription and types the result at the cursor.

Assumption: "Jino" in the request was read as GNOME. GNOME (Mutter) does not implement wlr-layer-shell or the virtual-keyboard protocol, so the design below has a GNOME path as well as a wlroots/KDE path.

## 1. What the two repos actually do

### KettShell (Quickshell/QML, Hyprland-oriented)

- STT is **batch, not streaming**. `services/Dictation.qml`: `pw-record` writes 16 kHz mono s16 raw PCM to a file, then `curl` POSTs it to Deepgram `/v1/listen` (prerecorded), then `wtype` types the transcript. Fallback is `wl-copy` plus a notification.
- Endpointing is a hand-rolled peak-level VAD (noise floor plus threshold, initial/trailing silence timers) via `PwNodePeakMonitor`.
- Overlay: `components/dictation/DictationOsd.qml`. A `PanelWindow` anchored on all edges with `exclusionMode: Ignore`, `mask: Region { item: pill }` (only the pill receives input), `WlrLayershell.layer: Overlay`, bottom margin on the pill. Rise/fade/morph animations per state.
- Trigger: `IpcHandler { target: "dictation" }` (`qs ipc call dictation toggle`) plus `GlobalShortcut` (toggle and hold-to-talk).
- Limits for vocaltri: Quickshell only, Wayland only, `wtype` needs the virtual-keyboard protocol, app context capture is Hyprland-specific.

### Vicinae (Qt 6 QML plus C++23)

- **Daemon plus thin CLI.** `vicinae server --replace` runs as a systemd user unit (`extra/vicinae.service`). `vicinae toggle` and `vicinae dictation start|stop|cancel|toggle` (`src/cli/src/cli.cpp`) send a `vicinae://...` deeplink over a local IPC socket (`cli::IpcClient::sendDeeplink`, handled in `src/server/src/ipc/ipc-command-handler.cpp`). This is the model to copy for "one command, user binds the key".
- **Overlay on screen.** `src/server/src/ui/qml/hud/HudWindowLayerShell.qml` uses `org.kde.layershell` (a fork fetched in `cmake/QtLayerShell.cmake`): `LayerTop`, scope `vicinae-hud`, `AnchorBottom`, bottom margin, `KeyboardInteractivityNone`. `HudWindow.qml` is a frameless, always-on-top, non-focusable pill. macOS and Windows variants sit beside it (`HudWindowMacOS.qml`, `HudWindowWindows.qml`).
- **Dictation already exists** (local whisper/parakeet, batch): `src/server/src/services/dictation/` (`dictation-session`, `transcription-session`), audio via `QAudioSource` in `services/audio/audio-recorder.cpp`, HUD driven by `ui/windows/hud-bridge.cpp`.
- **Text insertion** (`services/paste/linux-paste-service.cpp`): copies to clipboard, then a separate privileged helper `vicinae-input-server` (`src/snippet`, `src/lib/linux-utils/.../keyboard.cpp`) injects Ctrl+V (Ctrl+Shift+V in terminals) through **uinput**. It needs the `uinput` kernel module (`extra/modules-load.d/vicinae.conf`).
- **Global shortcuts** (`services/global-shortcuts/`): backend factory picks X11, `xx-hotkey` or `vicinae-hotkey` Wayland protocols (`src/wayland-protocols/`).
- Code rules worth copying: `AGENTS.md` (QML = presentation only, C++ bridge via `Q_PROPERTY`/`Q_INVOKABLE`, no mutable globals, `Q_OS_*` guards). Note in `src/server/src/server.cpp` (\~line 247) about Qt's native PipeWire audio backend deadlocking on Qt 6.10+.

## 2. Architecture

```
vocaltri (CLI) --local socket--> vocaltri daemon (systemd --user)
                                  |- Audio capture (16 kHz mono s16)
                                  |- Deepgram WebSocket client (streaming)
                                  |- Session state machine
                                  |- Overlay window (Qt Quick)
                                  '- Text injector (backend chosen at runtime)
```

- Commands: `vocaltri server`, `vocaltri toggle|start|stop|cancel`. The user binds `vocaltri toggle` in their DE/WM keybinding settings. That works on every compositor, which is the portable baseline. Optional: register through the GlobalShortcuts portal so vocaltri owns the key itself.
- States: `idle -> connecting -> listening -> finalizing -> done | error`.
- **Use streaming, not KettShell's batch flow.** Interim results fill the overlay box live. Only *final* segments (`is_final`) are committed to the focused app, so text is never retyped or rewritten.
- Deepgram only transcribes. If real translation is wanted, add a separate text-translation step later.

## 3. Layout spec (overlay)

Surface width = 60% of screen width (20% left and right padding). Bottom margin = 5% of screen height. Height auto-grows to the text with a max of about 4 lines. Compute from `QScreen::geometry()` in logical pixels and recompute on screen or scale change. Layer-shell: anchor bottom, left and right, margins left/right = 20% of width, bottom = 5% of height, layer Overlay or Top, keyboard interactivity None. The overlay must never take focus or the typed text lands in the wrong window.

## 4. Tech stack

| Concern | Choice | Why |
| --- | --- | --- |
| Language / build | C++23, CMake, Ninja | Portable, same as Vicinae |
| UI | **Qt 6 (6.5+) with Qt Quick/QML** | A professional animated UI in plain C++ widgets is slow to build. QML is GPU-accelerated and packaged on every distro. Keep logic in C++, QML for presentation only |
| Overlay (Wayland) | layer-shell-qt (wlr-layer-shell) | wlroots, KDE, Hyprland, Sway |
| Overlay (GNOME) | XWayland (`QT_QPA_PLATFORM=xcb`) frameless always-on-top window, or a small GNOME Shell extension | Mutter has no layer-shell |
| Overlay (X11) | Frameless, `_NET_WM_WINDOW_TYPE_NOTIFICATION`, `WindowDoesNotAcceptFocus`, empty input region |  |
| Audio | `QAudioSource` (Qt Multimedia), 16 kHz mono s16; RMS level for the waveform | Same as Vicinae. Alternative: libpipewire or miniaudio |
| STT | Deepgram streaming WebSocket (`nova-3`) via `QWebSocket` | Live interim text |
| IPC | `QLocalServer`/`QLocalSocket` | Same as Vicinae's CLI to daemon |
| Text insertion | Pluggable backends, auto-detected (below) | No single method works everywhere |
| Secrets / config | libsecret or QtKeychain; fallback `DEEPGRAM_API_KEY` env or a 0600 TOML file | Never shell out with the key in argv |
| Packaging | systemd user unit, `.desktop` file, AppImage/Flatpak later |  |

Lighter alternative: GTK4 plus gtk4-layer-shell. Smaller, but weaker animation tooling and no QML.

### Text insertion backends (probe at startup, fall back in order)

1. **libei via the RemoteDesktop portal**: the modern compositor-sanctioned route on GNOME and KDE (permission prompt once).
2. **Clipboard plus synthetic Ctrl+V** (Ctrl+Shift+V for terminals) through uinput: universal and layout-independent, handles non-ASCII text. Save and restore the previous clipboard. Needs the `uinput` module and device permission, as Vicinae does.
3. **wtype / virtual-keyboard protocol**: direct typing on wlroots compositors only.
4. **XTest / xdotool**: X11 only.
5. Last resort: copy to clipboard and notify.

Global hotkey options: DE keybinding calling the CLI (baseline), GlobalShortcuts portal, X11 XGrabKey, Vicinae-style hotkey protocols.

## 5. Deepgram streaming essentials

Endpoint `wss://api.deepgram.com/v1/listen` with header `Authorization: Token <key>`. Query params: `model=nova-3`, `encoding=linear16`, `sample_rate=16000`, `channels=1`, `interim_results=true`, `smart_format=true`, `punctuate=true`, `endpointing=300`, `utterance_end_ms=1000`, `language=<code>` (or multilingual). Send binary audio chunks of roughly 20 to 100 ms. Send a KeepAlive text message during silence and `CloseStream` when done. Read `channel.alternatives[0].transcript`, `is_final` and `speech_final`. Handle reconnect and 429/401 errors. Verify exact param names against the docs below before coding.

## 6. Professional UI guidelines (Qt Quick)

- One rounded surface (radius about 16 to 20 px), 1 px translucent border, soft shadow drawn inside the layer surface margin. Layer-shell has no built-in blur, so use a mostly opaque background (about 92 to 96 percent) and do not rely on backdrop blur.
- Follow system light/dark via `QStyleHints::colorScheme()`; define colors, spacing and radii as design tokens in one QML singleton.
- Left: pulsing mic or status dot and a live waveform (bars driven by RMS at about 30 to 60 fps). Center: transcript. Right: elapsed time and a small cancel control. Only the card accepts input; the rest of the surface has an empty input region.
- Transcript: finalized text in full contrast, interim text dimmed. Auto-scroll to the newest line with a top fade mask. No layout jumps between interim and final.
- Motion: 150 to 300 ms `OutCubic`/`OutBack` rise and fade in and out; honor a reduce-motion setting.
- Distinct, calm states: connecting, listening, finalizing, error (short message, auto-dismiss), plus optional sound cues.
- Use the system font, support HiDPI via logical pixels, and keep idle CPU near zero (window hidden, no timers).

## 7. Documentation links (agent should read these for depth)

**Deepgram**

- Live streaming: https://developers.deepgram.com/docs/live-streaming-audio
- Streaming API reference: https://developers.deepgram.com/reference/speech-to-text/listen-streaming
- Interim results: https://developers.deepgram.com/docs/interim-results
- Endpointing: https://developers.deepgram.com/docs/endpointing
- UtteranceEnd: https://developers.deepgram.com/docs/utterance-end
- KeepAlive: https://developers.deepgram.com/docs/audio-keep-alive
- Smart format: https://developers.deepgram.com/docs/smart-format
- Models and languages: https://developers.deepgram.com/docs/models-languages-overview

**Overlay / Wayland / X11**

- wlr-layer-shell protocol: https://wayland.app/protocols/wlr-layer-shell-unstable-v1
- layer-shell-qt: https://invent.kde.org/plasma/layer-shell-qt (Vicinae's fork: https://github.com/vicinaehq/layer-shell-qt)
- gtk4-layer-shell (alternative): https://github.com/wmww/gtk4-layer-shell
- Qt Quick: https://doc.qt.io/qt-6/qtquick-index.html
- QWebSocket: https://doc.qt.io/qt-6/qwebsocket.html
- QAudioSource: https://doc.qt.io/qt-6/qaudiosource.html
- QLocalServer: https://doc.qt.io/qt-6/qlocalserver.html

**Input and shortcuts**

- libei: https://gitlab.freedesktop.org/libinput/libei
- RemoteDesktop portal: https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html
- GlobalShortcuts portal: https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html
- virtual-keyboard protocol: https://wayland.app/protocols/virtual-keyboard-unstable-v1
- wtype: https://github.com/atx/wtype
- ydotool (uinput): https://github.com/ReimuNotMoe/ydotool
- XTest: https://www.x.org/releases/current/doc/libXtst/xtestlib.html

**Reference repos**

- Vicinae docs: https://docs.vicinae.com
- Quickshell: https://quickshell.org/

## 8. Agent prompt (copy everything below)

```
You are building "vocaltri": a Linux voice-typing tool, equivalent to Windows+H. It must work on any Linux distribution and desktop (GNOME, KDE, Hyprland, Sway, X11).

REFERENCE CODE (already cloned locally, read it for production-grade patterns before designing):
- /home/tusharneje/test/KettShell  (Quickshell/QML; see services/Dictation.qml and components/dictation/DictationOsd.qml)
- /home/tusharneje/test/vicinae    (Qt6 QML + C++23; see AGENTS.md, src/cli/src/cli.cpp, src/server/src/ipc/ipc-command-handler.cpp, src/server/src/ui/qml/hud/, src/server/src/services/dictation/, src/server/src/services/paste/, src/server/src/services/audio/audio-recorder.cpp, src/snippet/src/server.cpp, src/lib/linux-utils/, src/server/src/services/global-shortcuts/, cmake/QtLayerShell.cmake, extra/vicinae.service)
Note: KettShell's STT is batch (pw-record -> curl -> wtype). Do NOT copy that; vocaltri must use Deepgram STREAMING over WebSocket.

PRODUCT
- One daemon (`vocaltri server`, systemd --user) and one thin CLI: `vocaltri toggle|start|stop|cancel`, sent over a local socket (mirror `vicinae toggle`). The user binds `vocaltri toggle` to any key in their DE; optionally also register via the XDG GlobalShortcuts portal.
- On start: show an overlay at the bottom center of the screen: width 60% of screen (20% padding left and right), bottom margin 5% of screen height, height grows with text (max ~4 lines). It must never take keyboard focus.
- Stream mic audio (16 kHz mono s16) to Deepgram (nova-3). Show interim text live in the overlay; commit only final segments into the focused app at the cursor.
- Stop on toggle, on a silence timeout, or on cancel. Always flush remaining text before closing.

STACK
- C++23, CMake+Ninja, Qt 6.5+ with Qt Quick/QML. Logic in C++ exposed via Q_PROPERTY/Q_INVOKABLE; QML is presentation only (follow Vicinae AGENTS.md). No mutable globals; dependency injection; Q_OS_* guards.
- Overlay: layer-shell-qt on wlroots/KDE/Hyprland; on GNOME use an XWayland always-on-top frameless window (or a minimal GNOME Shell extension if needed); on X11 use a notification-type window. Probe at runtime.
- Text insertion: pluggable backend interface, probed in order: libei (RemoteDesktop portal) -> clipboard + Ctrl+V/Ctrl+Shift+V via uinput (restore clipboard) -> wtype -> XTest -> clipboard-only fallback with notification.
- API key from libsecret/QtKeychain, env DEEPGRAM_API_KEY, or a 0600 config file. Never put it in process arguments or logs.

UI (must look professional)
- Design tokens in one QML singleton; system light/dark; rounded card, 1px border, subtle shadow, ~92-96% opaque background (no reliance on blur); waveform driven by RMS; dimmed interim vs full-contrast final text; smooth 150-300ms animations; states: connecting, listening, finalizing, error; cancel control; reduce-motion support; HiDPI-safe; zero idle CPU.

DEEPGRAM
Read these docs and verify parameter names before coding: https://developers.deepgram.com/docs/live-streaming-audio , https://developers.deepgram.com/reference/speech-to-text/listen-streaming , .../docs/interim-results , .../docs/endpointing , .../docs/utterance-end , .../docs/audio-keep-alive , .../docs/smart-format . Implement KeepAlive, CloseStream, reconnect with backoff, and 401/429 handling.

PROCESS
1. Read both repos and the linked docs first. Write a short design note listing what you reuse from each repo and what differs.
2. Build in milestones: (a) CLI+daemon IPC, (b) overlay window with the exact geometry on each platform, (c) audio capture + Deepgram streaming with live text in overlay, (d) text insertion backends, (e) polish, settings, packaging.
3. After each milestone, build with CMake and test; report which compositors/sessions you could and could not verify. Ask me before adding any dependency not listed here.
```
