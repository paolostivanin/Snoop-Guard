# Snoop Guard

Receive a notification every time your webcam and/or your microphone are being used.

Project status: active, user-space daemon + CLI.

## Requirements
- GCC or Clang
- glib-2.0, gio-2.0, gobject-2.0
- libnotify (desktop notifications)
- PipeWire development headers (libpipewire-0.3)

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
Snoop Guard reads its configuration from: `$HOME/.config/snoop-guard.ini`

A sample configuration is provided at the repository root as `snoop-guard.ini`.
Copy it to your config directory and adjust values:
```
$ mkdir -p "$HOME/.config"
$ cp ./snoop-guard.ini "$HOME/.config/snoop-guard.ini"
```

Relevant options:
- [server]
  - `check_interval`: polling interval in seconds (> 5)
  - `notification_timeout`: seconds; 0 = manual dismissal
  - `microphone_device`: optional PipeWire node/application filter; if unset/empty, any active capture node triggers mic checks
- [policy]
  - `allow_list`: semicolon-separated process names that will NOT trigger a notification when using the webcam
  - `deny_list`: semicolon-separated process names that WILL trigger a notification when using the webcam

Notes:
- The policy lists apply to webcam checks. Microphone process attribution is best-effort via PipeWire metadata.

## Running as a user service (systemd)
This project is intended to run as a per-user systemd service. A unit file is provided: `snoop-guard.service`.

Install and enable:

```
$ mkdir -p "$HOME/.config/systemd/user"
$ cp ./snoop-guard.service "$HOME/.config/systemd/user/"
$ systemctl --user daemon-reload
$ systemctl --user enable --now snoop-guard.service
```

By default the unit expects the binary at `/usr/bin/sg-daemon`. If you run it from a custom location, override ExecStart with a user drop-in:
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
The `sg-ctl` tool connects over the user session D-Bus to query status and recent events:
- `sg-ctl status`
- `sg-ctl recent [N]`
- `sg-ctl watch`
- `sg-ctl --help`

## Limitations
- Microphone attribution depends on PipeWire node metadata and may be unavailable for some clients; allow/deny lists apply to webcam usage only.

## Security
- The provided systemd unit includes modern hardening options while preserving access to `/dev/video*` and `/dev/snd/*`.
- See SECURITY.md for the vulnerability disclosure policy and hardening details.

## License
GPL-3.0-or-later. See LICENSE for details.
