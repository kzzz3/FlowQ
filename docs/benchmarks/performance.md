# Performance Benchmark Gate

## Overview

测量 FlowQ QUIC 传输库的吞吐量、延迟和资源使用。

## Test Scenarios

### 1. Throughput (吞吐量)

**目标**: 测量单连接和多连接场景下的数据传输速率。

| 场景 | 连接数 | 数据量 | 目标 |
|------|--------|--------|------|
| Single stream, small | 1 | 1 MB | > 100 Mbps |
| Single stream, large | 1 | 100 MB | > 500 Mbps |
| Multi-stream | 10 | 10 MB each | > 400 Mbps aggregate |
| Multi-connection | 100 | 1 MB each | > 300 Mbps aggregate |

**指标**:
- `throughput_mbps`: 数据传输速率 (Mbps)
- `cpu_usage_percent`: CPU 使用率
- `memory_mb`: 内存使用 (MB)

### 2. Latency (延迟)

**目标**: 测量包处理和 RTT 延迟。

| 场景 | RTT 模拟 | 包大小 | 目标 |
|------|----------|--------|------|
| RTT baseline | 0ms | 1200 bytes | < 0.1ms |
| RTT low | 10ms | 1200 bytes | < 10.1ms |
| RTT medium | 50ms | 1200 bytes | < 50.1ms |
| RTT high | 100ms | 1200 bytes | < 100.1ms |

**指标**:
- `rtt_ms`: 往返时间 (ms)
- `processing_us`: 包处理时间 (μs)

### 3. Connection Setup (连接建立)

**目标**: 测量握手建立时间。

| 场景 | 目标 |
|------|------|
| Cold start | < 50ms |
| Warm start | < 10ms |
| 100 connections burst | < 5s total |

**指标**:
- `handshake_ms`: 握手完成时间 (ms)
- `connections_per_second`: 每秒连接数

## Success Criteria

- 所有场景达到目标阈值
- 无内存泄漏 (RSS 稳定)
- 无 CPU 使用率异常 (> 80% 触发告警)

## Execution

```powershell
# 构建 benchmark 二进制
cmake --build --preset windows-msvc-vcpkg --config Release --target flowq_benchmarks

# 运行性能基准
ctest --preset windows-msvc-vcpkg -R "benchmark.*performance" --timeout 300
```

## Results Format

```json
{
  "date": "2026-05-31",
  "platform": "windows-msvc-vcpkg",
  "commit": "c58a696",
  "scenarios": [
    {
      "name": "single_stream_small",
      "throughput_mbps": 150.5,
      "cpu_usage_percent": 12.3,
      "memory_mb": 45.2,
      "passed": true
    }
  ]
}
```
