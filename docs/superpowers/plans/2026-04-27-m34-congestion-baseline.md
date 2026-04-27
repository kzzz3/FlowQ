# M34 Congestion Baseline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bytes-in-flight accounting and deterministic NewReno-style congestion behavior.

**Architecture:** Loss detection remains per packet-number space. RTT and congestion state are path-level and independent from ASIO scheduling.

**Tech Stack:** C++20, Catch2, existing `ack_loss.hpp` and `connection.hpp` recovery paths.

---

## Files

- Create: `include/flowq/quic/congestion.hpp`
- Create: `tests/unit/quic_congestion_tests.cpp`
- Modify: `include/flowq/quic/ack_loss.hpp`
- Modify: `include/flowq/quic/connection.hpp`
- Modify: `tests/unit/quic_ack_loss_tests.cpp`
- Modify: `tests/unit/quic_connection_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/development.md`

## Tasks

- [ ] Write RED tests for bytes-in-flight increase on ack-eliciting send and decrease on ACK/loss.
- [ ] Write RED tests for slow start growth, congestion avoidance growth, and loss reduction.
- [ ] Write RED tests for persistent congestion signal handling.
- [ ] Write RED tests proving loss detection stays per packet-number space while congestion is path-level.
- [ ] Verify RED.
- [ ] Implement `congestion_controller`, `congestion_state`, and deterministic NewReno-style transitions.
- [ ] Integrate send/ACK/loss paths without adding pacing timers.
- [ ] Verify GREEN, full CTest, install, and package-consumer.

## Acceptance Gate

Deterministic congestion behavior passes tests. Pacing, ECN tuning, and production performance tuning remain separate.
