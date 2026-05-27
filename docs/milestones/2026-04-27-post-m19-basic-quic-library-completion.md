# Post-M19 Basic QUIC Library Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Bring FlowQ from the current deterministic QUIC-like core to a basic complete non-production QUIC library release.

**Architecture:** Keep protocol state deterministic and value-oriented, then add a narrow public session façade, an explicit UDP/ASIO adapter layer, examples, install packaging, CI, and release documentation. Do not expand “basic complete” into production QUIC: real TLS, AEAD, header protection, congestion control, HTTP/3, and interoperability remain future tracks behind the M19 adapter boundary.

**Tech Stack:** C++20 headers, CMake/vcpkg, standalone Asio, stdexec-direction sender seams, Catch2, GitHub Actions or equivalent CI.

---

## Basic Complete Definition

FlowQ is basic complete when it is a usable C++20 QUIC-like library baseline that:

- Builds and tests from a clean checkout with documented CMake/vcpkg commands.
- Exposes a small public API for constructing a non-production QUIC-like session without forcing users to manipulate raw frame queues.
- Provides deterministic in-memory and UDP-backed smoke paths for stream data, ACK/loss recovery, flow-control signaling, and reset/close observability.
- Keeps packet protection explicit: plaintext is test-only, production-required policy rejects it, and real TLS/AEAD/header protection remains delegated to a future external adapter.
- Ships examples, install/export packaging, CI, and documentation that clearly state supported behavior and non-goals.

## Non-Goals for Basic Complete

- RFC-complete or interoperable QUIC.
- Real TLS 1.3 handshake, certificate validation, key schedule, AEAD, or header protection.
- Real short-header 1-RTT packet-number reconstruction.
- Congestion control, pacing, ECN, migration, address validation, Retry integrity, or multipath.
- HTTP/3, WebTransport, 0-RTT, production deployment guidance, or security claims.

## File Structure

- Create `include/flowq/quic/session.hpp`: public synchronous/value-oriented session façade over `connection_loop`.
- Create `include/flowq/quic/events.hpp`: stable public event/action aliases for application-facing receive/send/close observations.
- Create `include/flowq/quic/udp_session.hpp`: non-production ASIO UDP adapter that bridges socket datagrams to `session`.
- Create `examples/in_memory_loopback.cpp`: minimal no-socket example for the current deterministic baseline.
- Create `examples/udp_stream_echo.cpp`: non-production UDP smoke example using test-only protection.
- Create `examples/protection_policy.cpp`: small example showing `test_allowed` vs `production_required` behavior.
- Create `tests/integration/example_build_tests.cpp`: Catch2 smoke test that invokes configured example executable paths when examples are enabled.
- Modify `CMakeLists.txt`: install/export rules and optional example targets.
- Modify `tests/CMakeLists.txt`: public API, UDP adapter, example-build, and package-consumer tests.
- Create `.github/workflows/ci.yml`: configure/build/test workflow for the supported baseline toolchain.
- Modify `README.md`, `PLAN.md`, and `docs/development.md`: basic-complete status, API usage, packaging, CI, and deferred backlog.

## Phase 5: Basic Library Surface

### M20: Basic Complete Scope Freeze and Public API Contract

**Goal:** Freeze what “basic complete QUIC library” means before adding more code.

**Files:**
- Create: `docs/superpowers/specs/2026-04-27-m20-basic-complete-library-scope-design.md`
- Create: `docs/superpowers/plans/2026-04-27-m20-basic-complete-library-scope.md`
- Modify: `README.md`
- Modify: `PLAN.md`
- Modify: `docs/development.md`

- [x] **Step 1: Write the scope spec**

  Include these acceptance bullets verbatim in the spec:

  ```markdown
  ## Acceptance

  - FlowQ is described as a non-production QUIC-like C++ library baseline, not a production QUIC stack.
  - Basic complete requires public session API, UDP adapter smoke path, examples, packaging, CI, and docs.
  - Basic complete excludes real TLS, AEAD, header protection, congestion control, HTTP/3, and interoperability claims.
  - Future production tracks are listed as backlog, not implied current work.
  ```

- [x] **Step 2: Review docs for contradictory scope language**

  Run:

  ```powershell
  git -c safe.directory=F:/Project/FlowQ grep -n "production\|TLS\|AEAD\|header protection\|interoper" -- README.md PLAN.md docs/development.md docs/superpowers/specs docs/superpowers/plans
  ```

  Expected: every production/security mention either says “deferred”, “future”, “non-production”, or “test-only”.

