# Snoop Guard

Receive a notification every time your webcam and/or your microphone are being used.

Project status: active, user-space daemon + CLI.

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

## Configuration
Snoop Guard reads its configuration from `$XDG_CONFIG_HOME/snoop-guard.ini` (defaults to `$HOME/.config/snoop-guard.ini`). A custom path can be passed with `--config /path/to/file`.

A sample configuration is provided at the repository root as `snoop-guard.ini`. Copy it to your config directory and adjust:
```
$ mkdir -p "$HOME/.config"
$ cp ./snoop-guard.ini "$HOME/.config/snoop-guard.ini"
```

Relevant options (see `snoop-guard.ini` for the full set):
- `[server]`
  - `check_interval`: webcam polling interval in seconds (>= 5). Microphone monitoring is event-driven and unaffected.
  - `notification_timeout`: seconds; 0 = manual dismissal.
  - `log_max_bytes`: max size of the events log before rotation (one `.1` backup is kept).
  - `microphone_device`: optional PipeWire node/application substring filter; if empty, any active capture node triggers a mic event.
- `[policy]`
  - `allow_list` / `deny_list`: semicolon-separated process names for the webcam.
  - `mic_allow_list` / `mic_deny_list`: same, for the microphone.
  - Order of evaluation: `deny_list` always notifies; `allow_list` suppresses notifications; default is to notify.

The daemon reloads its configuration on `SIGHUP` or via `sg-ctl reload`.

## Logs
Events are appended to `$XDG_STATE_HOME/snoop-guard/events.log` with mode 0600. The file is rotated when it exceeds `log_max_bytes`; one backup (`.1`) is kept.

## Running as a user service (systemd)
This project is intended to run as a per-user systemd service. A unit file is provided: `snoop-guard.service`.

Install and enable:
```
$ mkdir -p "$HOME/.config/systemd/user"
$ cp ./snoop-guard.service "$HOME/.config/systemd/user/"
$ systemctl --user daemon-reload
$ systemctl --user enable --now snoop-guard.service
```

By default the unit expects the binary at `/usr/bin/sg-daemon`. To run from a custom location, override `ExecStart` with a user drop-in:
```
$ systemctl --user edit snoop-guard.service
```
and add:
```
   [Service]
   ExecStart=
   ExecStart=/absolute/path/to/sg-daemon
```

## CLI usage
The `sg-ctl` tool connects over the user session D-Bus to query status, recent events, and trigger config reload:
```
$ sg-ctl status            # current state
$ sg-ctl recent [N]        # last N (1..1000) log lines (default 100)
$ sg-ctl watch             # stream state changes (timestamped)
$ sg-ctl reload            # ask the daemon to reload its config
$ sg-ctl --json status     # machine-readable output
$ sg-ctl --help
```

## Limitations
- Microphone attribution depends on PipeWire node metadata and may be unavailable for some clients.
- Webcam attribution scans `/proc/*/fd` for symlinks to the device node and so cannot identify processes owned by other users.

## Security
- The provided systemd unit applies modern hardening while preserving access to `/dev/video*` and `/dev/snd/*`.
- Process-name strings injected into notifications are escaped before being passed to the notification server.
- See `SECURITY.md` for the vulnerability disclosure policy and hardening details.

## License
GPL-3.0-or-later. See `LICENSE` for details.
