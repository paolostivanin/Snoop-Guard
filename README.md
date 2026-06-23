# Snoop Guard

Snoop Guard is a per-user Linux service that monitors webcam and microphone
capture, attributes activity to applications where possible, and sends desktop
notifications. It exposes both a D-Bus API and the `sg-ctl` command-line client.

## Monitoring model

- Webcam devices are checked immediately at startup and then every two seconds
  by default. The daemon scans same-user `/proc/*/fd` entries once per cycle,
  tracks concurrent holders, and treats an `EBUSY` probe as active even when
  attribution is unavailable.
- PipeWire microphone capture is event-driven. Concurrent capture applications
  are tracked, and the daemon reconnects with exponential backoff after
  PipeWire failures.
- Monitoring health is explicit: `ok`, `degraded`, or `unavailable`. Uncertain
  monitoring produces one critical notification and is visible through
  `sg-ctl health` and D-Bus.

Webcam monitoring is best-effort: activity shorter than the configured polling
interval can be missed. Processes owned by other users cannot normally be
attributed. PipeWire attribution depends on client metadata.

## Requirements

- Linux with V4L2, PipeWire, a user D-Bus session, and a notification server
- CMake 3.16 or newer
- GCC or Clang
- GLib/GIO development files
- PipeWire 0.3 development files

## Build, test, and install

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build
systemctl --user daemon-reload
systemctl --user enable --now snoop-guard.service
```

For a staged package installation:

```sh
DESTDIR="$PWD/stage" cmake --install build
```

Generate binary and source archives with:

```sh
cmake --build build --target package package_source
```

The binary archive is rooted at `usr/` and is intended to be extracted at `/`
by a package manager or administrator after its contents have been inspected.
CPack also writes `.sha256` sidecars for generated archives.

Uninstall using the install manifest:

```sh
sudo xargs rm -v < build/install_manifest.txt
```

## Configuration

The daemon reads `$XDG_CONFIG_HOME/snoop-guard.ini`, defaulting to
`~/.config/snoop-guard.ini`. Copy the installed example from
`/usr/share/snoop-guard/snoop-guard.ini.example`.

```ini
[server]
check_interval=2
notification_timeout=5
log_max_bytes=262144
microphone_device=

[policy]
allow_list=zoom;teams;
deny_list=
mic_allow_list=
mic_deny_list=
```

- `check_interval`: webcam polling interval, 1 through 3600 seconds.
- `notification_timeout`: 0 for manual dismissal, otherwise up to 86400 seconds.
- `log_max_bytes`: event-log rotation threshold, up to 1 GiB.
- `microphone_device`: optional case-insensitive substring matched against
  PipeWire node and application metadata.
- Policy entries are exact, case-sensitive application/process names. Deny
  overrides allow; unknown applications notify.

The Linux `comm` value used for webcam policy is normally limited to 15
characters. Use the names reported by `sg-ctl status`, `watch`, or `recent`.

Startup fails for a missing explicit `--config` file or malformed configuration.
A failed live reload keeps the last valid configuration and returns an error.
Reload with `SIGHUP` or `sg-ctl reload`.

## CLI

```sh
sg-ctl status
sg-ctl health
sg-ctl recent 100
sg-ctl watch
sg-ctl reload
sg-ctl --json status
sg-ctl --json health
```

Operational failures return nonzero. `health` returns 0 when both monitors are
healthy, 2 when either is degraded, and 3 when either is unavailable.

## D-Bus API

- Bus/interface: `org.snoopguard.Service`
- Object: `/org/snoopguard/Service`

Compatibility members:

| Member | Signature |
|---|---|
| `GetStatus` | `() -> (bbss)` |
| `GetRecentEvents` | `(i) -> (as)` |
| `ReloadConfig` | `() -> ()` |
| `ActivityChanged` | `(bbss)` |

The scalar process fields are the first entry in a stable sorted process list.

Detailed API:

| Member | Signature |
|---|---|
| `GetDetailedStatus` | `() -> (a{sv})` |
| `DetailedStatusChanged` | `(a{sv})` |

The dictionary includes `schema_version`, active flags, health strings,
`webcam_processes`, `mic_processes`, `webcam_unknown_devices`, and diagnostics.

## Logs and troubleshooting

Events are stored in `$XDG_STATE_HOME/snoop-guard/events.log`, defaulting to
`~/.local/state/snoop-guard/events.log`, with mode `0600` and one rotated backup.
Service diagnostics are also sent to the user journal:

```sh
journalctl --user -u snoop-guard.service
sg-ctl health
systemctl --user status snoop-guard.service
```

If microphone monitoring is unavailable, confirm that PipeWire is running in
the same user session. If webcam monitoring is degraded, verify permissions on
`/dev/video*` and membership in the distribution's video-access group.

## Security and release status

Optimized builds enable FORTIFY, stack protection, PIE, RELRO/NOW, and a
non-executable stack when supported. The supplied user unit applies additional
systemd sandboxing while retaining device and session-bus access.

See [SECURITY.md](SECURITY.md) for private vulnerability reporting. Development
versions are not production releases; the hardware acceptance checklist in
[RELEASE.md](RELEASE.md) must pass before tagging.

Licensed under GPL-3.0-or-later.
