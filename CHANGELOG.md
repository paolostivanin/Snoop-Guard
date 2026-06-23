# Changelog

## 1.1.0-dev

- Track concurrent webcam and microphone users instead of one arbitrary process.
- Expose monitor health and detailed status while preserving the 1.0 D-Bus API.
- Retry PipeWire after disconnects and report monitoring failures.
- Make configuration reload transactional and validate input bounds.
- Add correct CLI failure codes and JSON escaping.
- Add installation, CPack archives, hardening, tests, CI, and operator documentation.
