# vocaltri

Linux voice typing, controlled by one command and designed for GNOME, KDE,
wlroots compositors, and X11.

## Status

Milestones A through D are complete: a user daemon and thin CLI communicate
through a per-user local socket, a non-focusing Qt Quick overlay renders the
session state, optional Qt audio/Deepgram streaming is available, and final
transcripts use a runtime-selected text insertion backend.

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
the RemoteDesktop/libei capability boundary. If no injector is available,
the transcript is copied to the clipboard and a desktop notification is
requested.

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
not part of the CLI protocol and will never be placed in process arguments or
logs.
