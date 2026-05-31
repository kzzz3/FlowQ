# FlowQ 零拷贝发送路径设计

## 当前状态

当前发送路径涉及多次缓冲区拷贝：
1. 应用数据 → STREAM 帧缓冲区
2. STREAM 帧 → 包组装缓冲区
3. 包组装 → AEAD 加密缓冲区
4. 加密缓冲区 → 网络发送缓冲区

## 设计目标

减少到 1-2 次拷贝：
1. 应用数据 → 共享缓冲区（零拷贝）
2. 共享缓冲区 → 网络发送（可能需要加密时原地修改）

## 方案

### 方案 1: 引用计数缓冲区

```cpp
class shared_buffer {
    std::shared_ptr<std::vector<std::byte>> data_;
    std::size_t offset_{};
    std::size_t size_{};
    
public:
    // 创建子视图，不拷贝数据
    shared_buffer slice(std::size_t offset, std::size_t size) const {
        return shared_buffer{data_, offset_ + offset, size};
    }
};
```

### 方案 2: Scatter/Gather I/O

使用 `WSABUF` (Windows) 或 `iovec` (Linux) 实现分散收集：

```cpp
struct datagram_view {
    std::span<const std::byte> header;
    std::span<const std::byte> payload;
    std::span<const std::byte> tag;
};
```

### 方案 3: 预分配缓冲区池

```cpp
class buffer_pool {
    std::vector<std::vector<std::byte>> pool_;
    
    std::vector<std::byte>& acquire() {
        // 重用已分配的缓冲区
    }
    
    void release(std::vector<std::byte>&& buf) {
        // 归还到池中
    }
};
```

## 实现计划

1. **Phase 1**: 实现缓冲区池，减少分配开销
2. **Phase 2**: 实现引用计数缓冲区，支持零拷贝切片
3. **Phase 3**: 集成 scatter/gather I/O

## 性能预期

- 减少 50-70% 的内存分配
- 减少 30-50% 的 CPU 开销
- 提升 20-40% 的吞吐量

## 兼容性

- 保持现有 API 兼容
- 新 API 通过 `FLOWQ_ENABLE_ZERO_COPY` 宏启用
- 降级到传统路径时无性能损失
