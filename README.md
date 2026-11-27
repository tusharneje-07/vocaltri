# vocaltri

Linux voice typing, controlled by one command and designed for GNOME, KDE,
wlroots compositors, and X11.

## Status

Milestones A through E are complete: a user daemon and thin CLI communicate
through a per-user local socket, a non-focusing Qt Quick overlay renders the
session state, Qt audio/Deepgram streaming is available, final transcripts use
a runtime-selected text insertion backend, and settings plus systemd packaging
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
