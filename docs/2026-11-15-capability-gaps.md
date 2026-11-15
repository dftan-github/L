# L 语言能力缺口清单（2026-11-15）

面向目标：**用 L 写大项目**（万行以上、多人协作、需要长期维护）。

本文基于当日代码实况盘点：

- `stdlib/`：26 个模块
- `runtime/`：9 个 C 源文件（callback / io / memory / net / regex / sys / thread / tls / CMakeLists）
- `compiler/`：19 个 C++ 源文件
- `tests/cases/`：141 条自动化用例

> 注：`README.md` 中写的“123 条自动化用例”已过时，实际为 141 条。

---

## 一、阻塞级（不解决就写不成大项目）

### 1. 没有模块级命名空间

现状：文件即模块，`import` 后所有符号**全局展平**，可见性只有 `pub` / 非 `pub`。

后果：100 个文件的项目必然出现 `parse`、`Config`、`Result`、`Options` 之类重名，只能靠人工加前缀回避。

缺：
- `mod a.b.c` 声明与嵌套模块
- `use` / 限定名调用（`a.b.func()`）
- `internal` 等中间可见性

### 2. 包管理只有一层依赖

现状：`l.toml` 支持 `[package]` 与 `[dependencies]`，vendor 目录按名字查找 `<dep>.li` / `lib<dep>.a`。

缺：
- 版本约束（`^1.2`、`~1.2`、范围）
- 锁文件（可重现构建）
- `dev-dependencies`
- workspace / 多 crate 单仓
- 依赖替换 / patch

### 3. 没有增量编译

现状：改动任意一个 `.l` 都会重新生成整个 LLVM 模块并重新链接。

缺：
- 模块级哈希与目标文件缓存
- 链接期重复符号去重
- 并行编译

### 4. 没有调试信息

现状：不生成 DWARF / PDB，崩溃栈里只有地址。

缺：
- `-g` 开关
- 行号表与变量位置描述
- 与调试器（gdb / lldb / WinDbg）的源码级联动

### 5. 泛型实例化无管控

现状：`<T: Interface>` 全单态化，嵌套深度上限 2，无实例化去重。

后果：`Array<Map<Array<X>>>` 这类组合一多，代码体积与编译时间会失控，且没有诊断提示。

缺：
- 实例化去重与统计
- 实例化数量上限及明确诊断信息

---

## 二、库层面（项目基础设施）

### 6. `Array<T>` 只有 5 个方法

实际 API（`stdlib/array.l`）：`push`、`get`、`set`、`len`、`each`。

缺：`pop`、`insert`、`remove`、`index_of`、`slice`、`reverse`、`sort`、`contains`、`clear`、`extend`。

### 7. `Map<V>` 不是 `Map<K, V>`，且键不拷贝

实际（`stdlib/map.l`）：`class Map<V>`，键固定为 String，内部存 `key.c_str()` 的**外借指针**。

问题：
- 用临时 String 作键（如 `a + b`）一旦被 auto-free，Map 内即留下悬空指针
- 无法用 `i32`、类实例作键，也无法自定义哈希
- 没有 `remove`、`contains`，遍历需先 `keys()` 再逐个 `get`

与 Rule 20（`Map<K, V>` + `l_alloc` 连续内存）不一致，属规格与实现偏差。

### 8. JSON 只支持单层 Object

实际（`stdlib/json.l`）：`json_parse_object -> Result<Map<String>, String>`，值由 `json_parse_value_text` 取为**原始文本片段**。

缺：
- 嵌套对象 / 数组解析
- 类型化取值（现在是字符串，调用方自行转换）
- 序列化（`json_stringify_object` 只处理一层）

### 9. 日志无分级配置

实际（`stdlib/log.l`）：`log_info` / `log_warn` / `log_error` 三个函数，输出写死。

缺：级别过滤、时间戳、结构化字段、输出目标（文件 / 滚动 / 异步）。

### 10. 配置无分层与校验

实际（`stdlib/config.l`）：读 JSON 成 `Map<String>`，再 `config_get` 取值。

缺：默认值 < 文件 < 环境变量 < 命令行 的覆盖链、schema 校验、热加载。

### 11. HTTP 客户端能力弱

实际（`stdlib/http.l`）：`http_get(host, port, path) -> String` 返回原始响应文本，调用方再 `http_body`。

缺：请求头设置、POST/PUT、超时、重定向、chunked 解码、HTTPS 证书校验（`https_get` 走 `tls.c`，但无配置面）。

### 12. 没有 L 侧测试框架

现状：测试全部由外部 `tests/run_tests.rb` 驱动。

缺：L 内可用的 `assert`、测试分组、fixture、diff 输出。写多模块项目的单元测试需要另建一套。

### 13. 并发原语单一

实际：`spawn { ... }` 返回 `ptr` 句柄，需显式 `join`；`sync { ... }` 映射到全局 mutex。

缺：channel、线程池、读写锁、`Future`/`await`。此外 `spawn` 的捕获通过把**栈地址**塞进 ctx 传递，生命周期由人工保证，易出错。

---

## 三、工程与运维

### 14. 没有可观测性

无 metrics、无 trace、无结构化日志出口。

### 15. 没有构建变体

只有 `-O3`（benchmarks 固定）。缺 `--release` / `--debug` 区分与条件编译（类似 `#[cfg]`）。

### 16. `l doc` 产物不被验证

`l doc` 只生成 Markdown，文档中的示例代码**不会被编译**，会随版本腐坏。

缺：文档测试（doctest）。

### 17. 文档与实现不同步

`README.md` 写“123 条自动化用例”，实际 141 条。

---

## 建议推进顺序

| 顺序 | 项目 | 范围 | 风险 |
| --- | --- | --- | --- |
| 1 | `Array` 补全基本操作 | 纯 stdlib | 低 |
| 2 | `Map` 键拷贝 + `remove` | 纯 stdlib | 中（影响 properties / json / cli） |
| 3 | JSON 嵌套 + 序列化 | 纯 stdlib | 中 |
| 4 | 日志分级 + 时间戳 | 纯 stdlib | 低 |
| 5 | 模块命名空间 | 编译器（lexer/parser/loader/codegen） | 高 |
| 6 | 调试信息 `-g` | 编译器 + 构建 | 中高 |

第 1–4 项不触碰编译器，可用 `--filter` 定向验证；第 5 项是 100 文件规模的关键瓶颈，需要单独立项。
