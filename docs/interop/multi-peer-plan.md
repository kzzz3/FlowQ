# FlowQ 多 Peer 互操作计划

## 候选 Peer 实现

| Peer | 语言 | Install | 状态 |
|------|------|---------|------|
| **aioquic** | Python | `pip install aioquic` | ✅ 已验证 |
| **ngtcp2** | C | vcpkg | ✅ 已验证 |
| **quiche** | Rust | `cargo install` | ⏳ 待验证 |

## 已验证结果

### aioquic 1.3.0
- bidirectional_stream: PASS
- loss_recovery: PASS

### ngtcp2 1.20.0
- initial_packet: PASS

## 待完成

### quiche (Cloudflare)

quiche 是 Cloudflare 的 Rust QUIC 实现。

**步骤**:
1. 安装 Rust 工具链 (`rustup`)
2. 编译 quiche client/server 示例
3. 运行握手、流、丢包恢复场景

## 运行命令

```powershell
# 构建 interop 支持
cmake --preset windows-msvc-vcpkg-interop -DVCPKG_MANIFEST_FEATURES="interop"
cmake --build --preset windows-msvc-vcpkg-interop --config Debug

# aioquic
conda run -n expr python tests/interop/test_interop.py

# ngtcp2
.\build\windows-msvc-vcpkg-interop-openssl\Debug\flowq_ngtcp2_interop.exe --ca build\certs\cert.pem
```
