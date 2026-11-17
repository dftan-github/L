# L 能力全景

**面向：想判断"该不该用 L 写我的项目"的评估者**
**复核日期：2026-11-17 · 对应测试：169 条全通过**

本文是**正向能力清单**。它与另外三份文档的分工：

| 文档 | 回答什么 |
| --- | --- |
| **本文** | L **现在能做什么**、**适合做什么**、**不该碰什么** |
| [`README.md`](../README.md) | L 的**卖点**：7 个差异化特性，带可运行示例 |
| [`2026-11-15-capability-gaps.md`](2026-11-15-capability-gaps.md) | L **还缺什么**（17 项缺口反向清单） |
| [`备忘.md`](../备忘.md) | L 的**设计意图**与长期蓝图（含未实现部分） |

> ⚠️ 阅读须知：本文每条能力都标注了**代码位置**，可直接核实。
> 未标注位置的描述一律视为未经验证，请勿采信。

---

## 一、一句话结论

L 现在的定位是：**借用 C 生态、语法接近脚本语言的静态编译型语言**。
它编译产出原生代码（LLVM 后端），性能与 `clang++ -O3` 同量级，
但缺少成熟包生态与高并发运行时。

**它当前的最佳甜点区是：单机、命令行、围绕文件与网络的工具类程序。**

---

## 二、能力速查表

状态说明：`✅ 可用` · `⚠️ 可用但有明显限制` · `❌ 不可用`

### 2.1 语言核心

| 能力 | 状态 | 说明 | 代码位置 |
| --- | --- | --- | --- |
| 静态类型 + `:=` 推断 | ✅ | 局部变量类型推断；无 `let`/`var` | `.cursor/rules` Rule 3 |
| 不可变默认 + `!` 标记可变 | ✅ | 编译期强制，写入不可变变量直接报错 | Rule 3 |
| 类（纯数据，无继承） | ✅ | `class` / `partial class` 分部合并 | `compiler/parser/` |
| 接口 + 动态派发 | ✅ | 胖指针 `{data_ptr, vtable_ptr}`，vtable 首项为 TypeID | Rule 62 / 63 |
| 泛型（单态化） | ✅ | `<T: Interface>` 约束；嵌套上限 2 层 | `codegen.cpp:1398` |
| 属性访问糖 | ✅ | `obj.age` → `get_age()` / 私有字段兜底 | Rule 34 / 70 |
| `delegate` 隐式组合 | ✅ | 方法提升，避免手写转发样板 | Rule 97 |
| 闭包 / Lambda | ✅ | 捕获变量的闭包；逃逸时自动堆提升 | Rule 40 / 41 / 77 |
| `match` 模式匹配 | ⚠️ | 仅字面量与 `_` 通配；无解构、无枚举 | Rule 30 |
| `?.` 与 `??` | ✅ | 安全链式访问 + 空值合并，左操作数只求值一次 | Rule 101 |
| `Result<T,E>` + `?` 运算符 | ✅ | 无异常，错误显式传播 | Rule 53 / 54 |
| `comptime` 编译期执行 | ✅ | 语法树注入，零运行时开销 | Rule 12 / 21 |
| `LCMD` 编译期指令 | ✅ | `//LCMD_xxx` 触发展开；写在类体内直接注入成员 | Rule 18 / 19 |
| 模板字符串 | ✅ | `"Hello {name}"` 自动 `l_int_to_str` | Rule 47 |
| 区间循环 `for i in a..b` | ✅ | 半开区间，退化为 `while` | Rule 44 |
| 原生 Windows / Linux | ✅ | 同一份源码，平台差异在 runtime 内 | `runtime/` |

### 2.2 类型系统与内存

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 数值类型 | ✅ | `i32` / `i64` / `f64` / `bool`；自动提升 `i32 → i64 → f64` |
| 字符串 | ✅ | 堆分配 + 21 个方法（`split` / `replace_all` / `trim` / `index_of` …） |
| `Array<T>` | ⚠️ | 12 个方法；**缺 `sort` / `slice` / `map` / `filter`** |
| `Map<V>` | ⚠️ | 7 个方法；**键类型只能是 `String`**，与 Rule 20 规格有偏差 |
| `Optional<T>` | ✅ | 显式可空，杜绝隐式 null |
| `Result<T,E>` | ✅ | 显式错误 |
| `Variant` | ✅ | 动态标签值：`i32` / `String` / `f64` / `bool` |
| 自动释放 | ✅ | `heap` 对象离开作用域自动 `l_free`；`defer` 手写清理 |
| 内存池 `arena` | ✅ | 批量分配，整体释放 |
| 无 GC | ✅ | 确定性析构，无停顿 |
| 裸指针 `*p` / `p[i]` / `&x` | ✅ | 与 C 语义一致，不做越界检查 |

