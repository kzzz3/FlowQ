# FlowQ 生产级路线图

本文档记录 FlowQ 从当前状态到达生产级 QUIC 库的完整路线图。

## 当前状态

- **评分**: 61.25 / 100
- **级别**: Pre-production candidate
- **日期**: 2026-05-31
- **已完成**: 约 75-80% 的生产级路程

### 已完成的核心能力

| 模块 | 完成度 | 状态 |
|------|--------|------|
| Varint/Frame/Header 编解码 | 98% | ✅ 完整 |
| 传输参数 | 98% | ✅ 完整 |
| 包处理管线 | 95% | ✅ 完整 |
| TLS 1.3 握手 (OpenSSL) | 90% | ✅ 完整 |
| AEAD 保护 (AES-128-GCM) | 95% | ✅ 完整 |
| Header Protection | 95% | ✅ 完整 |
| 流管理 + 流控 | 95% | ✅ 完整 |
| ACK/丢包恢复 | 93% | ✅ 完整 |
| 连接 ID 管理 | 95% | ✅ 完整 |
| Stateless Reset | 95% | ✅ 完整 |
| 连接迁移 | 90% | ✅ 完整 |
| 拥塞控制 (NewReno) | 85% | ✅ 完整 |

### 互操作验证

| Peer | 版本 | 场景 | 状态 |
|------|------|------|------|
| aioquic | 1.3.0 | 握手 | ✅ PASS |
| aioquic | 1.3.0 | 双向流回显 | ✅ PASS |
| aioquic | 1.3.0 | 丢包恢复 | ✅ PASS |

---

## Phase 1: 生产候选 (Production Candidate)

**目标**: 达到可声称 "production-candidate" 的状态
**预计时间**: 2-3 周（Windows 平台任务可立即执行）

### 1.1 安全加固 (Security Hardening)

#### [P1] 实现 secure_zero() — 密钥材料安全擦除

**问题**: traffic secrets、AEAD keys、IV 存储在 `std::vector<std::byte>` 中，析构时仅释放内存不擦除。

**解决方案**:
1. 创建 `include/flowq/secure.hpp`，实现跨平台安全擦除：
   - Windows: `SecureZeroMemory()`
   - Linux/macOS: `OPENSSL_cleanse()` 或 `explicit_bzero()`
   - Fallback: volatile write barrier
2. 为 `openssl_tls_handshake_adapter` 的 secret 成员添加 `secure_zero()` 析构
3. 为 `traffic_key_material` 添加 `secure_zero()` 析构
4. 为 `buffer` 类添加可选的 `secure_zero()` 支持

**涉及文件**:
- `include/flowq/secure.hpp` (新建)
- `include/flowq/quic/openssl_tls_handshake.hpp`
- `include/flowq/quic/key_derivation.hpp`
- `include/flowq/buffer.hpp`

#### [P2] 限制密钥导出接口

**问题**: `traffic_secret()` 直接暴露原始密钥字节。

**解决方案**:
1. 将 `traffic_secret()` 改为 `protected` 或 `detail::` 命名空间
2. 仅在 `FLOWQ_ENABLE_INSPECTION` 下可用
3. 添加安全警告文档

### 1.2 跨平台验证 (Cross-Platform Validation)

> **注意**: Linux 相关任务由用户换平台后执行

#### [P3] Linux GCC 构建验证 (用户执行)

**命令**:
```bash
# 在 Linux 主机上执行
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-gcc-vcpkg
cmake --build --preset linux-gcc-vcpkg
ctest --preset linux-gcc-vcpkg --timeout 10
```

**验证点**:
- 所有 519+ 测试通过
- 无编译器警告
- Install + package-consumer 正常

#### [P4] ASan/UBSan 验证 (用户执行)

**命令**:
```bash
# 在 Linux 主机上执行
export VCPKG_ROOT=/path/to/vcpkg
cmake --preset linux-asan-ubsan
cmake --build --preset linux-asan-ubsan
ctest --preset linux-asan-ubsan --timeout 10
```

**验证点**:
- 无内存泄漏
- 无未定义行为
- 无越界访问

### 1.3 互操作扩展 (Interop Expansion)

