# FlowQ 多 Peer 互操作计划

## 候选 Peer 实现

| Peer | 语言 | Install | 状态 | 备注 |
|------|------|---------|------|------|
| **aioquic** | Python | `pip install aioquic` | ✅ 已验证 | 完整握手+流+丢包恢复 |
| **ngtcp2** | C | vcpkg | ✅ 已验证 | Initial 包生成 |
| **quic-go** | Go | `go build` | ⚠️ 已构建 | 二进制已构建，需要单独终端运行服务器 |
| **quiche** | Rust | `cargo build` | ❌ 阻塞 | Windows 需要 NASM 编译 BoringSSL |

## 已验证结果

### aioquic 1.3.0
- bidirectional_stream: **PASS**
- loss_recovery: **PASS**

### ngtcp2 1.20.0
- initial_packet: **PASS**

## 待完成

### quic-go (已构建)

```powershell
# 构建
git clone --depth 1 https://github.com/quic-go/quic-go.git tools/quic-go
cd tools/quic-go
go build -o ..\quic-go-client.exe .\example\client\main.go
go build -o ..\quic-go-server.exe .\example\main.go

# 运行服务器（需要单独终端）
.\quic-go-server.exe --cert build\certs\cert.pem --key build\certs\key.pem --bind :4434

# 运行客户端
.\quic-go-client.exe --cert build\certs\cert.pem https://localhost:4434/
```

### quiche (需要 NASM)

Windows 上编译 quiche 需要 NASM：
1. 下载安装 [NASM](https://www.nasm.us/)
2. 添加到 PATH
3. 然后 `cargo build --examples --release`

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
