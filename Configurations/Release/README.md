# 发布构建输入

本目录保存需要随源码版本固定的构建输入：

- `dependencies.json`：Agora 下载地址、JSON 头文件与许可证的 SHA-256，以及 Qt 和构建工具版本。
- `abi-v1.json`：公开 C ABI 的兼容性基线。

CMake 从依赖锁定文件获取 JSON 头文件与许可证，校验后缓存在构建目录中。原生独立接入样例位于
[`Examples/NativeConsumer`](../../Examples/NativeConsumer)。

维护者的发布编排、验证工具和 Release Notes 保存在本地忽略的 `.cicd/`，不提交到 Git。
