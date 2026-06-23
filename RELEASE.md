# Release checklist

## Automated

- GCC and Clang CI pass with warnings treated as errors.
- Unit/integration tests and ASan/UBSan pass.
- Staged installation contains binaries, service, D-Bus activation, example
  configuration, man pages, license, and documentation.
- `systemd-analyze verify` passes for the installed user service.
- CPack binary and source archives build and contain the expected version.
- The CMake version, changelog, generated man pages, tag, and release notes agree.

## Manual Linux hardware matrix

- Test at least two current distributions and one Wayland desktop session.
- Verify webcam start/stop, sessions near the polling interval, multiple video
  devices, concurrent allowed/denied applications, and inaccessible devices.
- Verify concurrent microphone applications, missing attribution, PipeWire
  restart/reconnect, and configured microphone filters.
- Verify operation with no notification server and after the server restarts.
- Confirm `sg-ctl` status, health exit codes, JSON parsing, watch, recent, and
  successful/failed reloads.
- Measure idle CPU, memory, wakeups, and log growth with the two-second default.

Do not publish a final tag while any monitor silently reports inactive after its
backend has failed.