#### [P5] 添加第二个外部 peer

**候选**:
- ngtcp2 (C, 高性能)
- quiche (Rust, Cloudflare)
- MsQuic (C, Microsoft)
- picoquic (C, 教学级)

**方案**: 优先选择 ngtcp2 或 quiche，因为：
- 活跃维护
- 广泛使用
- 有现成的 interop 测试框架

---

## Phase 2: 生产就绪 (Production Ready)

**目标**: 达到可部署生产环境的状态
**预计时间**: 1-2 月

### 2.1 密码学增强 (Cryptographic Enhancement)

#### [P6] 实现 AEAD 密钥轮换 (RFC 9000 §6)

**问题**: 长连接安全性受影响，无法响应 peer 发起的密钥更新。

**解决方案**:
1. 在 `key_lifecycle_state` 中添加 `key_update` 状态
2. 实现 Key Phase 位检测和切换
3. 实现密钥更新安装路径
4. 支持 peer-initiated 和 self-initiated 密钥更新

**涉及文件**:
- `include/flowq/quic/key_lifecycle.hpp`
- `include/flowq/quic/connection.hpp`
- `include/flowq/quic/packet_pipeline.hpp`

**RFC 参考**: RFC 9000 Section 6, RFC 9001 Section 6

#### [P7] 添加 ChaCha20-Poly1305 支持

**问题**: 移动端和无硬件 AES 加速的设备性能差。

**解决方案**:
1. 在 `openssl_aead_protector` 中添加 ChaCha20-Poly1305 支持
2. 实现密码套件协商优先级
3. 添加性能基准测试

**涉及文件**:
- `include/flowq/quic/openssl_aead_protector.hpp`
- `include/flowq/quic/initial_keys.hpp`

#### [P8] 添加 AES-256-GCM 支持

**问题**: 某些企业部署要求更高安全级别。

**解决方案**:
1. 在 `openssl_aead_protector` 中添加 AES-256-GCM 支持
2. 自动检测密钥长度并选择正确的算法

### 2.2 性能优化 (Performance Optimization)

#### [P9] 实现零拷贝发送路径

**问题**: 每个数据报处理涉及 2-3 次完整缓冲区拷贝。

**解决方案**:
1. 实现 scatter/gather I/O (WSABUF/iovec)
2. 使用 `std::span` 传递避免拷贝
3. 实现帧缓冲区池化
4. 预分配发送缓冲区

**涉及文件**:
- `include/flowq/quic/packet_pipeline.hpp`
- `include/flowq/quic/connection.hpp`
- `include/flowq/buffer.hpp`

#### [P10] 添加 Pacing 到拥塞控制器

**问题**: 无发送节奏控制，可能导致突发丢包。

**解决方案**:
1. 实现 token bucket pacer
2. 与拥塞控制器集成
3. 支持自适应 pacing rate

**涉及文件**:
- `include/flowq/quic/congestion.hpp`
- `include/flowq/quic/congestion_algorithms.hpp`

### 2.3 安全审计 (Security Audit)

#### [P11] 委托外部安全审计

**审计范围**:
1. TLS 1.3 握手实现
2. AEAD 保护器
3. Header Protection
4. 密钥管理
5. 证书验证

**审计方选择**:
- Trail of Bits
- NCC Group
- Cure53
- 其他专业安全审计公司

---

## Phase 3: 生产优化 (Production Hardened)

**目标**: 达到生产环境高性能、高可靠的状态
**预计时间**: 3-6 月

### 3.1 高级拥塞控制 (Advanced Congestion Control)

#### [P12] 实现 BBR 拥塞控制

**问题**: NewReno 不适合高带宽长延迟场景。

**解决方案**:
1. 实现 BBR v1 基础
2. 测量 bottleneck bandwidth
3. 测量 RTprop (最小 RTT)
4. 实现 pacing gain 和 cwnd gain 循环

**涉及文件**:
- `include/flowq/quic/congestion_algorithms.hpp` (新建 bbr.hpp)

#### [P13] 实现 CUBIC 拥塞控制

**问题**: 某些网络环境更适合 CUBIC。

