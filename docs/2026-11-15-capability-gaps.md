# L 语言能力缺口清单

**初版：2026-11-15 · 复核：2026-11-17**

面向目标：**用 L 写大项目**（万行以上、多人协作、需要长期维护）。

本文基于代码实况盘点（每次复核必须重新核对源码，不沿用旧结论）：

- `stdlib/`：26 个模块
- `runtime/`：9 个 C 源文件（callback / io / memory / net / regex / sys / thread / tls / task）
- `compiler/`：13 个子目录（ast / codegen / compile / comptime / diagnostic / doc / iface / jit / lexer / module / package / parser / semantic）
- `tests/cases/`：169 条自动化用例（`ruby tests/run_tests.rb --all`）

> **2026-11-17 复核结论：17 项中 13 项已完成、1 项部分完成、2 项仍有效（并发原语、`l doc` doctest）。**
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

### 2. `[完成]` 包管理版本约束与锁文件

2026-11-16 补齐约束求解与 `l.lock`。

- `compiler/package/semver.{hpp,cpp}`：SemVer 解析 + 约束求解
    - 支持 `^1.2.3`（caret，含 `0.x` 特例）、`~1.2.3`（tilde）、`>=` `>` `<=` `<`、`=1.2.3`、`*`
    - caret 遵循 semver 规范：`^1.2.3 → [1.2.3, 2.0.0)`，`^0.2.3 → [0.2.3, 0.3.0)`，`^0.0.3 → 0.0.3`
- `resolve.cpp`：约束校验接入 DFS，覆盖**根 manifest** 与**传递依赖**两类边
    - `[ERROR] package 'app' requires 'liba' ^2.0.0, found 1.4.2`
    - `[ERROR] package 'liba' requires 'libb' >=3.0.0, found 2.1.7`
    - `[ERROR] package 'app' has invalid version constraint '^^abc' for 'libb'`
- `l.lock`：`[[package]]` 段记录 `name` / `version`，按依赖后序（被依赖者在前）输出
    - 存在有效 pin 时以 pin 为准，磁盘版本升级不会自动改变构建结果
    - pin 不再满足 manifest 约束时判定 stale，自动回落到磁盘真实版本并重写锁文件
    - 无版本号的 vendor 包（合成 manifest）跳过校验，不会误报

仍缺：`dev-dependencies`、workspace、依赖替换（patch）、多版本共存（同一依赖的两个不兼容版本）。

测试：`tests/run_tests.rb --filter=package_versioning`。

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

### 5. `[完成]` 泛型实例化管控

复核结论：初版判断有误，三项能力均已存在。

- **嵌套上限**：`codegen.cpp` → `genericNestingDepth()` + `kMaxGenericDepth = 2`，
  在 `ensureTypeAvailable()` 中最先拦截，报
  `generic instantiation depth limit exceeded (2): 'X<Y<Z<i32>>>' nests 3 generic types`
- **实例化去重**：`specializeClass()` 首行 `if (classes_.count(instance_name) != 0) return true;`，
  同名实例只特化一次
- **约束校验**：`classImplements()` 在特化时校验 `[ERROR] Type 'X' does not implement interface 'I'`

不做的部分：实例化**数量**上限。仓库中没有真实大项目数据能标定阈值，
凭猜测写死一个数字属于无需求驱动的配置，YAGNI。若将来出现编译时间问题，
应以实测数据（实例化个数 vs 编译耗时曲线）为依据再定。

---

## 一、库层面 —— 大部分已补齐

### 6. `[完成]` `Array<T>` 操作补全

初版判断：只有 5 个方法。

实际 12 个方法（`stdlib/array.l`，含 `push` / `get`）：
`push` · `get` · `set` · `len` · `each` · `pop` · `insert` · `remove` · `reverse` · `contains` · `index_of` · `clear`

