# Contributing

Build changes with both GCC and Clang where practical. Before submitting:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_C_FLAGS=-Werror
cmake --build build
ctest --test-dir build --output-on-failure
```

Monitoring changes should add deterministic tests through an injectable or
fixture-backed boundary. Do not weaken health reporting to hide a backend
failure. Preserve the compatibility D-Bus methods unless a major-version change
has been agreed.

Security-sensitive reports should follow `SECURITY.md`, not public issues.
