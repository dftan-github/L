# L 语言能力缺口清单

**初版：2026-11-15 · 复核：2026-11-16**

面向目标：**用 L 写大项目**（万行以上、多人协作、需要长期维护）。

本文基于代码实况盘点（每次复核必须重新核对源码，不沿用旧结论）：

- `stdlib/`：25 个模块
- `runtime/`：9 个 C 源文件（callback / io / memory / net / regex / sys / thread / tls / task）
- `compiler/`：14 个子目录（ast / codegen / compile / comptime / diagnostic / doc / iface / jit / lexer / module / package / parser / semantic）
- `tests/cases/`：161 条自动化用例（`ruby tests/run_tests.rb --all`）

> **2026-11-16 复核结论：17 项中 7 项已完成、2 项部分完成、8 项仍有效。**
> 初版低估了当时的实现进度，以下逐项标注了代码证据与复核日期。
> 引用本文排期前，请先跑一遍 `grep` 核对，不要直接采信结论。

状态图例：`[完成]` 已落地并有代码证据 · `[部分]` 有基础实现但不完整 · `[缺失]` 仍待实现

---

## 原「阻塞级」项 —— 复核后全部解除

初版把这个分类定义为“不解决就写不成大项目”。**复核后该分类下的 4 项已全部落地或不再成立**，
因此本分类撤销。这几项不再是规模扩张的前置门槛。

### 1. `[完成]` 模块级命名空间

初版判断：符号全局展平，100 文件必然重名。

实际：`use` 语法已实现路径限定与别名。

- 语法：`use net::http::Server;` / `use net::http::Server as Http;`
- AST：`compiler/ast/ast.hpp` → `struct UseStmt { std::vector<std::string> path; std::string alias; }`
- 解析：`compiler/parser/parser.cpp` 处理 `a::b::Symbol` 及可选 `as Alias`
- 语义：别名只影响名称解析，**不降低为运行时开销**（符合 Rule 109 零成本约束）
- 类型位置亦支持限定名：`net::http::Server s`

仍缺：`mod a.b.c` 式的**层级模块声明**（当前以文件为模块单元）、`internal` 中间可见性。
这不阻塞多文件项目，但模块粒度仍是“一个文件一个模块”。

### 2. `[部分]` 包管理依赖解析

现状：`l.toml` 的 `[package]` / `[dependencies]` 可解析，`vendor/` 按名查找 `<dep>.li` 与 `lib<dep>.a`，
循环依赖有检测（`[ERROR] Circular dependency detected`）。

- `version` 字段：`compiler/package/toml.cpp` 仅解析为字符串，**无约束求解**
- 锁文件：**不存在**，无法保证构建可重现

仍缺：版本约束（`^1.2` / `~1.2` / 范围）、`l.lock`、`dev-dependencies`、workspace、
依赖替换（patch）。**当前依赖版本号只是注解，不参与任何决策。**

### 3. `[完成]` 增量编译

初版判断：改任一文件都要全量重编。

实际：模块级缓存已实现。

- `compiler/main.cpp` → `incrementalCacheDir()` + `buildCacheKey(unit_names, import_paths, shared_lib, debug_info, opt_level)`
- 缓存命中时**跳过 parse + semantics + codegen 全流程**，直接复用 `module.o` / `module.ll`
- 缓存键包含优化级别与调试开关，切换 `-O3` / `-g` 不会误命中

仍缺：模块级（而非整工程级）粒度、并行编译。

### 4. `[完成]` 调试信息

初版判断：不生成 DWARF / PDB，崩溃栈只有地址。

实际：`-g` / `--debug` 已接入。

- `compiler/main.cpp` 解析 `-g` / `--debug` → `debug_info = true`
- `codegen->setDebugInfo(debug_info)` / `codegen->finalizeDebugInfo()`

已纳入缓存键，`-g` 与非 `-g` 构建互不污染。

### 5. `[缺失]` 泛型实例化无管控

现状：`<T: Interface>` 全单态化，嵌套上限 2，**无实例化去重与数量统计**。

后果：`Array<Map<Array<X>>>` 组合增多时，代码体积与编译时间会失控，且无诊断提示。