仍缺：`slice`、`sort`、`extend`、`map`/`filter`/`reduce` 等高阶操作。
高阶层可用 `each` + 手写循环替代，不阻塞。

> 复核记录：2026-11-17 用 `Select-String "^\s*(pub )?fun "` 逐行点数为 12。
> 此前记为 13 是漏掉了 `push` / `get` 不带 `pub` 前缀的误算（它们同样可用）。

### 7. `[完成]` `Map` 键所有权

初版判断：键存 `key.c_str()` 的外借指针，临时 String 作键会悬空。

实际：已改为 owning copy（`stdlib/map.l`）。

- 第 28–30 行注释明确 "Owning heap copy of a key. Never store a borrowed `String.c_str()`"
- 实现：`return key.slice(0, key.length()).c_str()`
- 已补 `remove` / `contains` / `keys` / `len` / `drop`，加原有 `put` / `get` 共 7 个 pub 方法

仍缺：`Map` 仍是 `Map<V>`，键类型固定 String，无法用 `i32` 或类实例作键，
也无自定义哈希 —— **与 Rule 20 的 `Map<K, V>` 规格仍有偏差**。

### 8. `[完成]` JSON 嵌套与序列化

初版判断：只支持单层 Object，值为原始文本片段。

实际：基于 `CPropertys` 属性树的完整解析与序列化（`stdlib/json.l`）。

- `parse(String) -> Result<CPropertys, String>`
- `stringify(CPropertys) -> String`
- 底层依赖 `stdlib/properties.l`（283 行，支持嵌套属性树与 typed config）

仍缺：JSON 严格模式下的大整数保真（数值统一走 i32/i64/f64 路径）、错误定位的行列号。

### 9. `[完成]` 日志分级与结构化输出

初版判断：三个函数写死输出。

- 级别：`0=DEBUG` / `1=INFO` / `2=WARN` / `3=ERROR` / `4=OFF`，默认 INFO
- `log_set_level(i32)` 越界自动钳制到 `[0,4]`；`log_get_level()` 可查询
- `log_emit` 在输出前比较级别，被抑制的调用只付一次整数比较
- 格式：`[LEVEL] {YYYY-MM-DD HH:MM:SS} {msg}`
- 测试：`tests/run_tests.rb --filter=log_level`（`tests/cases/log_level_test.l`）

2026-11-16 补齐文件与静默出口：

- **文件输出**：`log_to_file(path) -> Result<bool, String>`，追加写入；失败时**保持原输出**并返回原因
- **静默**：`log_silence()` 丢弃全部输出（测试用），`log_to_console()` 恢复
- **结构化 key=value**：`log_info_kv` / `log_warn_kv` / `log_error_kv`，输出 `ts [LEVEL] key=value`
    - 用独立参数而非 `Map`，避免每次调用都分配
- **过滤前置**：级别与 sink 检查都在格式化之前，被压掉的调用只花一次比较
- 依赖：`stdlib/file.l` 新增 `append_all(path, content) -> Result<bool, String>`

仍缺：滚动（按大小/日期切分）、异步写入、按模块设定级别。

测试：`tests/run_tests.rb --filter=log_sink`（`tests/cases/log_sink_test.l`）。

### 10. `[完成]` 配置分层与类型化取值

2026-11-16 补齐覆盖链与类型化访问器（`stdlib/config.l`）。

- **覆盖链**：`config_layered(default_path, file_path, cli)` → `Result<Map<String>, String>`
    - 顺序：默认值文件 < 文件 < 环境变量 < 命令行
    - 一次性摊平成 `Map`，之后查表 O(1)，不在每次取值时走四层
- **环境变量**：`config_env_name(key)` 把 `database.port` 映射为 `L_DATABASE_PORT`
    - **仅对配置中已存在的键生效**，避免无关的 `L_*` 变量注入意外设置
- **类型化访问器**：`config_i32` / `config_bool` / `config_get`，缺键返回调用方给的默认值
    - `config_bool` 接受 `true/1/false/0`，其余回落到默认值
