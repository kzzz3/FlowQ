# FlowQ 生产级路线图

## 当前状态 (v1.0.0-rc1)

- **日期**: 2026-06-01
- **版本**: 1.0.0-rc1
- **测试**: 514 tests (Windows), 510/511 tests (Linux)
- **互操作**: aioquic 1.3.0 + ngtcp2 1.20.0
- **密码套件**: AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305
- **拥塞控制**: NewReno, BBR, CUBIC + Pacing
- **Soak 测试**: 10,000 连接, 0 错误
- **生产就绪度**: ~90/100

## Phase 1: 生产候选 ✅ 已完成

### 安全加固 ✅

- [x] secure_zero() 实现 (Windows/macOS/Linux/volatile)
- [x] traffic_key_material 析构安全擦除
- [x] openssl_tls_handshake_adapter 析构擦除 6 个 secret
- [x] initial_packet_protector 析构安全擦除
- [x] buffer::secure_zero() 委托给 secure.hpp
- [x] Cipher-suite-aware header protection (AES-ECB / ChaCha20)
- [x] traffic_secret() 访问限制 (FLOWQ_ENABLE_INSPECTION)

### 密码套件支持 ✅

- [x] AES-128-GCM (16-byte key)
- [x] AES-256-GCM (32-byte key)
- [x] ChaCha20-Poly1305 (32-byte key)

### 密钥管理 ✅

- [x] AEAD 密钥轮换 (RFC 9000 §6)
- [x] key_update_state 和 key_update_manager

## Phase 2: 生产就绪 ✅ 已完成

### 拥塞控制 ✅

- [x] NewReno 基础实现
- [x] BBR 拥塞控制
- [x] CUBIC 拥塞控制 (RFC 8312)
- [x] Pacing 发送节奏控制 (RFC 9002 §7.7)
- [x] 集成到 connection_loop (congestion_control_interface)

### 性能基准 ✅

- [x] Benchmark 框架建立 (40 个场景)
- [x] Benchmark 执行和结果记录 (9 个场景通过)
- [x] run-benchmarks.ps1 自动化脚本
- [x] Soak 测试: 10,000 连接, 0 错误

## Phase 3: 生产优化 ⏳ 进行中

### 集成和优化

- [x] Pacing/BBR/CUBIC 集成到 connection.hpp
- [x] 零拷贝 packet_builder 组件
- [ ] 零拷贝完全集成到 packet_pipeline
- [x] API 文档生成 (Doxygen)
- [x] Soak 稳定性测试 (60秒, 10,000 连接)
- [ ] 丢包重排 benchmark 实现
- [ ] 连接迁移 benchmark 实现

### 跨平台验证 ✅ 已完成

- [x] Windows MSVC 构建验证 (514 tests passing)
- [x] Linux GCC 构建验证 (510/511 tests passing)
- [x] ASan/UBSan 验证 (0 errors)
- [ ] macOS 平台验证 (可选)

### 互操作扩展

- [x] aioquic 1.3.0 (握手、流、丢包恢复)
- [x] ngtcp2 1.20.0 (Initial 包生成)
- [ ] 第三个外部 peer

## Benchmark Gates

详见 `docs/benchmarks/` 目录。

| 类别 | 场景数 | 状态 |
|------|--------|------|
| 性能基准 | 10 | ✅ 9 通过 |
| Soak 稳定性 | 3 | ✅ 1 通过 (10k 连接) |
| 丢包重排 | 12 | ⏳ 待实现 |
| 连接迁移 | 15 | ⏳ 待实现 |

## 更新日志

| 日期 | 版本 | 更新 |
|------|------|------|
| 2026-06-01 | - | Linux GCC + ASan/UBSan 验证通过 |
| 2026-06-01 | - | 修复 GCC 编译问题 (-Wchanges-meaning, missing <cmath>) |
| 2026-06-01 | 1.0.0-rc1 | Pacing 调优 + BBR/CUBIC 集成 + Release Notes |
| 2026-06-01 | - | Soak 测试 10,000 连接 + Benchmark 结果 |
| 2026-05-31 | - | secure_zero + 多密码套件 + header protection 修复 |
| 2026-05-31 | - | AEAD 密钥轮换 + traffic_secret 限制 |
| 2026-05-31 | - | Pacing + BBR + CUBIC 拥塞控制 |
| 2026-05-31 | - | Benchmark 框架和结果记录 |
| 2026-05-29 | 0.1.0 | 初始版本 |
