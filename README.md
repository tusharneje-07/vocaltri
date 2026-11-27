# vocaltri

Linux voice typing, controlled by one command and designed for GNOME, KDE,
wlroots compositors, and X11.

## Status

Milestones A through E are complete: a user daemon and thin CLI communicate
through a per-user local socket, a non-focusing Qt Quick status overlay shows
listening state and elapsed time, Qt audio/Deepgram streaming is available,
final transcripts are sent to the focused input using a runtime-selected
backend, and settings plus systemd packaging
are installed. The CLI waits for the asynchronous startup result, so a rejected
API key or an unavailable microphone is reported instead of leaving the command
silently in `connecting`.

## Build

Requirements:

- C++23 compiler
- CMake 3.24+
- Ninja
- Qt 6.5+ Core and Network development packages

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
ctest --test-dir build --output-on-failure
```

For live transcription, also install the Qt Multimedia and Qt WebSockets
development packages supplied by your distribution. Without them, the IPC
and overlay milestones still build and run; CMake prints a warning and
disables streaming.

Text insertion probes `ydotool`, `wtype`, and `xdotool` in that order after
the RemoteDesktop/libei capability boundary. On Wayland, `ydotool` is only
selected when `ydotoold` is reachable; `xdotool` is limited to X11 sessions.
If no injector is available, the transcript is copied to the clipboard and a
desktop notification is requested. Install `wl-clipboard` on Wayland so the
clipboard fallback works from a background daemon.

The API key is checked against Deepgram before a WebSocket is opened. This
also avoids a Qt WebSockets crash on the 401 handshake response produced by an
invalid key.

For local testing, `VOCALTRI_DEEPGRAM_AUTH_URL` and
`VOCALTRI_DEEPGRAM_WS_URL` can point at a mock server, and
`VOCALTRI_TEXT_BACKEND=clipboard` forces the clipboard path.

## Configuration

Copy `extra/config.toml.example` to `~/.config/vocaltri/config.toml`, replace
the API key, and set owner-only permissions:

```sh
install -Dm600 extra/config.toml.example ~/.config/vocaltri/config.toml
```

`DEEPGRAM_API_KEY` overrides the file value. Model, language, formatting, and
endpointing settings are read from the file.

## Run

Start the daemon in one terminal:

```sh
./build/vocaltri server
```

In another terminal:

```sh
./build/vocaltri toggle
./build/vocaltri start
./build/vocaltri stop
./build/vocaltri cancel
./build/vocaltri status
```

The overlay remains visible during an active session and shows only its status
and elapsed time (`MM:SS`); recognized words are not displayed there. Final
transcripts are inserted into the focused input. Listening continues through
silence until you call `stop` or `toggle` to finish, or `cancel` to discard the
current session.

`vocaltri mic-test` records three seconds from the default microphone and
prints the captured byte count and peak level. It is useful for checking the
audio path without a Deepgram key:

```sh
./build/vocaltri mic-test
```

The daemon socket is `$XDG_RUNTIME_DIR/vocaltri.sock`, with a per-user `/tmp`
fallback when the runtime directory is unavailable.

## systemd user service

Install the binary, copy `extra/vocaltri.service` into
`~/.config/systemd/user/`, then run:

```sh
systemctl --user daemon-reload
systemctl --user enable --now vocaltri.service
```

## Security

The IPC socket is created with user-only access where supported. API keys are
not part of the CLI protocol and are never placed in process arguments or
logs. The configuration file is ignored by the application unless its group
and other permission bits are clear; install it with mode `600`.

## Linux Wayland text insertion

On Linux builds, CMake enables the native Wayland input backend only when
`pkg-config` can find all three development modules: `libei-1.0`,
`liboeffis-1.0`, and `xkbcommon`. Install the corresponding development packages
for your distribution; package names vary. If `pkg-config` or any of these
modules is unavailable, the application still builds, but this backend is
compiled out. The installed application also needs the matching runtime shared
libraries when they are not bundled with the executable.

At runtime, the native backend is selected by default when vocaltri is running
with Qt's Wayland platform and was built with those libraries. It needs an
`xdg-desktop-portal` service and a compositor portal backend that supports the
RemoteDesktop portal and EIS (the libei input channel). Vocaltri requests a
keyboard-only portal session when it first has text to insert, not when the
daemon starts. The portal/compositor controls whether to show a permission
prompt and whether permission can be reused; vocaltri does not request a
persistent session or restore token. The user can deny or close the session.

If portal permission is denied/cancelled, the portal or EIS connection fails,
or the native keyboard cannot represent the text, vocaltri joins queued
transcripts with newlines and sends them to the clipboard as one payload. The
queue is limited to 128 segments, 256K characters, or 1 MiB of UTF-8 data. If
adding a transcript would exceed any limit, vocaltri uses clipboard fallback
with the queued text joined to that incoming transcript, preserving both. It
deliberately does not launch `ydotool`, `wtype`, or another injector after a
native portal failure, so that a denied portal request is not bypassed. On
Wayland, install `wl-clipboard` (`wl-copy`) for clipboard ownership from a
background daemon; vocaltri otherwise tries Qt's clipboard interface. The
clipboard-only path requests a desktop notification.

When the native backend is not compiled in, or on other platforms, automatic
selection falls back to `ydotool` when its executable and daemon socket are
available; otherwise it selects `wtype` on Wayland, `xdotool` outside Wayland,
or clipboard-only if no applicable injector is found. You can explicitly set
`VOCALTRI_TEXT_BACKEND=clipboard|ydotool|wtype|xdotool` to choose one of those
existing paths.

The CMake probe enables libei's text-keysym API when that API is available. If
the compositor's EIS keyboard does not offer text capability, vocaltri instead
uses the keyboard's XKB keymap. That fallback searches only keymap group zero
and supports the unmodified, Shift, and Mod5 (commonly AltGr) levels; it does
not synthesize layout/group changes or compose sequences. Characters not
represented by those mappings are not typed through EIS and are sent to the
clipboard fallback instead.

References: [XDG RemoteDesktop portal](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html),
[liboeffis API](https://libinput.pages.freedesktop.org/libei/api/group__liboeffis.html),
and [libei keyboard protocol](https://libinput.pages.freedesktop.org/libei/interfaces/ei_keyboard/index.html).