- **错误不静默**：显式指定的配置文件解析失败直接返回 `Err`，不当作"没有配置"
- **环境变量写入**：`runtime/sys.c` 新增 `l_set_env`（`_putenv_s` / `setenv`），
  `stdlib/env.l` 暴露 `set_env`

仍缺：schema 校验（字段类型/必填约束的声明式描述）、热加载。

测试：`tests/run_tests.rb --filter=config_layered`（`tests/cases/config_layered_test.l`）。

### 11. `[完成]` HTTP 客户端

2026-11-16 补齐超时、完整响应读取与 POST（`stdlib/http.l` + `runtime/net.c`）。

核心能力：

- `http_request(method, host, port, path, headers, body, timeout_ms)` — 统一驱动
- `http_get` / `http_get_timeout` / `http_post(host, port, path, content_type, body)`
- `http_status_code` / `http_status_ok` / `http_body` / `https_get`（Schannel）
- `http_server.l`（133 行）提供服务端：`http_serve` / `http_serve_once` / `http_handle_client`

**超时是硬性保证**：`runtime/net.c` 新增 `l_socket_recv_timeout`，底层走
`WSAPoll`（Windows）/ `poll`（Unix），返回 `-2` 表示超时而非阻塞。
`l_socket_recv` 保留兼容，内部转调 `-1`（无限等待）。
**静默对端不再能挂死调用方。**

**完整读取**：旧实现单次 `recv` 读 4KB 就返回，大响应会被截断。现按
`Content-Length` 精确读满；无该头时读到对端关闭为止（`Connection: close`）。

仍缺：`PATCH`、重定向跟随、HTTPS 客户端证书配置面（`https_get` 固定 443，无超时参数）。

2026-11-16（第二批）补齐：

- **PUT / DELETE**：`http_put(host, port, path, content_type, body)` / `http_delete(host, port, path)`
- **chunked 解码**：`http_is_chunked` / `http_dechunk` / `http_body_auto`
    - `http_body_auto` 检测 `Transfer-Encoding: chunked` 后自动解块，调用方可无脑使用
    - 截断块返回已读部分而非崩溃或挂起（失败降级优于挂死）
- **hex 解析**：`http_hex_to_i32`，大小写均可，非法输入返回 -1
- **头名大小写**：新增 `http_starts_with_ci`。HTTP 头名大小写不敏感（RFC 7230），
  原 `http_content_length` 用小写比较、服务端发大写，长期靠巧合工作；现已修正

测试：`tests/run_tests.rb --filter=http_client` 与 `--filter=http_chunked`
（`tests/cases/http_client_test.l`、`tests/cases/http_chunked_test.l`）。

### 12. `[完成]` L 侧测试框架

`stdlib/test.l` 提供 L 原生的断言与统计，无需外部驱动：

- 断言：`expect_true` / `expect_false` / `expect_eq` / `expect_int` / `expect_i64`
  / `expect_some` / `expect_none` / `expect_ok` / `expect_err`
- 分组：`test_group(name)`，失败输出带组名前缀
- 统计：`test_report() -> i32`（0 = 全过），`test_reset()` 清空计数
- 失败不中断：所有断言都执行完再汇总，一次看到全部红点

用法：

```l
import "stdlib/test.l"

fun main() -> i32 {
    test_group("math")
    expect_int(2 + 2, 4, "two plus two")
    return test_report()
}
```

仍未覆盖：fixture 生命周期、参数化用例、与外部 `run_tests.rb` 的互通。
对“给自己的模块写单元测试”这一需求已足够。

**顺带修掉的编译器 Bug**：实现过程中发现，`:=` 推断出的 String 局部变量被
装箱成 `ptr`（8 字节），而 `concat` 等方法返回整个 `String` 结构体（16 字节）。
赋值时把 16 字节写进 8 字节槽，**溢出破坏相邻栈变量并触发 0xC0000005**。
已在 `emitAssign` 补上“结构体值写入指针槽”的分支（堆分配后存指针）。
回归用例：`tests/cases/str_reassign_test.l`。