### 2.3 并发模型

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| `spawn { }` | ⚠️ | **Phase 1 策略：创建后立即 join**，即"不能真正并行" |
| `sync { }` | ✅ | 映射全局 mutex；`spawn` 内访问 `!` 变量未加锁会**编译报错** |
| 原子操作 | ✅ | `l_atomic_add_i32` / `load` / `store`，无锁计数可用（实测 10 线程 × 10000 次正确） |
| channel / 线程池 | ❌ | **未实现** |
| `async` / `await` / `Future` | ❌ | 未实现 |

> **这是目前最需要警惕的一项。** `spawn` 捕获的是**栈地址**，
> 生命周期靠人工保证；配合"立即 join"策略，当前无法表达真正的并行任务图。
> 写高并发服务前务必先确认这一点（见 `stdlib/thread.l`，全文 22 行）。

### 2.4 标准库（26 个模块）

| 模块 | 行数 | 状态 | 能力 |
| --- | --- | --- | --- |
| `string.l` | 244 | ✅ | 21 个方法：`slice` / `split` / `trim` / `replace_all` / `index_of` / `ends_with` … |
| `json.l` | 500 | ✅ | 完整解析 + 序列化，支持嵌套（基于属性树） |
| `properties.l` | 283 | ✅ | 嵌套属性树，支撑配置系统 |
| `http.l` | 343 | ✅ | GET / POST / PUT / DELETE、超时、chunked 解码、HTTPS（Schannel） |
| `http_server.l` | 133 | ✅ | `http_serve` / `http_serve_once` |
| `map.l` | 211 | ⚠️ | 开放寻址，键只能是 String |
| `config.l` | 156 | ✅ | 分层覆盖（默认 < 文件 < 环境变量 < 命令行）+ 类型化取值 |
| `test.l` | 147 | ✅ | L 原生断言：`expect_eq` / `expect_ok` / `test_report` |
| `log.l` | 126 | ✅ | 5 级过滤、文件/控制台/静默出口、key=value 结构化 |
| `array.l` | 124 | ⚠️ | 12 个方法，缺高阶操作 |
| `variant.l` | 93 | ✅ | 动态标签值 |
| `fs.l` | 76 | ✅ | 存在性 / 目录 / 删除 / 重命名 / 复制 / 遍历 |
| `file.l` | 66 | ✅ | `read_all` / `write_all` / `append_all` |
| `path.l` | 61 | ✅ | 路径拼接与规范化 |
| `io.l` | 60 | ✅ | C 运行时 extern 声明 + print 系列 |
| `cli.l` | 29 | ✅ | `parse_flags` 命令行解析 |
| `env.l` | 29 | ✅ | `get_args` / `get_env` / `set_env` |
| `process.l` | 28 | ✅ | `run_command` / `run_capture_stdout` |
| `time.l` | 28 | ✅ | 时间戳 + `format_time` |
| `regex.l` | 24 | ⚠️ | 自制引擎：字面量 / `.` / `[a-z]` / `*` `+` `?`；**不支持分组与回溯** |
| `thread.l` | 22 | ⚠️ | 见 §2.3 |
| `memory.l` | 22 | ✅ | 分配与存取原语 |
| `optional.l` | 20 | ✅ | `Optional<T>` |
| `result.l` | 19 | ✅ | `Result<T,E>` |
| `net.l` | 14 | ✅ | TCP socket（bind / listen / accept / connect / send / recv） |
| `math.l` | 11 | ⚠️ | 极简，只有基本函数 |

