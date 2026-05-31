# FlowQ 多 Peer 互操作计划

## 已验证 Peer

| Peer | 语言 | Install | 状态 |
|------|------|---------|------|
| **aioquic** | Python | `pip install aioquic` | ✅ 已验证 |
| **ngtcp2** | C | vcpkg | ✅ 已验证 |

## 验证结果

### aioquic 1.3.0
- bidirectional_stream: **PASS**
- loss_recovery: **PASS**

### ngtcp2 1.20.0
- initial_packet: **PASS**

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
