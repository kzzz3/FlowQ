# FlowQ 生产级路线图

当前状态和详细证据见 [readiness-gate.md](readiness-gate.md) 和 [release-checklist.md](release-checklist.md)。

## 当前状态

| 项目 | 状态 |
|------|------|
| 版本 | 1.0.0 |
| Windows 测试 | 514/516 passing |
| Linux 测试 | 510/511 passing |
| ASan/UBSan | 0 errors |
| 互操作 | aioquic 1.3.0 + ngtcp2 1.20.0 |
| 密码套件 | AES-128/256-GCM, ChaCha20-Poly1305 |
| 拥塞控制 | NewReno, BBR, CUBIC + Pacing |

## 更新日志

| 日期 | 更新 |
|------|------|
| 2026-06-01 | v1.0.0 发布：深度 cleanup，文档简化 |
| 2026-06-01 | Linux GCC + ASan/UBSan 验证通过 |
| 2026-06-01 | 修复 GCC 编译问题 (-Wchanges-meaning, missing \<cmath\>) |
| 2026-06-01 | Pacing 调优 + BBR/CUBIC 集成 + Release Notes |
| 2026-06-01 | Soak 测试 10,000 连接 + Benchmark 结果 |
| 2026-05-31 | secure_zero + 多密码套件 + header protection 修复 |
| 2026-05-31 | AEAD 密钥轮换 + traffic_secret 限制 |
| 2026-05-31 | Pacing + BBR + CUBIC 拥塞控制 |
| 2026-05-31 | Benchmark 框架和结果记录 |
| 2026-05-29 | v0.1.0 初始版本 |
