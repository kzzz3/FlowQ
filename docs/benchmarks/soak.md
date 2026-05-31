# Soak Stability Benchmark Gate

## Overview

长时间运行测试，验证内存稳定性、资源泄漏和长期可靠性。

## Test Scenarios

### 1. Continuous Stream (持续流)

**目标**: 验证长时间数据传输的稳定性。

| 参数 | 值 |
|------|-----|
| 持续时间 | 1 小时 (短期) / 24 小时 (长期) |
| 连接数 | 1 |
| 数据模式 | 连续 1KB-64KB 随机大小 STREAM 帧 |
| 验证间隔 | 每 60 秒 |

**指标**:
- `memory_rss_mb`: 内存使用 (MB) - 应稳定 ±5%
- `throughput_mbps`: 吞吐量 - 应稳定 ±10%
- `packet_loss_rate`: 丢包率 - 应为 0% (本地)
- `error_count`: 错误数 - 应为 0

### 2. Connection Churn (连接抖动)

**目标**: 验证频繁连接建立/断开的稳定性。

| 参数 | 值 |
|------|-----|
| 持续时间 | 30 分钟 |
| 连接模式 | 每秒建立 10 个连接，每个传输 100KB 后关闭 |
| 总连接数 | ~18,000 |

**指标**:
- `connections_total`: 总连接数
- `failures_total`: 失败连接数 (应 < 0.1%)
- `memory_growth_mb`: 内存增长 (应 < 10MB)

### 3. Idle Connection (空闲连接)

**目标**: 验证空闲连接的资源使用。

| 参数 | 值 |
|------|-----|
| 持续时间 | 1 小时 |
| 连接数 | 1000 |
| 活动 | 仅保持连接，无数据传输 |

**指标**:
- `memory_per_connection_kb`: 每连接内存 (应 < 100KB)
- `idle_timeout_fires`: 超时触发次数

## Success Criteria

- 无内存泄漏 (RSS 增长 < 5%/小时)
- 无句柄泄漏
- 无崩溃或断言失败
- 吞吐量稳定 (无性能退化)

## Execution

```powershell
# 构建 soak 测试
cmake --build --preset windows-msvc-vcpkg --config Release --target flowq_soak_tests

# 运行短期 soak (1 小时)
ctest --preset windows-msvc-vcpkg -R "soak.*short" --timeout 3600

# 运行长期 soak (24 小时) - 需手动执行
ctest --preset windows-msvc-vcpkg -R "soak.*long" --timeout 86400
```

## Monitoring

Soak 测试期间监控：
- RSS 内存 (每分钟采样)
- CPU 使用率 (每分钟采样)
- 句柄数 (每 5 分钟采样)
- 吞吐量 (每秒计算)

## Results Format

```json
{
  "date": "2026-05-31",
  "scenario": "continuous_stream_short",
  "duration_seconds": 3600,
  "samples": [
    {"timestamp": 0, "memory_mb": 45.2, "cpu_percent": 5.1, "throughput_mbps": 150.3},
    {"timestamp": 60, "memory_mb": 45.3, "cpu_percent": 4.9, "throughput_mbps": 149.8}
  ],
  "summary": {
    "memory_growth_percent": 0.2,
    "throughput_stable": true,
    "errors": 0,
    "passed": true
  }
}
```