### 13. `[缺失]` 并发原语单一

实际（`stdlib/thread.l`，全文 22 行，几乎全是 `extern module c_thread` 声明）：

- `spawn { ... } -> ptr` 句柄，需显式 `join(handle)`
- `sync { ... }` 映射到全局 mutex（`l_mutex_global`）
- 原子操作：`l_atomic_add_i32` / `load_i32` / `store_i32`
- 可观测：`l_get_lock_count` / `l_get_unlock_count` / `l_reset_lock_counts`
- 线程：`l_spawn` / `l_thread_join` / `l_sleep_ms`

缺：channel、线程池、读写锁、`Future` / `await`。

风险提示：`spawn` 的捕获通过把**栈地址**塞进 ctx 传递，生命周期由人工保证，易出错。
叠加 Rule 55 的 Phase 1 策略（spawn 后立即 join），当前**无法表达真正的并行任务图**。

---

## 二、工程与运维

### 14. `[部分]` 可观测性

有：`l_get_alloc_count`（内存计数）、`l_get_lock_count` / `l_get_unlock_count`（锁计数），
可用于泄漏检测与并发验证（Rule 44 / Rule 58）。

2026-11-17：**结构化日志出口已落地**（`stdlib/log.l`）——`log_to_file` 文件输出、
`log_silence` 静默、`log_*_kv` key=value 字段。原先「缺结构化日志出口」一条已消除。

仍缺：metrics 导出（Prometheus 等）、trace / span、函数耗时统计、滚动与异步写入。

### 15. `[完成]` 构建变体

初版判断：只有 `-O3`。

实际：`-O0` / `-O1` / `-O2` / `-O3` / `--release` 均可选，另有 `-g` / `--debug` 独立开关。
（`compiler/main.cpp` 参数解析）。

仍缺：条件编译（类似 `#[cfg]`）—— 同一份源码无法按构建目标裁剪代码。

### 16. `[缺失]` `l doc` 产物不被验证

`l doc` 只生成 Markdown，文档中的示例代码**不会被编译**，会随版本腐坏。

缺：doctest（把文档示例拉出来编译执行）。

> 2026-11-17 复核：`compiler/doc/` 下 `grep -i "doctest|compile.*example"` 命中数为 0，确认仍未实现。

### 17. `[完成]` 文档与实现不同步

初版记录：`README.md` 写“123 条自动化用例”，实际 141 条。

复核轨迹：141 → 161（2026-11-16）→ **169**（2026-11-17）。
本次复核发现 README 徽章仍停留在 161，已同步更新。

> 教训：用例数字散落在多处时，改一处必然漏改。后续建议只在 `tests/` 下维护单一
> 计数来源，文档引用而非硬编码。

---

## 建议推进顺序（2026-11-17 复核后）

按“实际项目会先撞哪面墙”排序，而非按初版的实现难度：

| 顺序 | 项目 | 范围 | 风险 | 理由 |
| --- | --- | --- | --- | --- |
| 1 | 并发原语（channel / 线程池） | stdlib + runtime | 中高 | 当前 spawn 立即 join，无法表达并行任务图 |
| 2 | HTTP 重定向跟随 + `PATCH` | 纯 stdlib | 低 | 对接真实服务端时的常见缺失 |
| 3 | 配置 schema 校验 | 纯 stdlib | 中 | 分层已就绪，缺声明式字段约束 |
| 4 | `Map<K, V>` 键类型泛化 | 编译器 codegen + stdlib | 中高 | 与 Rule 20 规格仍有偏差，影响非 String 键场景 |
| 5 | `Array` 高阶方法（slice / sort / map / filter） | 纯 stdlib | 低 | 可用 `each` 替代，但样板代码多 |
| 6 | 日志滚动与异步写入 | 纯 stdlib | 低 | 长跑服务会撑爆日志文件 |
| 7 | `l doc` doctest | 编译器 doc | 中 | 防止文档示例腐坏 |