- [x] **Step 3: Update root docs**

  `README.md` Current Status should say M20 is complete and M21 starts the public session façade track. `PLAN.md` should mark M20 complete while keeping M21-M26 unchecked.

- [x] **Step 4: Verify docs-only change**

  Run:

  ```powershell
  git -c safe.directory=F:/Project/FlowQ diff --check
  ```

  Expected: no whitespace errors other than existing LF-to-CRLF warnings on Windows.

- [ ] **Step 5: Commit**

  ```powershell
  $env:GIT_MASTER='1'
  git -c safe.directory=F:/Project/FlowQ add README.md PLAN.md docs/development.md docs/superpowers/specs/2026-04-27-m20-basic-complete-library-scope-design.md docs/superpowers/plans/2026-04-27-m20-basic-complete-library-scope.md
  git -c safe.directory=F:/Project/FlowQ commit -m "docs: define FlowQ basic library completion scope"
  ```

### M21: Public Session Façade

**Goal:** Provide a small library-facing API over `connection_loop` so users do not construct raw frame queues for common stream actions.

**Files:**
- Create: `include/flowq/quic/events.hpp`
- Create: `include/flowq/quic/session.hpp`
- Create: `tests/unit/quic_session_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

- [x] **Step 1: Write failing public header smoke test**

  Create `tests/unit/quic_session_tests.cpp` with:

  ```cpp
  #include <flowq/quic/session.hpp>

  #include <catch2/catch_test_macros.hpp>

  TEST_CASE("QUIC session public header exposes basic client configuration") {
      flowq::quic::session_config config{};
      config.role = flowq::quic::connection_role::client;
      CHECK(config.version == 1);
      CHECK(config.protection_policy == flowq::quic::packet_protection_policy::test_allowed);
  }
  ```

- [x] **Step 2: Run RED**

  Run:

  ```powershell
  $env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg
  ```

  Expected: compile fails because `flowq/quic/session.hpp` does not exist.

- [x] **Step 3: Implement minimal config façade**

  Add `include/flowq/quic/session.hpp` with `session_config` wrapping role, version, local/remote connection IDs, peer endpoint, protector pointers, packet pipeline config, stream/connection credit, payload budget, and protection policy. Do not add sockets.

- [x] **Step 4: Add session action API tests**

  Add tests for:

  - `append_stream_data(stream_id, buffer)` delegates successfully.
  - `queue_stream_data({stream_id})` queues Application frames.
  - `flush(now)` returns outbound datagrams.
  - `on_datagram(datagram)` returns stream delivery events.

- [x] **Step 5: Implement minimal `quic::session`**

  `session` should own one `connection_loop` and expose synchronous methods that return vectors of public event/action values. Keep names direct: `append_stream_data`, `queue_stream_data`, `flush`, `acknowledge`, `on_datagram`, `next_recovery_timer`, `on_recovery_timer`.

- [x] **Step 6: Verify focused tests**

  Run:

  ```powershell
  $env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 -R "QUIC session" }
  ```

- [ ] **Step 7: Commit**

  ```powershell
  $env:GIT_MASTER='1'
  git -c safe.directory=F:/Project/FlowQ add include/flowq/quic/events.hpp include/flowq/quic/session.hpp tests/unit/quic_session_tests.cpp tests/CMakeLists.txt docs/development.md
  git -c safe.directory=F:/Project/FlowQ commit -m "feat: add QUIC session facade"
  ```

### M22: Non-Production UDP Session Adapter

**Goal:** Bridge the public session façade to ASIO UDP send/receive wrappers for a library-level smoke path.

**Files:**
- Create: `include/flowq/quic/udp_session.hpp`
- Create: `tests/integration/quic_udp_session_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

- [x] **Step 1: Write failing UDP adapter construction test**

  Test that a `udp_session_config` can bind a `flowq::quic::session_config` to an externally owned `asio::ip::udp::socket&` without taking ownership.

- [x] **Step 2: Run RED**

  Expected: compile fails because `flowq/quic/udp_session.hpp` does not exist.

- [x] **Step 3: Implement minimal adapter shell**

  Add `udp_session` that stores references to `asio::ip::udp::socket` and `quic::session`, plus a peer endpoint. Document that callers own sockets and the adapter is non-production.