### 2.5 工具链

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| `l build` | ✅ | 输出原生可执行文件 |
| `l run file.l` | ✅ | 自动补齐 stdlib 依赖，一条命令跑起来 |
| `l eval '<code>'` | ✅ | **ORC JIT，毫秒级**，不落临时文件 |
| `l repl` | ✅ | 交互式，出错不退出 |
| `l doc` | ⚠️ | 生成 Markdown；**示例代码不参与编译验证** |
| `l gen-interface` | ✅ | 导出 `.li` 接口文件（只有 `pub` 签名） |
| `--shared` | ✅ | 编译为 `.dll` / `.so`，C ABI 可被 Python / Ruby / C 直接调用 |
| `-O0` ~ `-O3` / `--release` / `-g` | ✅ | 独立开关，缓存键已隔离 |
| 增量编译 | ✅ | 模块级缓存，命中跳过 parse + codegen |
| LTO | ✅ | `-flto -fuse-ld=lld`，C runtime 亦为 bitcode |
| 包管理 | ✅ | `l.toml` + `vendor/` + 语义版本约束 + `l.lock` 锁文件 |
| 多文件编译 | ✅ | 多 `.l` 合并为单个 LLVM module |
| 命名空间 | ✅ | `use net::http::Server as Http;` |
| 调试符号 | ✅ | `-g` 生成 DWARF / PDB |
| 格式化工具 | ❌ | 无 `l fmt` |
| 包仓库 / 在线拉取 | ❌ | 只能手工 `vendor/` 放入 |

---

## 三、适合做什么

### ✅ 现在就能做，且体验不错

| 场景 | 为什么适合 | 现成参考 |
| --- | --- | --- |
| **命令行工具** | `parse_flags` + `fs` + `process` + 原生单文件可执行 | [`examples/config_parser.l`](../examples/config_parser.l) |
| **文本 / 配置处理** | `string` 21 方法 + `json` 嵌套 + `properties` 属性树 | [`examples/typed_config.l`](../examples/typed_config.l) |
| **HTTP 客户端 / 爬取** | 四种方法 + 超时 + chunked + HTTPS，不会挂死 | [`examples/http_client.l`](../examples/http_client.l) |
| **配置文件驱动的应用** | 分层覆盖链 + 类型化取值，外部可改行为 | [`stdlib/config.l`](../stdlib/config.l) |
| **给 Python / C++ 写高性能插件** | `--shared` 导出 C ABI，自包含（runtime 静态链入） | `compiler/main.cpp` `--shared` |
| **词法 / 语法分析器** | 闭包 + 模式匹配 + 泛型容器 | [`examples/mini_lexer.l`](../examples/mini_lexer.l) · [`mini_parser.l`](../examples/mini_parser.l) |
| **编译期代码生成** | `comptime` + `LCMD` 直接注入 AST，零运行时开销 | [`examples/plugin_framework.l`](../examples/plugin_framework.l) |
| **批处理 / 数据转换** | 原生速度 + 无 GC 停顿 + 文件系统全覆盖 | [`examples/fibonacci.l`](../examples/fibonacci.l)（起点） |
| **性能敏感的计算核心** | CLang `-O3` 同量级（README §7 有实测对比） | `benchmarks/` |

### ⚠️ 能做，但要先权衡

| 场景 | 限制 | 折中方案 |
| --- | --- | --- |
| **HTTP 服务端** | `spawn` 立即 join，无法并发处理多连接 | 一次一个请求（`serve_once`）；或把每个连接交给 C 侧线程 |
| **并发任务处理** | 无 channel / 线程池 | 用原子计数 + 手动线程；或下沉到 C 扩展 |
| **排序 / 集合运算** | `Array` 无 `sort`，`Map` 键限 String | 手写快排；或数值键转字符串 |
| **复杂文本匹配** | 正则不支持分组与回溯 | 用 lexer 思路手写状态机 |
| **多版本依赖共存** | 同一依赖只能一个版本 | 拆分 vendor 目录 |

### ❌ 现在不该做

| 场景 | 原因 |
| --- | --- |
| **GUI 桌面应用** | 无窗口 / 图形库绑定；只能用 Win32 手写 extern |
| **Web 框架 / 高并发微服务** | 并发模型是硬伤（见 §2.3） |
| **依赖丰富生态的项目** | 无在线包仓库，三方库全靠手工 vendor |
| **机器学习 / 科学计算** | 无矩阵库、无 GPU 绑定、无 numpy 等价物 |
| **需要成熟 ORM / 数据库驱动的后端** | 无任何数据库驱动 |
| **移动端 / 嵌入式** | 未验证，无交叉编译工具链 |
| **多人长期协作的大型代码库** | 无 `l fmt`、无 LSP / IDE 补全、无 linter |