建议：加入实例化去重与数量上限，超限时给出明确诊断而非静默膨胀。

---

## 一、库层面 —— 大部分已补齐

### 6. `[完成]` `Array<T>` 操作补全

初版判断：只有 5 个方法。

实际 13 个 pub 方法（`stdlib/array.l`）：
`push` · `get` · `set` · `len` · `each` · `pop` · `insert` · `remove` · `reverse` · `contains` · `index_of` · `clear`

仍缺：`slice`、`sort`、`extend`、`map`/`filter`/`reduce` 等高阶操作。
高阶层可用 `each` + 手写循环替代，不阻塞。

### 7. `[完成]` `Map` 键所有权

初版判断：键存 `key.c_str()` 的外借指针，临时 String 作键会悬空。

实际：已改为 owning copy（`stdlib/map.l`）。

- 第 28–30 行注释明确 "Owning heap copy of a key. Never store a borrowed `String.c_str()`"
- 实现：`return key.slice(0, key.length()).c_str()`
- 已补 `remove` / `contains` / `keys` / `values`

仍缺：`Map` 仍是 `Map<V>`，键类型固定 String，无法用 `i32` 或类实例作键，
也无自定义哈希 —— **与 Rule 20 的 `Map<K, V>` 规格仍有偏差**。

### 8. `[完成]` JSON 嵌套与序列化

初版判断：只支持单层 Object，值为原始文本片段。

实际：基于 `CPropertys` 属性树的完整解析与序列化（`stdlib/json.l`）。

- `parse(String) -> Result<CPropertys, String>`
- `stringify(CPropertys) -> String`
- 底层依赖 `stdlib/properties.l`（252 行，支持嵌套属性树与 typed config）

仍缺：JSON 严格模式下的大整数保真（数值统一走 i32/i64/f64 路径）、错误定位的行列号。

### 9. `[部分]` 日志分级

初版判断：三个函数写死输出。

实际：已有时间戳与前缀（`stdlib/log.l`）。

- `log_info` / `log_warn` / `log_error` → `[INFO] {ts} {msg}` 格式
- 时间格式通过 `format_time(time_now(), "%Y-%m-%d %H:%M:%S")`

仍缺：**级别过滤**（无法关闭 INFO 只留 WARN/ERROR）、结构化字段、输出目标
（文件 / 滚动 / 异步）、按模块设定级别。当前三档均为无条件 `print`。

### 10. `[缺失]` 配置无分层与校验

实际：读 JSON 成属性树，再取值。

缺：`默认值 < 文件 < 环境变量 < 命令行` 覆盖链、schema 校验、热加载。

### 11. `[部分]` HTTP 客户端能力弱

实际（`stdlib/http.l`）4 个函数：

- `http_get(host, port, path) -> String` — 返回原始响应文本
- `https_get(host, path) -> String` — 走 Schannel
- `http_status_ok(response) -> bool`
- `http_body(response) -> String`
- `http_server.l`（124 行）提供服务端能力

仍缺：POST / PUT、请求头设置、超时、重定向、chunked 解码、HTTPS 证书配置面。
**只能发 GET，且无超时控制** —— 这是实际项目里最先撞墙的一项。

### 12. `[缺失]` 没有 L 侧测试框架

现状：`stdlib/` 中**不存在** `assert` / `expect` 任何形式，测试全部由外部 `tests/run_tests.rb` 驱动。

缺：L 内可用的断言、测试分组、fixture、diff 输出。

后果：想给自己的项目写单元测试，必须跳出 L 再建一套外部驱动。
这一项对“库作者”影响最大，对“应用作者”影响可控（可用返回码 + 外部脚本兜底）。

### 13. `[缺失]` 并发原语单一

实际（`stdlib/thread.l`，全文 26 行）：

- `spawn { ... } -> ptr` 句柄，需显式 `join(handle)`
- `sync { ... }` 映射到全局 mutex（`l_mutex_global`）
- 原子操作：`l_atomic_add_i32` / `load_i32` / `store_i32`
- 可观测：`l_get_lock_count` / `l_get_unlock_count` / `l_reset_lock_counts`

