# FlowQ 多 Peer 互操作计划

## 目标

集成多个 QUIC 实现进行互操作验证，提升 FlowQ 的互操作性证明。

## 候选 Peer 实现

| Peer | 语言 | Windows 支持 | 预编译二进制 | 优先级 |
|------|------|--------------|--------------|--------|
| **aioquic** | Python | ✅ | pip install | ✅ 已完成 |
| **MsQuic** | C/C++ | ✅ | GitHub Releases | 🔴 高 |
| **quiche** | Rust | ✅ | cargo build | 🟠 中 |
| **ngtcp2** | C | ✅ | 需编译 | 🟠 中 |
| **picoquic** | C | ✅ | 需编译 | 🟡 低 |
| **lsquic** | C | ⚠️ | 需编译 | 🟡 低 |

## 实施计划

### Phase 1: MsQuic (Microsoft)

MsQuic 是 Microsoft 的 QUIC 实现，Windows 原生支持，有预编译二进制。

**步骤**:
1. 下载 MsQuic 预编译二进制
2. 创建 MsQuic 互操作测试脚本
3. 运行握手、流、丢包恢复场景

**预期时间**: 2-4 小时

### Phase 2: quiche (Cloudflare)

quiche 是 Cloudflare 的 Rust 实现，需要编译。

**步骤**:
1. 安装 Rust 工具链
2. 编译 quiche 示例 (client/server)
3. 创建 quiche 互操作测试脚本
4. 运行握手、流、丢包恢复场景

**预期时间**: 4-6 小时

### Phase 3: ngtcp2

ngtcp2 是一个高性能 C 实现。

**步骤**:
1. 编译 ngtcp2 及其依赖 (OpenSSL/nghttp3)
2. 创建 ngtcp2 互操作测试脚本
3. 运行握手、流、丢包恢复场景

**预期时间**: 6-8 小时

## 统一测试框架

创建统一的互操作测试框架，支持：

1. **Peer 发现**: 自动检测可用的 peer 二进制
2. **场景执行**: 统一的场景定义和执行
3. **结果收集**: 标准化的结果格式
4. **报告生成**: 自动生成互操作报告

## 场景定义

所有 peer 共享相同的测试场景：

| 场景 | 描述 | 验证点 |
|------|------|--------|
| `basic_handshake` | TLS 握手完成 | HandshakeCompleted 事件 |
| `stream_echo` | 双向流回显 | 数据正确传输 |
| `loss_recovery` | 丢包恢复 | 重传和恢复 |
| `key_update` | 密钥轮换 | Key phase 切换 |
| `connection_migration` | 连接迁移 | 地址变更处理 |

## 成功标准

- 至少 3 个 peer 通过所有场景
- 所有 peer 使用相同的证书和配置
- 结果记录在 `docs/interop/results.md`

## 资源需求

- Windows 开发环境
- Rust 工具链 (quiche)
- Git (获取源码)
- CMake + MSVC (编译)

## 风险和缓解

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| 编译失败 | 阻塞 | 使用预编译二进制 |
| 证书不兼容 | 测试失败 | 统一证书生成 |
| 网络问题 | 测试超时 | 使用 localhost |
| 版本不匹配 | 互操作失败 | 记录版本信息 |
