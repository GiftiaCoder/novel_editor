# novel_editor_server 拆分版

这是从原始 `main(7).cpp` 按功能模块拆分出的 C++ 工程。

## 模块划分

- `config.h/.cpp`：gflags 命令行参数定义与声明。
- `common.h`：公共 include、`fs` 与 `json` 别名。
- `utils.h/.cpp`：UUID、时间戳、目录创建、安全读写文件等通用工具。
- `auth.h/.cpp`：认证、指纹、Base64、payload XOR 加密、静态 HTML 认证保护。
- `storage.h/.cpp`：章节创建、删除、重命名、版本保存、latest 内容读取与预览。
- `docx_export.h/.cpp`：章节排序、Word XML 生成、ZIP store 打包、DOCX 导出文件名。
- `command_hooks.h/.cpp`：hook 配置加载、异步执行、执行结果消息缓存。
- `routes.h/.cpp`：所有 HTTP 路由注册。
- `main.cpp`：启动参数解析、目录初始化、Server 初始化、路由注册与 listen。

## 编译

依赖仍和原文件一致：`gflags`、`cpp-httplib`、`nlohmann/json`。

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

如果你的 `httplib.h` 或 `nlohmann/json.hpp` 不在系统 include 路径里，把对应 include 目录加入 `CMAKE_CXX_FLAGS` 或在 `CMakeLists.txt` 里添加 `target_include_directories`。
