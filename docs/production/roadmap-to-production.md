# FlowQ 生产级路线图

## 当前状态

- **日期**: 2026-05-31
- **测试**: 511 tests passing (Windows MSVC/vcpkg)
- **互操作**: aioquic 1.3.0 (握手、流、丢包恢复)
- **密码套件**: AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305
- **拥塞控制**: NewReno, BBR, CUBIC
- **生产就绪度**: ~80/100

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
- [x] Pacing 发送节奏控制

### 性能基准 ✅

- [x] Benchmark 框架建立 (40 个场景)
- [x] Benchmark 执行和结果记录 (9 个场景通过)
- [x] run-benchmarks.ps1 自动化脚本

## Phase 3: 生产优化 ⏳ 进行中

### 集成和优化

- [ ] 集成 Pacing/BBR/CUBIC 到 connection.hpp
- [ ] 零拷贝发送路径优化
- [ ] API 文档生成 (Doxygen)
- [ ] Soak 稳定性测试 (24小时)

### 跨平台验证 (用户执行)

- [ ] Linux GCC 构建验证
- [ ] ASan/UBSan 验证
- [ ] macOS 平台验证

### 互操作扩展 (用户执行)

- [ ] 第二个外部 peer (ngtcp2/quiche/MsQuic)
- [ ] 外部安全审计

## Benchmark Gates

详见 `docs/benchmarks/` 目录。

| 类别 | 场景数 | 状态 |
|------|--------|------|
| 性能基准 | 10 | ✅ 9 通过 |
| Soak 稳定性 | 3 | ⏳ 待执行 |
| 丢包重排 | 12 | ⏳ 待执行 |
| 连接迁移 | 15 | ⏳ 待执行 |

## 更新日志

| 日期 | 更新 |
|------|------|
| 2026-05-31 | 初始版本 |
| 2026-05-31 | secure_zero + 多密码套件 + header protection 修复 |
| 2026-05-31 | AEAD 密钥轮换 + traffic_secret 限制 |
| 2026-05-31 | Pacing + BBR + CUBIC 拥塞控制 |
| 2026-05-31 | Benchmark 框架和结果记录 |
