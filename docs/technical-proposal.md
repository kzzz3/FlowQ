# FlowQ 技术方案

## 1. 项目定位

FlowQ 是一个现代 C++ QUIC 协议库，目标是在 ASIO UDP 网络层之上实现 QUIC 协议能力，并用 stdexec 的 Sender/Receiver 模型封装异步操作。

核心目标：

- 使用 ASIO 负责跨平台 UDP I/O、定时器和事件循环。
- 使用 stdexec 暴露可组合、可取消、可调度的 sender API。
- 将 QUIC 连接、流、多路复用、流控、重传和关闭语义封装为清晰的 C++ 对象。
- 优先实现一个可运行、可测试、可扩展的 QUIC 协议库，而不是简单绑定现有高层 QUIC SDK。

## 2. 设计原则

- **ASIO 做网络底座**：UDP socket、timer、event loop 全部基于 standalone ASIO。
- **stdexec 做异步抽象**：所有核心异步操作以 sender 形式暴露。
- **协议核心与 I/O 解耦**：QUIC 状态机不直接依赖具体 socket，便于测试和后续替换 I/O。
- **先做最小闭环**：先完成连接、握手、双向流、发送、接收、关闭，再扩展高级能力。
- **错误显式化**：异步失败通过 `set_error(flowq::error)` 传播，不依赖异常作为主错误通道。

## 3. 总体架构

```text
应用层
  flowq::connect / listen / accept / stream.send / stream.recv

异步层
  stdexec sender + operation_state + cancellation

协议层
  QUIC connection state machine
  stream state machine
  packet encode/decode
  loss recovery / congestion control / flow control

加密层
  TLS 1.3 handshake adapter
  packet protection

I/O 层
  ASIO udp::socket
  ASIO steady_timer
  ASIO scheduler bridge
```

## 4. 核心模块

### 4.1 `flowq::context`

库级运行上下文，持有：

- `asio::io_context` 或外部传入的 ASIO execution context。
- UDP socket factory。
- timer factory。
- 默认 scheduler。
- 全局配置、日志和统计钩子。

### 4.2 `flowq::connection`

表示一个 QUIC 连接，负责：

- 连接握手。
- 连接 ID 管理。
- 包收发调度。
- 丢包检测与重传。
- 连接级流控。
- 创建和接收 stream。
- 优雅关闭和异常关闭。

### 4.3 `flowq::stream`

表示一个 QUIC stream，负责：

- 双向流读写。
- FIN / RESET 处理。
- stream 级流控。
- 背压管理。
- 数据乱序重组。

### 4.4 `flowq::endpoint`

描述本地或远端地址：

```cpp
struct endpoint {
    std::string host;
    std::uint16_t port;
    std::string alpn;
};
```

### 4.5 `flowq::error`

统一错误类型：

```cpp
enum class error_code {
    cancelled,
    timeout,
    udp_error,
    tls_error,
    protocol_error,
    connection_closed,
    stream_reset,
    flow_control_error,
    internal_error
};
```

## 5. Sender API 草案

### 5.1 客户端连接

```cpp
auto sender = flowq::connect(ctx, endpoint);
```

完成信号：

```cpp
set_value(flowq::connection)
set_error(flowq::error)
set_stopped()
```

### 5.2 服务端监听

```cpp
auto listener = flowq::listen(ctx, listen_config);
auto sender = listener.accept();
```

完成信号：

```cpp
set_value(flowq::connection)
set_error(flowq::error)
set_stopped()
```

### 5.3 打开双向流

```cpp
auto sender = conn.open_bidi_stream();
```

完成信号：

```cpp
set_value(flowq::stream)
set_error(flowq::error)
set_stopped()
```

### 5.4 接收新流

```cpp
auto sender = conn.accept_stream();
```

### 5.5 发送与接收

```cpp
auto send_op = stream.send_all(buffer);
auto recv_op = stream.recv_some(buffer);
```

语义：

