Package: boringssl:x64-windows@2025-08-18

**Host Environment**

- Host: x64-windows
- Compiler: MSVC 19.50.35724.0
- CMake Version: 4.2.3
-    vcpkg-tool version: 2025-12-16-44bb3ce006467fc13ba37ca099f64077b8bbf84d
    vcpkg-scripts version: ac7af7424c 2026-02-12 (4 months ago)

**To Reproduce**

`vcpkg install --x-feature=interop`

**Failure logs**

```
CMake Error at ports/boringssl/portfile.cmake:2 (message):
  Can't build BoringSSL if OpenSSL is installed.  Please remove OpenSSL, and
  try to install BoringSSL again if you need it.  Build will continue since
  BoringSSL is a drop-in replacement for OpenSSL
Call Stack (most recent call first):
  scripts/ports.cmake:206 (include)



```

**Additional context**

<details><summary>vcpkg.json</summary>

```
{
  "name": "flowq",
  "version-string": "0.1.0",
  "description": "Modern C++ QUIC protocol library built on standalone Asio and sender/receiver execution.",
  "dependencies": [
    "asio",
    "catch2",
    "openssl",
    "stdexec"
  ],
  "features": {
    "openssl-crypto": {
      "description": "Enable OpenSSL-backed QUIC crypto vector verification helpers.",
      "dependencies": [
        "openssl"
      ]
    },
    "openssl-quic-tls": {
      "description": "Enable the experimental OpenSSL QUIC TLS provider surface.",
      "dependencies": [
        "openssl"
      ]
    },
    "interop": {
      "description": "Build interop tests against external QUIC implementations.",
      "dependencies": [
        "msquic",
        "ngtcp2",
        "liblsquic"
      ]
    }
  }
}

```
</details>
