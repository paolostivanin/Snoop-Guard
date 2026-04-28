# Snoop Guard

A user-space Linux daemon that notifies you whenever your webcam or microphone is in use, with per-app allow/deny policy and a D-Bus API.

## Features
- Webcam monitoring across every `/dev/video*` device, with the holding process attributed via `/proc/*/fd`.
- Microphone monitoring driven by a single persistent PipeWire connection on the GLib main loop — no polling, no blind spots.
- Per-application policy via semicolon-separated `allow_list` / `deny_list` (separate lists for webcam and microphone).
- Notifications over the standard `org.freedesktop.Notifications` interface, with `critical` urgency and freedesktop icons (`camera-web`, `audio-input-microphone`).
- D-Bus API (`org.snoopguard.Service`) exposing current state, recent events, a config-reload method, and an `ActivityChanged` signal.
- `sg-ctl` CLI for status, recent events, live `watch`, config reload, and `--json` output.
- Live config reload via `SIGHUP` or `sg-ctl reload`.
- Rotating events log (`log_max_bytes`, mode `0600`).

## Requirements
- GCC or Clang
- glib-2.0, gio-2.0, gobject-2.0, gio-unix-2.0
- PipeWire development headers (libpipewire-0.3)

The notification daemon is reached over the standard `org.freedesktop.Notifications` D-Bus interface — any compliant notification server works (libnotify is not required).

## Build
```
$ mkdir -p build && cd build
$ cmake -DCMAKE_BUILD_TYPE=Release ..
$ cmake --build .
```

Check version:
```
$ ./sg-daemon --version
$ ./sg-ctl --version
```

## Running as a user service (systemd)
Snoop Guard is intended to run as a per-user systemd service. A unit file is provided: `snoop-guard.service`.

```
$ mkdir -p "$HOME/.config/systemd/user"
$ cp ./snoop-guard.service "$HOME/.config/systemd/user/"
$ systemctl --user daemon-reload
$ systemctl --user enable --now snoop-guard.service
```

The unit expects the binary at `/usr/bin/sg-daemon`. To run from a custom location, override `ExecStart` with a drop-in:
```
$ systemctl --user edit snoop-guard.service
```
```
[Service]
ExecStart=
ExecStart=/absolute/path/to/sg-daemon
```

## Configuration
Snoop Guard reads its configuration from `$XDG_CONFIG_HOME/snoop-guard.ini` (defaults to `$HOME/.config/snoop-guard.ini`). A custom path can be passed with `sg-daemon --config /path/to/file`.

A sample is provided at the repository root:
```
$ mkdir -p "$HOME/.config"
$ cp ./snoop-guard.ini "$HOME/.config/snoop-guard.ini"
```

### `[server]`
- `check_interval` — webcam polling interval in seconds (must be `>= 5`). Microphone monitoring is event-driven and unaffected.
- `notification_timeout` — notification timeout in seconds; `0` = manual dismissal.
- `log_max_bytes` — max size of the events log before rotation; one `.1` backup is kept (default 256 KiB).
- `microphone_device` — optional PipeWire node/application substring filter; if empty, any active capture node triggers a mic event.

### `[policy]`
- `allow_list` / `deny_list` — semicolon-separated process names for the webcam.
- `mic_allow_list` / `mic_deny_list` — same, for the microphone.
- Evaluation order: `deny_list` always notifies; `allow_list` suppresses notifications; otherwise notify.

### Reloading
The daemon reloads its configuration on `SIGHUP` or via `sg-ctl reload`.

## CLI (`sg-ctl`)
`sg-ctl` connects to the daemon over the user session D-Bus.

```
$ sg-ctl status            # current state (default subcommand)
$ sg-ctl recent [N]        # last N (1..1000) log lines (default 100)
$ sg-ctl watch             # stream state changes (timestamped)
$ sg-ctl reload            # ask the daemon to reload its config
$ sg-ctl --json status     # machine-readable output (status / watch)
$ sg-ctl --help
```

## D-Bus API
- Bus name: `org.snoopguard.Service`
- Object path: `/org/snoopguard/Service`
- Interface: `org.snoopguard.Service`

| Member | Kind | Signature |
|---|---|---|
| `GetStatus` | method | `() -> (b webcam_active, b mic_active, s webcam_proc, s mic_proc)` |
| `GetRecentEvents` | method | `(i max_lines) -> (as lines)` — returns the tail of the events log (bounded server-side) |
| `ReloadConfig` | method | `()` |
| `ActivityChanged` | signal | `(b webcam_active, b mic_active, s webcam_proc, s mic_proc)` |

The signal is emitted only on real state transitions.

## Logs
Events are appended to `$XDG_STATE_HOME/snoop-guard/events.log` with mode `0600`. The file is rotated when it exceeds `log_max_bytes`; one backup (`.1`) is kept.

## Limitations
- Microphone attribution depends on PipeWire node metadata and may be unavailable for some clients.
- Webcam attribution scans `/proc/*/fd` for symlinks to the device node and so cannot identify processes owned by other users.

## Security
- The provided systemd unit applies modern hardening (`NoNewPrivileges`, `ProtectSystem=strict`, `MemoryDenyWriteExecute`, `RestrictAddressFamilies=AF_UNIX`, `IPAddressDeny=any`, empty capability set, …) while preserving access to `/dev/video*` and `/dev/snd/*`.
- Process-name strings are escaped with `g_markup_escape_text` before being passed to the notification server.
- See `SECURITY.md` for the vulnerability disclosure policy and hardening details.

## License
GPL-3.0-or-later. See `LICENSE` for details.
