# Loss and Reordering Benchmark Gate

## Overview

验证 FlowQ 在丢包和包乱序场景下的恢复能力和性能。

## Test Scenarios

### 1. Uniform Loss (均匀丢包)

**目标**: 测量不同丢包率下的吞吐量退化。

| 场景 | 丢包率 | 目标吞吐量 |
|------|--------|-----------|
| No loss | 0% | baseline |
| Low loss | 1% | > 90% baseline |
| Medium loss | 5% | > 70% baseline |
| High loss | 10% | > 50% baseline |

**指标**:
- `throughput_mbps`: 吞吐量
- `recovery_time_ms`: 恢复时间
- `retransmission_count`: 重传次数
- `pto_fires`: PTO 触发次数

### 2. Burst Loss (突发丢包)

**目标**: 测量连续丢包后的恢复能力。

| 场景 | 突发大小 | 恢复目标 |
|------|----------|----------|
| Small burst | 3 packets | < 100ms |
| Medium burst | 10 packets | < 500ms |
| Large burst | 50 packets | < 2s |

**指标**:
- `recovery_ms`: 恢复到满吞吐量的时间
- `data_lost_bytes`: 丢失的数据量
- `data_recovered_bytes`: 恢复的数据量

### 3. Reordering (包乱序)

**目标**: 测量包乱序场景下的正确性和性能。

| 场景 | 乱序程度 | 目标 |
|------|----------|------|
| Minor reorder | 1-2 packets | 无性能退化 |
| Moderate reorder | 3-5 packets | < 5% 退化 |
| Severe reorder | 10+ packets | < 20% 退化 |

**指标**:
- `throughput_mbps`: 吞吐量
- `spurious_retransmissions`: 虚假重传数
- `reorder_delay_ms`: 乱序处理延迟

### 4. Combined Loss + Reordering (丢包 + 乱序组合)

**目标**: 模拟真实网络环境。

| 场景 | 丢包率 | 乱序率 | 目标 |
|------|--------|--------|------|
| Realistic 1 | 2% | 5% | > 85% baseline |
| Realistic 2 | 5% | 10% | > 70% baseline |
| Adverse | 10% | 20% | > 50% baseline |

## Success Criteria

- 所有场景达到目标吞吐量
- 无数据损坏 (校验和验证)
- 无连接意外断开
- 恢复时间在目标范围内
- PTO 和丢包检测正确触发

## Execution

```powershell
# 构建 loss/reorder 测试
cmake --build --preset windows-msvc-vcpkg --config Release --target flowq_loss_tests

# 运行均匀丢包测试
ctest --preset windows-msvc-vcpkg -R "loss.*uniform" --timeout 300

# 运行突发丢包测试
ctest --preset windows-msvc-vcpkg -R "loss.*burst" --timeout 300

# 运行乱序测试
ctest --preset windows-msvc-vcpkg -R "reorder" --timeout 300

# 运行组合测试
ctest --preset windows-msvc-vcpkg -R "loss.*reorder" --timeout 600
```

## Network Simulation

测试使用内存模拟网络条件：
- `simulate_loss(rate)`: 按概率丢包
- `simulate_burst_loss(count)`: 连续丢 N 个包
- `simulate_reorder(delay_ms, rate)`: 按概率延迟包
- `simulate_jitter(min_ms, max_ms)`: 随机延迟

## Results Format

```json
{
  "date": "2026-05-31",
  "scenario": "uniform_loss_5pct",
  "baseline_throughput_mbps": 150.0,
  "loss_rate": 0.05,
  "measured_throughput_mbps": 112.5,
  "throughput_ratio": 0.75,
  "retransmissions": 1250,
  "pto_fires": 3,
  "recovery_time_ms": 450,
  "data_integrity": true,
  "passed": true
}
```