缺：channel、线程池、读写锁、`Future` / `await`。

风险提示：`spawn` 的捕获通过把**栈地址**塞进 ctx 传递，生命周期由人工保证，易出错。
叠加 Rule 55 的 Phase 1 策略（spawn 后立即 join），当前**无法表达真正的并行任务图**。

---

## 二、工程与运维

### 14. `[部分]` 可观测性

有：`l_get_alloc_count`（内存计数）、`l_get_lock_count` / `l_get_unlock_count`（锁计数），
可用于泄漏检测与并发验证（Rule 44 / Rule 58）。

缺：metrics 导出、trace、结构化日志出口、耗时统计。

### 15. `[完成]` 构建变体

初版判断：只有 `-O3`。

实际：`-O0` / `-O1` / `-O2` / `-O3` / `--release` 均可选，另有 `-g` / `--debug` 独立开关。
（`compiler/main.cpp` 参数解析）。

仍缺：条件编译（类似 `#[cfg]`）—— 同一份源码无法按构建目标裁剪代码。

### 16. `[缺失]` `l doc` 产物不被验证

`l doc` 只生成 Markdown，文档中的示例代码**不会被编译**，会随版本腐坏。

缺：doctest（把文档示例拉出来编译执行）。

### 17. `[完成]` 文档与实现不同步

初版记录：`README.md` 写“123 条自动化用例”，实际 141 条。

2026-11-16 复核：README 已更新为 161 条（徽章 + 两处正文）。
本次复核共修正 3 处（注意原文有两个 `148`，易漏改第二处）。

> 教训：用例数字散落在多处时，改一处必然漏改。后续建议只在 `tests/` 下维护单一
> 计数来源，文档引用而非硬编码。

---

## 建议推进顺序（2026-11-16 复核后）

按“实际项目会先撞哪面墙”排序，而非按初版的实现难度：

| 顺序 | 项目 | 范围 | 风险 | 理由 |
| --- | --- | --- | --- | --- |
| 1 | HTTP 客户端补全（POST/头/超时） | 纯 stdlib | 低 | 只能发 GET 且无超时，实项目最先撞墙 |
| 2 | 并发原语（channel / 线程池） | stdlib + runtime | 中高 | 当前 spawn 立即 join，无法表达并行任务图 |
| 3 | 包管理版本约束 + 锁文件 | 编译器 package | 中 | 开发机可构建、换机失败，比性能问题更伤人 |
| 4 | L 侧测试框架（assert/expect） | 纯 stdlib | 低 | 库作者刚需，应用作者可外部兜底 |
| 5 | 日志级别过滤 | 纯 stdlib | 低 | 一行判断的事，但影响生产可用性 |
| 6 | 泛型实例化上限与诊断 | 编译器 codegen | 中 | 防止大项目编译时间静默失控 |

已撤销的初版建议（1 Array / 2 Map / 3 JSON / 4 日志时间戳 / 5 命名空间 / 6 调试信息）：
**前 5 项均已完成**，第 4 项仅剩“级别过滤”子项并入上表第 5 位。

---

## 复核方法（下次更新请照做）

不要凭记忆判断某一项是否还在。逐项跑：

```console
# 1 命名空间
grep -n "UseStmt" compiler/ast/ast.hpp

# 3 增量编译
grep -n "incrementalCacheDir\|buildCacheKey" compiler/main.cpp

# 4 调试信息
grep -n '"-g"\|setDebugInfo\|finalizeDebugInfo' compiler/main.cpp

# 6 Array 方法
grep -c "pub fun" stdlib/array.l

# 7 Map 键所有权
grep -n "Owning heap copy\|slice(0" stdlib/map.l

# 8 JSON 嵌套
grep -n "pub fun parse\|pub fun stringify" stdlib/json.l

# 12 L 侧测试框架（应为空）
grep -n "pub fun assert\|pub fun expect" stdlib/*.l

# 13 并发原语
cat stdlib/thread.l
```

用例数以 `ruby tests/run_tests.rb --all` 的实际输出为准。