已完成（无需再排期）：

| 项目 | 完成日期 |
| --- | --- |
| 包管理版本约束 + `l.lock` | 2026-11-16 |
| 泛型实例化管控（深度/去重/约束） | 复核确认已存在 |
| HTTP PUT / DELETE / chunked / 头名大小写 | 2026-11-16 |
| 配置分层 + 类型化取值 | 2026-11-16 |
| 日志文件/静默出口 + 结构化字段 | 2026-11-16 |

已撤销的初版建议（1 Array / 2 Map / 3 JSON / 4 日志时间戳 / 5 命名空间 / 6 调试信息）：
**全部完成**。

已完成批次时间线：
- 2026-11-16 P0：HTTP 超时与完整读取、POST、日志级别过滤
- 2026-11-16 P1：L 侧测试框架（`stdlib/test.l`），并修复装箱 String 重赋值溢出 bug
- 2026-11-16 批 2~6：包管理版本约束 + `l.lock`、HTTP PUT/DELETE/chunked、
  配置分层与类型化取值、日志文件/静默出口与结构化字段；
  泛型实例化经复核确认（深度限制、去重、约束校验）均已存在，数量上限按 YAGNI 不做
- 2026-11-17 复核：修正 Array/Map/thread/http_server 的方法数与行数，
  补齐各项最新证据；用例数同步为 169

下一批建议从「并发原语」起，因为它牵涉 `spawn` 的栈地址捕获，属架构改动。

---

## 复核方法（下次更新请照做）

不要凭记忆判断某一项是否还在。逐项跑：

```console
# 0 总体规模（本文顶部三行数字）
ls stdlib/*.l | wc -l          # stdlib 模块数
ls runtime/*.c | wc -l         # runtime C 源文件数
ls -d compiler/*/              # compiler 子目录数

# 1 命名空间
grep -n "UseStmt" compiler/ast/ast.hpp

# 2 包管理版本约束与锁文件
ls compiler/package/semver.*
grep -n "checkConstraint\|lockfile_stale" compiler/package/resolve.cpp
grep -n "writeLockfile" compiler/main.cpp

# 3 增量编译
grep -n "incrementalCacheDir\|buildCacheKey" compiler/main.cpp

# 4 调试信息
grep -n '"-g"\|setDebugInfo\|finalizeDebugInfo' compiler/main.cpp

# 5 泛型实例化
grep -n "kMaxGenericDepth\|genericNestingDepth\|classes_.count" compiler/codegen/codegen.cpp

# 6 Array 方法（应为 12）
grep -c "^\s*\(pub \)\?fun " stdlib/array.l

# 7 Map 键所有权
grep -n "Owning heap copy\|slice(0" stdlib/map.l

# 8 JSON 嵌套
grep -n "pub fun parse\|pub fun stringify" stdlib/json.l

# 9 日志级别 + 出口
grep -n "log_set_level\|log_to_file\|log_silence\|_kv" stdlib/log.l

# 10 配置分层
grep -n "config_layered\|config_env_name\|config_i32" stdlib/config.l

# 11 HTTP 能力面
grep -n "pub fun http_" stdlib/http.l

# 12 L 侧测试框架
grep -n "pub fun expect" stdlib/test.l

# 13 并发原语
cat stdlib/thread.l

# 14 可观测性
grep -n "l_get_alloc_count" runtime/memory.c
grep -n "l_get_lock_count" runtime/thread.c

# 16 doctest（命中数应为 0）
grep -ri "doctest" compiler/doc/
```

用例数以 `ruby tests/run_tests.rb --all` 的实际输出为准。
