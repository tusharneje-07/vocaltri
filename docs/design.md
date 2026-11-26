# vocaltri design note

## First milestone

This milestone establishes the portable control plane: one `vocaltri` binary
can run as a user daemon or send `toggle`, `start`, `stop`, and `cancel` over a
per-user `QLocalServer` socket. Requests and responses are newline-delimited
JSON so the protocol is inspectable and can evolve without coupling the CLI to
the daemon implementation.

The current state machine deliberately models the future session lifecycle:
`idle -> connecting -> listening -> finalizing -> done | error`. Audio and
Deepgram work will own the asynchronous transitions in a later milestone; the
initial daemon uses immediate transitions so the CLI and IPC can be tested
without a microphone or API key.

## Reference reuse

- From **Vicinae**: the daemon plus thin CLI split, local IPC as the stable
  boundary, explicit dictation verbs, and a service-oriented state owner.
- From **KettShell**: the dictation state vocabulary, the requirement that
  overlay input be isolated from the focused application, and the idea that
  interim transcription is UI state rather than text to insert.
- Unlike KettShell, vocaltri will not record to a file or use batch HTTP.
  Deepgram streaming will receive raw 16 kHz mono PCM over WebSocket.
- Unlike Vicinae, vocaltri does not reuse its generated RPC or application
  framework. The smaller JSON protocol keeps the standalone tool easy to
  package and bind from any desktop environment.

## Dependency status

Qt 6.10.2 Core and Network are available in the development environment.
Qt Multimedia and Qt WebSockets development modules are not currently
installed. Those modules are part of the requested stack and will be detected
explicitly in the audio/transcription milestone; this project will not add an
unlisted dependency without approval. Wayland layer-shell support is also
isolated so GNOME/X11 fallback behavior remains possible.