**解决方案**:
1. 实现 CUBIC 窗口增长函数
2. 实现快速恢复
3. 与 NewReno 共存

### 3.2 网络特性增强 (Network Feature Enhancement)

#### [P14] 实现 ECN 支持

**问题**: 现代网络环境需要显式拥塞通知。

**解决方案**:
1. 实现 ECT(0) 和 ECT(1) 标记
2. 实现 ECN-CE 回调处理
3. 与拥塞控制器集成

#### [P15] 实现多路径 QUIC (可选)

**问题**: 单路径限制了连接可靠性。

**解决方案**:
1. 实现 PATH_CHALLENGE/PATH_RESPONSE 扩展
2. 实现多路径调度
3. 实现路径质量评估

### 3.3 平台扩展 (Platform Extension)

#### [P16] macOS 平台验证

**命令**:
```bash
# 在 macOS 上执行
cmake --preset macos-clang-vcpkg
cmake --build --preset macos-clang-vcpkg
ctest --preset macos-clang-vcpkg --timeout 10
```

### 3.4 性能基准测试 (Performance Benchmarking)

#### [P17] 建立性能基准

**测试场景**:
1. 吞吐量测试 (100Mbps, 1Gbps, 10Gbps)
2. 延迟测试 (RTT 1ms, 10ms, 100ms)
3. 并发连接测试 (100, 1000, 10000)
4. 丢包恢复测试 (0%, 1%, 5% 丢包率)

**工具**:
- iperf3
- netperf
- 自定义 QUIC 基准测试

#### [P18] 性能调优

**优化方向**:
1. 内存分配优化
2. 锁竞争优化
3. CPU 缓存优化
4. 网络栈优化

### 3.5 长期稳定性 (Long-Term Stability)

#### [P19] 长期稳定性测试

**测试场景**:
1. 24 小时持续连接
2. 高频连接建立/断开
3. 内存泄漏检测
4. 资源耗尽测试

---

## 里程碑时间表

| 里程碑 | 预计完成 | 关键交付物 |
|--------|----------|-----------|
| Phase 1: 生产候选 | 2026-06-21 | secure_zero() + Linux 验证 + 第二个 peer |
| Phase 2: 生产就绪 | 2026-07-31 | 密钥轮换 + ChaCha20 + 零拷贝 + 安全审计 |
| Phase 3: 生产优化 | 2026-11-30 | BBR + ECN + macOS + 性能基准 |

---

## 当前阻塞项

### 立即可执行 (Windows)

1. ✅ 实现 secure_zero()
2. ✅ 限制密钥导出接口
3. ✅ 添加 ChaCha20-Poly1305 支持
4. ✅ 实现 AEAD 密钥轮换
5. ✅ 添加 Pacing
6. ✅ 实现零拷贝优化

### 需要用户换平台执行

1. ⏳ Linux GCC 构建验证
2. ⏳ ASan/UBSan 验证
3. ⏳ 第二个外部 peer 互操作

### 外部依赖

1. ⏳ 安全审计公司选择和委托
2. ⏳ macOS 平台访问

---

## 参考文档

- [Production Readiness Gate](readiness-gate.md) - 当前就绪状态证据
- [Release Checklist](release-checklist.md) - 发布清单
- [Interop Results](../interop/results.md) - 互操作测试结果
- [Architecture](../reference/architecture.md) - 架构文档
- [Current Plan](../plan.md) - 当前生产计划

---

## 更新日志

| 日期 | 更新内容 |
|------|----------|
| 2026-05-31 | 初始版本，基于深度评估创建 |
| 2026-05-31 | 实现 secure_zero() — traffic secrets 安全擦除 |
| 2026-05-31 | 添加 ChaCha20-Poly1305 + AES-256-GCM AEAD 支持 |
| 2026-05-31 | 修复 Header Protection bug - 支持 AES-256/ChaCha20 HP keys |
| 2026-05-31 | 为 initial_packet_protector 添加析构函数密钥擦除 |
| 2026-05-31 | 统一 buffer::secure_zero() 使用 secure.hpp |
| 2026-05-31 | 506 个测试全部通过，生产就绪度提升至 ~75% |