- [x] **Step 4: Write failing UDP stream echo test**

  Use two UDP sockets on loopback. Send one structural Application stream payload through one `udp_session`, receive it in the other, and assert delivered bytes match.

- [x] **Step 5: Implement datagram pump methods**

  Add explicit methods: `async_receive_once(receiver)`, `send_pending()`, or equivalent minimal sender-compatible wrappers. Keep deterministic tests bounded with timeouts.

- [x] **Step 6: Verify focused UDP session tests**

  Run:

  ```powershell
  $env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 -R "UDP session" }
  ```

- [ ] **Step 7: Commit**

  ```powershell
  $env:GIT_MASTER='1'
  git -c safe.directory=F:/Project/FlowQ add include/flowq/quic/udp_session.hpp tests/integration/quic_udp_session_tests.cpp tests/CMakeLists.txt docs/development.md
  git -c safe.directory=F:/Project/FlowQ commit -m "feat: add non-production QUIC UDP session adapter"
  ```

### M23: ASIO Timer and Recovery Integration

**Goal:** Connect recovery timer values to ASIO timers without changing deterministic recovery semantics.

**Files:**
- Create: `include/flowq/quic/recovery_scheduler.hpp`
- Create: `tests/unit/quic_recovery_scheduler_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

- [x] **Step 1: Write failing scheduler test**

  Assert that a recovery scheduler reads `session.next_recovery_timer(now)`, arms an ASIO timer for the returned deadline, and calls `session.on_recovery_timer(space, deadline)` when it fires.

- [x] **Step 2: Run RED**

  Expected: compile fails because `recovery_scheduler.hpp` does not exist.

- [x] **Step 3: Implement minimal scheduler**

  The scheduler should not implement new recovery logic. It should only translate `connection_recovery_timer` values into ASIO timer operations and expose resulting session actions.

- [x] **Step 4: Verify scheduler tests**

  Run:

  ```powershell
  $env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 -R "recovery scheduler" }
  ```

- [ ] **Step 5: Commit**

  ```powershell
  $env:GIT_MASTER='1'
  git -c safe.directory=F:/Project/FlowQ add include/flowq/quic/recovery_scheduler.hpp tests/unit/quic_recovery_scheduler_tests.cpp tests/CMakeLists.txt docs/development.md
  git -c safe.directory=F:/Project/FlowQ commit -m "feat: add QUIC recovery scheduler adapter"
  ```

## Phase 6: Library Productization

### M24: Examples and Public Smoke Tests

**Goal:** Demonstrate what FlowQ can do as a QUIC-like library today.

**Files:**
- Create: `examples/in_memory_loopback.cpp`
- Create: `examples/udp_stream_echo.cpp`
- Create: `examples/protection_policy.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/integration/example_build_tests.cpp`
- Modify: `README.md`

- [x] **Step 1: Add examples CMake option**

  Add `FLOWQ_BUILD_EXAMPLES` defaulting to `ON` for the build preset. Example targets should link `FlowQ::flowq` only.

- [x] **Step 2: Write `in_memory_loopback.cpp`**

  Show two `quic::session` objects exchanging one stream payload with `plaintext_packet_protector` and a clear comment: “test-only, non-production packet protection”.

- [x] **Step 3: Write `udp_stream_echo.cpp`**

  Show two local UDP sessions exchanging one payload. Keep it bounded and suitable for local smoke testing.

- [x] **Step 4: Write `protection_policy.cpp`**

  Show `test_allowed` succeeds with plaintext and `production_required` rejects plaintext.

- [x] **Step 5: Verify examples build**

  Run:

  ```powershell
  $env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg
  ```

  Expected: `flowq_example_in_memory_loopback`, `flowq_example_udp_stream_echo`, and `flowq_example_protection_policy` targets build successfully.

- [ ] **Step 6: Commit**

  ```powershell
  $env:GIT_MASTER='1'
  git -c safe.directory=F:/Project/FlowQ add CMakeLists.txt README.md examples/in_memory_loopback.cpp examples/udp_stream_echo.cpp examples/protection_policy.cpp
  git -c safe.directory=F:/Project/FlowQ commit -m "feat: add FlowQ QUIC library examples"
  ```

### M25: CMake Install, Package, and Consumer Test

**Goal:** Make FlowQ consumable as a CMake package.

**Files:**
- Modify: `CMakeLists.txt`
- Create: `cmake/FlowQConfig.cmake.in`
- Create: `tests/package-consumer/CMakeLists.txt`
- Create: `tests/package-consumer/main.cpp`
- Modify: `docs/development.md`

- [x] **Step 1: Write failing package consumer test project**

  `tests/package-consumer/main.cpp` should include `<flowq/quic/session.hpp>`, create a `session_config`, and return `0`.

- [x] **Step 2: Add install/export rules**

  Install headers, export `FlowQTargets`, generate `FlowQConfig.cmake`, and include dependency discovery for Asio/stdexec as needed.

- [x] **Step 3: Verify install and consumer configure**

  Run:

  ```powershell
  $env:VCPKG_ROOT='D:/vcpkg'
  cmake --build --preset windows-msvc-vcpkg
  cmake --install build/windows-msvc-vcpkg --prefix build/install-flowq
  cmake -S tests/package-consumer -B build/package-consumer -DCMAKE_PREFIX_PATH=build/install-flowq
  cmake --build build/package-consumer
  ```

- [ ] **Step 4: Commit**

  ```powershell
  $env:GIT_MASTER='1'
  git -c safe.directory=F:/Project/FlowQ add CMakeLists.txt cmake/FlowQConfig.cmake.in tests/package-consumer docs/development.md
  git -c safe.directory=F:/Project/FlowQ commit -m "build: add FlowQ CMake package export"
  ```

### M26: CI and Basic Complete Release Documentation

**Goal:** Make the basic complete state reproducible and clearly documented.

**Files:**
- Create: `.github/workflows/ci.yml`
- Modify: `README.md`
- Modify: `PLAN.md`
- Modify: `docs/development.md`
- Create: `docs/basic-complete.md`

- [x] **Step 1: Add CI workflow**

  Create `.github/workflows/ci.yml` with a `windows-latest` job that checks out the repository, installs or restores vcpkg, configures FlowQ with the repository CMake preset or an explicit vcpkg toolchain fallback, builds, and runs `ctest --test-dir build/windows-msvc-vcpkg --timeout 10 --output-on-failure`.

- [x] **Step 2: Write `docs/basic-complete.md`**

  Include sections:

  ```markdown
  # FlowQ Basic Complete Baseline

  ## Supported Today
  ## Explicitly Not Supported
  ## Public Headers
  ## Examples
  ## Build and Test Gate
  ## Future Production QUIC Backlog
  ```

- [x] **Step 3: Update root status**

  `README.md` should say FlowQ is basic complete as a non-production QUIC-like library baseline after M26. `PLAN.md` should mark M20-M26 complete only when their tasks are done.

- [x] **Step 4: Full verification**

  Run:

  ```powershell
  $env:VCPKG_ROOT='D:/vcpkg'; cmake --build --preset windows-msvc-vcpkg; if ($?) { ctest --preset windows-msvc-vcpkg --timeout 10 }
  ```

- [ ] **Step 5: Commit**

  ```powershell
  $env:GIT_MASTER='1'
  git -c safe.directory=F:/Project/FlowQ add .github/workflows/ci.yml README.md PLAN.md docs/development.md docs/basic-complete.md
  git -c safe.directory=F:/Project/FlowQ commit -m "docs: declare FlowQ basic complete baseline"
  ```

## Post-Basic Backlog

These items are intentionally after basic complete and must get separate specs/plans before implementation:

- Real TLS 1.3 and QUIC packet protection adapter using a mature external crypto library.
- RFC-valid short-header 1-RTT packet format, packet-number truncation/reconstruction, key phase, and key update.
- Congestion control, pacing, ECN, persistent congestion, and ACK frequency.
- Address validation, Retry integrity, connection migration, stateless reset, and path validation.
- Interoperability test track against real QUIC endpoints.
- HTTP/3 and WebTransport layers.

## Self-Review

- Spec coverage: Covers public API, UDP/ASIO integration, timer scheduling, examples, packaging, CI, docs, and future backlog.
- Placeholder scan: No incomplete placeholders; each milestone has concrete files, commands, and acceptance checks.
- Type consistency: Uses existing `connection_loop`, `packet_protection_policy`, `plaintext_packet_protector`, ASIO UDP/timer wrappers, and Catch2/CMake conventions.