---

## 四、性能定位

L 编译为 LLVM IR 后走与 clang 相同的优化管线（含 LTO）。
`README.md` §7 的实测对比（`benchmarks/run_bench.rb`）：

| 测试 | L vs `clang++ -O3` |
| --- | --- |
| 严格等价写法（A 组） | **1.01x**（L 略快） |
| 自由写法（C 用 `new/delete`，A 组） | 1.06x |
| 自由写法（C 组） | 1.25x ~ 1.53x |

结论：**在等价写法下，L 与 C++ 基本无差距**；差距来自 C 侧手工优化而非语言本身。

运行基准：

```console
ruby benchmarks/run_bench.rb
```

> 基准独立于单元测试，始终以 `-O3` 编译（Rule 51）。

---

## 五、上手路径

```console
# 1. 一条命令跑通（自动补齐 stdlib 依赖）
l run examples/config_parser.l

# 2. 交互式试语法（ORC JIT，毫秒级）
l repl

# 3. 单行求值
l eval 'print(1 + 1)'

# 4. 正式构建
l build -O3 -o myapp.exe main.l
```

**建议的阅读顺序**：

1. 本文 §2.1 语言核心 → 知道有哪些语法
2. [`README.md`](../README.md) 的 7 个特性 → 看差异化在哪
3. `examples/` 里挑一个最接近你需求的 → 照着改
4. [`docs/2026-11-15-capability-gaps.md`](2026-11-15-capability-gaps.md) → 动手前确认你要的能力不在缺口里

---

## 六、示例索引

| 示例 | 行数 | 演示什么 |
| --- | --- | --- |
| [`config_parser.l`](../examples/config_parser.l) | 58 | 命令行工具骨架：参数解析 + 文件读取 |
| [`typed_config.l`](../examples/typed_config.l) | 32 | 配置驱动：属性树 + 类型化取值 |
| [`http_client.l`](../examples/http_client.l) | 39 | HTTP GET 客户端 |
| [`http_server.l`](../examples/http_server.l) | 95 | 最小 HTTP 服务端 |
| [`http_parse.l`](../examples/http_parse.l) | 81 | `?.` 与 `??` 处理嵌套 Optional |
| [`mini_lexer.l`](../examples/mini_lexer.l) | 54 | 词法分析器 |
| [`mini_parser.l`](../examples/mini_parser.l) | 109 | 递归下降解析器 |
| [`plugin_framework.l`](../examples/plugin_framework.l) | 368 | `comptime` + `LCMD` 编译期代码生成 |
| [`fibonacci.l`](../examples/fibonacci.l) | 28 | 最小可运行程序 |
| [`all_samples.l`](../examples/all_samples.l) | 7249 | 全部语法特性合集（参考手册性质，由 `tools/gen_all_samples.rb` 生成，带 `row:N` 目录） |

---

## 七、如果 L 缺你要的能力

三种处理方式，按成本从低到高：

1. **`extern module` 绑定 C 库** —— L 直接调用任意 C 函数，无需胶水层
   （示例见 `stdlib/net.l`：14 行声明完整 TCP 能力）
2. **`--shared` 反向嵌入** —— L 作为高性能模块被 Python / C++ 调用
3. **改编译器 / 扩 stdlib** —— 缺口清单里每一项都标注了涉及的目录

判断标准：如果能力缺口在 **§2.3 并发** 或 **生态（包仓库 / GUI / 数据库）**，
短期内不要指望；其余多有 `extern` 兜底路径。

---

## 复核方法

本文数字均可用以下命令核实：

```console
# 模块与行数
ls stdlib/*.l | wc -l
wc -l stdlib/*.l

# 各模块公开 API
grep -n "pub fun" stdlib/string.l

# 并发模型（22 行，几乎全为 extern 声明）
cat stdlib/thread.l

# CLI 子命令
grep -n 'gen-interface\|"doc"\|"run"\|"eval"\|"repl"' compiler/main.cpp

# 用例总数
ruby tests/run_tests.rb --all
```

能力缺失项的详细证据见 [`2026-11-15-capability-gaps.md`](2026-11-15-capability-gaps.md)。
