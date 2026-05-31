# FlowQ Benchmark Gates

本目录定义 FlowQ 生产级所需的性能、稳定性、丢包恢复和迁移基准测试门控。

## Benchmark Categories

| 类别 | 文档 | 场景数 | 状态 |
|------|------|--------|------|
| 性能基准 | [performance.md](performance.md) | 10 | ⏳ 待执行 |
| Soak 稳定性 | [soak.md](soak.md) | 3 | ⏳ 待执行 |
| 丢包重排 | [loss-reordering.md](loss-reordering.md) | 12 | ⏳ 待执行 |
| 连接迁移 | [migration.md](migration.md) | 15 | ⏳ 待执行 |

## Gate Requirements

每个 benchmark gate 必须满足：
1. 定义明确的测试场景和成功标准
2. 可重复执行的测试脚本
3. 量化指标和阈值
4. 结果记录和回归检测

## Integration with Release Checklist

Benchmark gates 集成到发布清单：
```powershell
# 验证所有 benchmark gates
.\scripts\validate-benchmarks.ps1

# 验证特定类别
.\scripts\validate-benchmarks.ps1 -Category performance
.\scripts\validate-benchmarks.ps1 -Category soak
.\scripts\validate-benchmarks.ps1 -Category loss
.\scripts\validate-benchmarks.ps1 -Category migration
```

## Execution

所有 benchmark 应在以下环境执行：
- Windows MSVC/vcpkg (当前平台)
- Linux GCC/vcpkg (用户换平台后)
- 硬件规格记录在结果中

## Results

结果记录在 `results/` 子目录，格式：
```
results/
├── performance-YYYY-MM-DD.json
├── soak-YYYY-MM-DD.json
├── loss-reordering-YYYY-MM-DD.json
└── migration-YYYY-MM-DD.json
```

## Success Criteria Summary

| 类别 | 关键指标 | 阈值 |
|------|----------|------|
| 性能 | 吞吐量 | > 100 Mbps (单流) |
| 性能 | 延迟 | < RTT + 0.1ms |
| Soak | 内存增长 | < 5%/小时 |
| Soak | 错误数 | 0 |
| 丢包 | 恢复时间 | < 500ms (5% 丢包) |
| 丢包 | 虚假重传 | < 1% |
| 迁移 | 数据丢失 | 0 bytes |
| 迁移 | 切换时间 | < 100ms |
