# FlowQ API 文档生成指南

## 前置条件

需要安装 Doxygen：
- Windows: `choco install doxygen.install` 或从 https://www.doxygen.nl/download.html 下载
- Linux: `sudo apt-get install doxygen`
- macOS: `brew install doxygen`

## 生成文档

### 使用脚本

```bash
# Linux/macOS
./scripts/generate-docs.sh

# Windows (使用 Git Bash 或 WSL)
bash scripts/generate-docs.sh
```

### 手动生成

```bash
doxygen Doxyfile
```

文档将生成在 `docs/api/html/` 目录。

## 查看文档

在浏览器中打开 `docs/api/html/index.html`。

## 文档结构

```
docs/api/
├── html/
│   ├── index.html          # 主页
│   ├── modules.html        # 模块列表
│   ├── annotated.html      # 类列表
│   ├── files.html          # 文件列表
│   └── ...
└── latex/                  # LaTeX 输出（可选）
```

## 文档覆盖率

运行文档验证脚本检查覆盖率：

```bash
# Linux/macOS
./scripts/validate-docs.sh

# Windows
powershell -File scripts/validate-docs.ps1
```

## 文档规范

- 所有公共 API 必须有 Doxygen 注释
- 使用 `@brief` 描述函数/类的用途
- 使用 `@param` 描述参数
- 使用 `@return` 描述返回值
- 使用 `@note` 添加注意事项
- 使用 `@warning` 添加警告

## 示例

```cpp
/// @brief 发送数据到指定流
/// @param stream_id 流 ID
/// @param data 要发送的数据
/// @return 发送结果，包含错误信息（如果有）
[[nodiscard]] send_result send_stream_data(
    std::uint64_t stream_id,
    std::span<const std::byte> data);
```

## CI 集成

文档生成已集成到 CI 流程：
- PR 检查会验证文档覆盖率
- 合并到 main 分支会自动更新文档站点
