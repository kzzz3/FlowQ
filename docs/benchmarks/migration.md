# Connection Migration Benchmark Gate

## Overview

验证 FlowQ 连接迁移的正确性、性能和可靠性。

## Test Scenarios

### 1. Active Migration (主动迁移)

**目标**: 验证客户端主动迁移连接的能力。

| 场景 | 迁移次数 | 数据传输 | 目标 |
|------|----------|----------|------|
| Single migration | 1 | 10 MB | 无数据丢失 |
| Multiple migrations | 10 | 1 MB each | 无数据丢失 |
| Rapid migrations | 100 | 100 KB each | < 1% overhead |

**指标**:
- `migration_time_ms`: 迁移完成时间
- `path_validation_ms`: 路径验证时间
- `data_lost_bytes`: 丢失数据量 (应为 0)
- `throughput_during_migration_mbps`: 迁移期间吞吐量

### 2. Path Validation (路径验证)

**目标**: 验证 PATH_CHALLENGE/PATH_RESPONSE 机制。

| 场景 | 验证类型 | 目标 |
|------|----------|------|
| Standard validation | PATH_CHALLENGE/RESPONSE | < 100ms |
| Validation with loss | 1 packet lost | < 500ms |
| Validation timeout | 无响应 | 正确关闭 |

**指标**:
- `challenge_response_ms`: 挑战响应时间
- `validation_success_rate`: 验证成功率
- `timeout_handling`: 超时处理正确性

### 3. Address Change (地址变更)

**目标**: 验证 IP/端口变更场景。

| 场景 | 变更类型 | 目标 |
|------|----------|------|
| IP change | 10.0.0.1 → 10.0.0.2 | 无中断 |
| Port change | 1234 → 5678 | 无中断 |
| Both change | IP + Port | 无中断 |

**指标**:
- `handover_time_ms`: 切换时间
- `data_continuity`: 数据连续性

### 4. Anti-Amplification (防放大攻击)

**目标**: 验证迁移后的防放大限制。

| 场景 | 限制 | 目标 |
|------|------|------|
| Pre-validation | 3x received | 严格遵守 |
| Post-validation | Unlimited | 正确解除 |
| Re-migration | Reset to 3x | 正确重置 |

**指标**:
- `bytes_sent_before_validation`: 验证前发送字节
- `bytes_allowed`: 允许发送字节
- `amplification_ratio`: 放大比率 (应 < 3.0)

### 5. Server-Side Migration Handling (服务端迁移处理)

**目标**: 验证服务端处理客户端迁移的能力。

| 场景 | 处理 | 目标 |
|------|------|------|
| Client address change | 检测 + 验证 | 正确处理 |
| Spoofed address | 拒绝 | 安全处理 |
| NAT rebinding | 检测 + 验证 | 正确处理 |

**指标**:
- `detection_time_ms`: 检测时间
- `validation_trigger`: 验证触发正确性
- `security_violations`: 安全违规数 (应为 0)

## Success Criteria

- 所有迁移场景无数据丢失
- 路径验证在目标时间内完成
- 防放大限制正确执行
- 安全边界无违规
- 迁移期间吞吐量退化 < 20%

## Execution

```powershell
# 构建迁移测试
cmake --build --preset windows-msvc-vcpkg --config Release --target flowq_migration_tests

# 运行主动迁移测试
ctest --preset windows-msvc-vcpkg -R "migration.*active" --timeout 300

# 运行路径验证测试
ctest --preset windows-msvc-vcpkg -R "migration.*validation" --timeout 300

# 运行地址变更测试
ctest --preset windows-msvc-vcpkg -R "migration.*address" --timeout 300

# 运行防放大测试
ctest --preset windows-msvc-vcpkg -R "migration.*amplification" --timeout 300
```

## Network Topology

测试使用内存模拟网络拓扑：
```
Client (10.0.0.1)  ←→  Network  ←→  Server (10.0.0.100)
        ↓
   Client (10.0.0.2)  ←→  Network  ←→  Server (10.0.0.100)
```

迁移模拟：
- `simulate_address_change(new_ip, new_port)`: 模拟客户端地址变更
- `simulate_nat_rebinding()`: 模拟 NAT 重绑定
- `simulate_spoofed_source()`: 模拟源地址欺骗

## Results Format

```json
{
  "date": "2026-05-31",
  "scenario": "active_migration_single",
  "migrations": 1,
  "data_transferred_mb": 10,
  "migration_time_ms": 45,
  "path_validation_ms": 32,
  "data_lost_bytes": 0,
  "throughput_before_mbps": 150.2,
  "throughput_during_mbps": 142.8,
  "throughput_after_mbps": 149.9,
  "throughput_degradation_percent": 4.9,
  "security_violations": 0,
  "passed": true
}
```