- `send_all`：直到数据被 QUIC 发送队列完整接收，或失败、取消。
- `recv_some`：收到任意数据、FIN、错误或取消时完成。
- 背压由 operation_state 持有，等待流控窗口打开后继续推进。

## 6. ASIO 与 stdexec 的桥接

每个异步操作对应一个 sender。sender 被连接后生成 operation_state，`start()` 时注册 ASIO 操作或协议事件。

典型流程：

```text
user sender
  -> connect(sender, receiver)
  -> operation_state
  -> start()
  -> asio async_receive_from / timer / async_send_to
  -> QUIC state machine
  -> set_value / set_error / set_stopped
```

设计要求：

- operation_state 负责 receiver 生命周期。
- 每个操作只完成一次。
- ASIO handler 中不直接执行重型用户逻辑。
- 支持 stdexec cancellation，与 ASIO cancellation slot 对接。
- 默认 completion 投递回 FlowQ scheduler。

## 7. QUIC 协议核心范围

MVP 只实现 QUIC 传输层必要能力：

- Initial / Handshake / 1-RTT packet 处理。
- TLS 1.3 握手适配。
- ACK 生成与处理。
- packet number space。
- loss detection。
- 基础 congestion control。
- stream frame 编解码。
- crypto frame 编解码。
- connection close。
- stream reset。
- 基础 flow control。

暂不纳入 MVP：

- HTTP/3。
- WebTransport。
- 0-RTT。
- 连接迁移。
- multipath QUIC。
- 高级拥塞控制插件。

## 8. 依赖建议

- C++20。
- standalone ASIO。
- stdexec。
- TLS 库建议使用 BoringSSL 或 OpenSSL 3.x。
- CMake。
- 测试框架使用 Catch2 或 GoogleTest。

## 9. 目录结构建议

```text
include/flowq/
  context.hpp
  endpoint.hpp
  error.hpp
  connection.hpp
  stream.hpp
  listener.hpp
  buffer.hpp
  execution.hpp

src/
  asio/
  execution/
  quic/
  tls/
  util/

examples/
  echo_client.cpp
  echo_server.cpp

tests/
  unit/
  integration/

docs/
  technical-proposal.md
```

## 10. MVP 里程碑

### M0：项目骨架

- CMake 项目。
- ASIO 与 stdexec 依赖接入。
- 基础 error、buffer、context 类型。
- 最小 sender/operation_state 示例。

### M1：UDP 与调度闭环

- ASIO UDP 收发封装为 sender。
- timer 封装为 sender。
- cancellation 打通。
- 单元测试覆盖完成、错误、取消三种路径。

### M2：QUIC 最小握手

- packet 编解码。
- TLS handshake adapter。
- client/server loopback 握手成功。

### M3：双向流

- 创建双向流。
- stream frame 收发。
- `send_all` / `recv_some`。
- echo client/server 示例。

### M4：可靠性基础

- ACK。
- 超时重传。
- 基础流控。
- connection close。
- stream reset。

## 11. 主要风险

### 11.1 自研 QUIC 协议复杂度高

QUIC 涉及 TLS、丢包恢复、拥塞控制、流控和状态机。应严格控制 MVP 范围，并通过 loopback 测试逐步推进。

### 11.2 stdexec 生态仍在演进

应将 stdexec 隔离在 `flowq::execution` 适配层内，避免公共 API 过度绑定实现细节。

### 11.3 ASIO 与 sender 取消语义不完全一致

需要统一取消语义：用户取消映射为 `set_stopped()`，I/O 错误映射为 `set_error(flowq::error)`。

### 11.4 性能与正确性冲突

第一阶段优先正确性和可测试性。零拷贝、批量收发、pacing 优化放到后续阶段。

## 12. 一句话总结

FlowQ 将 ASIO 作为跨平台 UDP 和定时器底座，将 stdexec 作为异步组合模型，在此之上实现一个现代 C++ QUIC 协议库，先完成最小可用 QUIC 传输闭环，再逐步扩展 HTTP/3、0-RTT、连接迁移和性能优化。
