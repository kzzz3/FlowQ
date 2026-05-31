# FlowQ 生产级路线图

## 当前状态

- **日期**: 2026-05-31
- **测试**: 506 tests passing (Windows MSVC/vcpkg)
- **互操作**: aioquic 1.3.0 (握手、流、丢包恢复)
- **密码套件**: AES-128-GCM, AES-256-GCM, ChaCha20-Poly1305

## Phase 1: 生产候选 (Production Candidate)

### 安全加固 ✅ 已完成

- [x] secure_zero() 实现 (Windows/macOS/Linux/volatile)
- [x] traffic_key_material 析构安全擦除
- [x] openssl_tls_handshake_adapter 析构擦除 6 个 secret
- [x] initial_packet_protector 析构安全擦除
- [x] buffer::secure_zero() 委托给 secure.hpp
- [x] Cipher-suite-aware header protection

### 密码套件支持 ✅ 已完成

- [x] AES-128-GCM (16-byte key)
- [x] AES-256-GCM (32-byte key)
- [x] ChaCha20-Poly1305 (32-byte key)
- [x] Header protection 分发: AES-ECB / ChaCha20

### 跨平台验证 ⏳ 用户执行

- [ ] Linux GCC 构建验证
- [ ] ASan/UBSan 验证

### 互操作扩展 ⏳ 待完成

- [ ] 第二个外部 peer (ngtcp2/quiche/MsQuic)

## Phase 2: 生产就绪 (Production Ready)

### 密钥管理增强

- [ ] AEAD 密钥轮换 (RFC 9000 §6)
- [ ] traffic_secret() 访问限制 (FLOWQ_ENABLE_INSPECTION)

### 性能优化

- [ ] Pacing 发送节奏控制
- [ ] 零拷贝发送路径

### 安全审计

- [ ] 外部安全审计 (Trail of Bits / NCC Group / Cure53)

## Phase 3: 生产优化 (Production Hardened)

### 高级拥塞控制

- [ ] BBR 拥塞控制
- [ ] CUBIC 拥塞控制

### 性能基准

- [ ] 吞吐量基准 (100Mbps, 1Gbps, 10Gbps)
- [ ] 延迟基准 (RTT 1ms, 10ms, 100ms)
- [ ] 并发连接基准 (100, 1000, 10000)
- [ ] 丢包恢复基准 (0%, 1%, 5% 丢包率)

### 平台扩展

- [ ] macOS 平台验证
- [ ] 长期稳定性测试 (24小时 soak test)

## Benchmark Gates

详见 `docs/benchmarks/` 目录。

## 更新日志

| 日期 | 更新 |
|------|------|
| 2026-05-31 | 初始版本 |
| 2026-05-31 | secure_zero + 多密码套件 + header protection 修复 |
