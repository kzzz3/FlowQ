# FlowQ Benchmark Gates

This directory defines performance, stability, loss recovery, and connection migration benchmarks for FlowQ.

## Benchmark Categories

| Category | Document | Scenarios | Status |
|----------|----------|-----------|--------|
| Performance | [performance.md](performance.md) | 10 | 9 PASS |
| Soak Stability | [soak.md](soak.md) | 3 | 1 PASS (10k connections) |
| Loss/Reordering | [loss-reordering.md](loss-reordering.md) | 12 | 4 PASS |
| Connection Migration | [migration.md](migration.md) | 15 | 8 PASS |

## Gate Requirements

Each benchmark gate must satisfy:
1. Defined test scenarios and success criteria
2. Reproducible test scripts
3. Quantified metrics and thresholds
4. Results recorded for regression detection

## Integration with Release Checklist

Benchmark gates integrate into the release checklist:
```powershell
# Validate all benchmark gates
.\scripts\validate-benchmarks.ps1

# Validate specific category
.\scripts\validate-benchmarks.ps1 -Category performance
.\scripts\validate-benchmarks.ps1 -Category soak
.\scripts\validate-benchmarks.ps1 -Category loss
.\scripts\validate-benchmarks.ps1 -Category migration
```

## Execution

Run benchmarks on the target platform and record hardware specifications with results.

## Results

Results are stored in the `results/` subdirectory:
```
results/
├── performance-YYYY-MM-DD.json
├── soak-YYYY-MM-DD.json
├── loss-reordering-YYYY-MM-DD.json
└── migration-YYYY-MM-DD.json
```

## Success Criteria Summary

| Category | Key Metric | Threshold |
|----------|------------|-----------|
| Performance | Throughput | > 100 Mbps (single stream) |
| Performance | Latency | < RTT + 0.1ms |
| Soak | Memory growth | < 5%/hour |
| Soak | Errors | 0 |
| Loss | Recovery time | < 500ms (5% loss) |
| Loss | Spurious retransmissions | < 1% |
| Migration | Data loss | 0 bytes |
| Migration | Switch time | < 100ms |
