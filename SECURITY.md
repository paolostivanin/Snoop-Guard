# Security Policy

## Supported Versions

This is a community project. We aim to keep the `main` branch secure and supported.
Bug fixes and security fixes are provided on a best-effort basis for the latest released version.

- Supported: latest release (and `main`)
- Not supported: older tags/releases

## Reporting a Vulnerability

If you discover a security issue or a privacy-impacting bug, please report it responsibly:

- Email: info@paolostivanin.com (subject: "Snoop-Guard security report")
- Please include: a clear description, affected version/commit, environment (distro, versions), and steps to reproduce or a proof-of-concept.
- Do not open a public issue for newly discovered vulnerabilities. We prefer coordinated disclosure.

We will acknowledge receipt within 7 days and aim to provide an initial assessment within 14 days.

If you prefer encrypted communication, you may use a PGP key associated with the maintainer’s email if available publicly.

## Coordinated Disclosure

- We appreciate a 90-day public disclosure timeline by default, extended as needed if a fix is in progress.
- We will credit reporters who wish to be acknowledged after a fix is released.

## Runtime Hardening

The provided systemd user unit (`snoop-guard.service`) is configured with modern security hardening while maintaining desktop compatibility:

- NoNewPrivileges=true, PrivateTmp=true
- ProtectSystem=strict, ProtectKernel{Tunables,Modules,Logs}=yes, ProtectControlGroups=yes
- ProtectClock=yes, ProtectHostname=yes, LockPersonality=yes, MemoryDenyWriteExecute=yes
- RestrictNamespaces=yes, RestrictAddressFamilies=AF_UNIX, SystemCallArchitectures=native
- IPAddressDeny=any (no network access)
- CapabilityBoundingSet= (empty), AmbientCapabilities=
- PrivateDevices=no to allow access to /dev/video* and /dev/snd/*

Note: Further sandboxing (e.g., system call filters) may be introduced, but must be tested against desktop environments and libnotify/GLib to avoid regressions.

## Build/Release Integrity

- Builds use modern compiler hardening flags (RELRO, FORTIFY in optimized builds) via CMake configuration.
- Please verify release artifacts against checksums/signatures if provided.
