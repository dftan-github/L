# L Language

**目标：解放程序员的心智负担——无废话代码。**

把仪式感、样板和记忆负担扔给编译器；你只写**业务真正需要的那几行**。  
L 是一门 **编译型、静态强类型** 的系统语言（LLVM 22）：短得像脚本，快得像 C，类型与安全在编译期钉死——**不是解释器，不是动态类型猜谜**。  
当前主线由 **`ruby tests/run_tests.rb` 的 123 条自动化用例**端到端兜底（Lexer / AST / Codegen / 并发 / FFI / 元编程 / `l doc` …）。

### 心智负担怎么被拿掉

| 你不必再操心 | 编译器 / 语言替你做 |
| --- | --- |
| 分号、`let`/`var`、尾下划线 | ASI；`:=` / `!`；字段自动加 `_`，对外只写 `u.age` |
| 「这是编译期常量还是运行期锁定？」 | `const` vs `constd` 两个关键字说完，不再靠命名约定 |
| 满屏 getter 样板或裸 `age_` | 有 accessor 就走方法，没有且 `pub` 就直接字段——**一种写法** |
| 记参数位置、或抄一堆重载 | 关键字参数 `name: val`；位置参数在前，关键字可乱序 |
| 每个出口手写 `free` | 作用域结束自动 `l_free`；需要时再 `defer` |
| 「这里会不会数据竞争？」 | `spawn` 里写 `!` 没进 `sync` → **直接编不过** |
| 再解析一遍 YAML/TOML | 配置就是 L 源码，相对赋值降成内存写入 |
| 宏语言第二套语法 | `//LCMD_` 用 L 自己生成代码并注入 |
| 宏里手搓唯一符号名 | 编译期 `uid_counter()` → 单调 `i64`，直接拼进注入源码 |
| 「配置对不对？要不要禁编？」 | `comptime { … }` 前端跑完即剥；`compile_error` / `comptime_assert` 直接否决 |
| `null` 心智税 | 只有 `Optional` / `Result`，没有隐式空 |

原则很简单：**能推断的不写，能注入的不抄，能编译期拦的不靠人记。**

---

## L 的特色（一眼看懂）

| 别人常见做法 | L 的选择（少废话） |
| --- | --- |
| 解释执行 / 动态类型运行时 | **编译型**：AOT 原生码或 ORC JIT；类型编译期钉死 |
| `any` / 隐式转型 / 运行时才炸 | **静态强类型**：`:=` 只是少写类型名，结果仍是具体类型 |
| `let` / `var` / `mut` 堆叠 | `:=` 声明，`!` 才可变；读从不带 `!` |
| `#define` / `constexpr` / `final` 混用 | **`const`**（编译期内联）与 **`constd`**（运行期一次锁定）分工明确 |
| `obj.age_` 泄漏物理名 | 源码禁止尾 `_`；声明 `i32 age`，对外一律 `u.age` |
| 只靠位置传参 / 重载爆炸 | **关键字参数** `role: "admin"`；位置在前，关键字可乱序 |
| 类继承 + 对象内藏 vptr | **无继承**；接口胖指针 `{data, vtable}` |
| GC / 手工 `free` 满天飞 | `heap` + `defer` + **自动 `l_free`** |
| 数据竞争靠纪律 | 少写 `sync` → 编译错误 |
| C 宏 / 模板黑盒 | `//LCMD_`：L 写给 L，注入 AST；`uid_counter()` 生成唯一 ID |
| `#error` / 手写静态断言散落 | **`comptime { }`** + `compile_error` / `comptime_assert`（前端剥干净） |
| YAML 再解析一遍 | **源码即配置** |
| `null` 到处飞 | 只有 `Optional<T>` / `Result<T,E>` |

更多可运行片段：[`examples/all_samples.l`](examples/all_samples.l)

---

## `const` 与 `constd`

不可变性分两档，对应「编译期就能定」和「运行期算一次就锁死」——**不要靠命名习惯区分**。

| | `const` | `constd` |
| --- | --- | --- |
| 何时求值 | **编译期** | **运行期**（初始化一次） |
| 之后能否改 | 不能 | 不能（永久锁定） |
| 底层 | 直接内联为字面量，**无运行时存储** | 有真实绑定；初始化后禁止再 `=` |
| 典型用途 | 上限、魔数、尺寸表 | 读文件大小、取启动时间、依赖运行时的配置快照 |
| 作用域 | 模块级常量 | 模块级（及字段级动态常量） |

```l
const MAX_SIZE = 10              // 编译期常量，用点即内联

constd GLOBAL_VERSION = 42       // 运行期初始化后锁定
constd start_time = get_current_time()
constd size = get_file_size("data.bin")

i32 global_count = 0             // 可变全局：必须显式写类型（不能靠 :=）
```

### 和 `:=` 的边界（别混）

- **`:=`**：只用于**函数内局部**推导绑定；默认不可变，要改写 `name! :=`。  
- **`constd`**：模块级 / 字段级的「运行期动态常量」写法——**不是** `:=` 的语法糖。  
- 模块顶层：用 `const` / `constd` / 显式类型（可变如 `i32 x = 0`）。顶层 `:=` 若出现，语义上按不可变的 `constd` 理解，但推荐直接写清 `const`/`constd`。  
- 不可变绑定默认可跨线程只读共享，少一层「要不要加锁」的心智税。

```l
fun main() -> i32 {
    n := MAX_SIZE                // 局部推导；读的是编译期常量
    print(n)
    print(GLOBAL_VERSION)        // constd：运行期已锁，只能读
    // GLOBAL_VERSION = 99       // 错误：constd 禁止再赋值
    return 0
}
```

---

## 属性写法（Property）

这是 L 最“有辨识度”的表面语法之一：**你永远不写尾下划线**；物理字段名由编译器在底层自动加 `_`。

### 声明

类里 `Type name`（无 `fun`、无 `()`）就是物理字段。不要写 `var`，也不要手写 `age_`。

```l
class User {
    i32 age              // 底层 LLVM 字段：age_
    pub String name      // pub → 允许无 accessor 时直接读写
    fun init(i32 a, String n) {
        age = a          // 类内部：直接碰物理字段
        name = n
    }
}
```

源码里写 `u.age_` → **编译错误**（长度 > 1 的标识符禁止尾 `_`；单独的 `_` 只留给 `match` 通配）。

### 访问优先级

对 `u.age` / `u.age = v`（无括号）：

1. **先找** `get_age()` / `set_age()` → 降成方法调用  
2. **否则**，若存在 **`pub`** 物理字段 `age_` → 降成 GEP 读写  
3. 都没有，或字段非 `pub` → 报错（私有字段不能靠属性名偷渡）

```l
class User {
    i32 score
    pub fun get_score() -> i32 { return score }
    pub fun set_score(i32 v) { score = v + 1 }   // 赋值走 setter

    pub i32 level                                // 无 accessor → 直接读写 level_
}

fun main() -> i32 {
    u := User()
    u.score = 10        // → set_score(10) → 内部存 11
    print(u.score)      // → get_score()
    u.level = 99        // → 物理字段
    print(u.level)
    return 0
}
```

带括号的是方法：`u.get_age()` 永远是直接调用，不走属性规则。

### 相对赋值（配置友好）

```l
app.rs232 {
    .baud = 115200
    .parity = "none"
}
```

前端降成 `app.rs232.baud = 115200`，再走同一套属性优先级——适合强类型、零解析成本的配置文件。

---

## 隐式委托（delegate）

内嵌一个组件，不必手写转发方法：`delegate TypeName` 声明一个**以类型名命名**的匿名字段，外层类自动获得它的全部方法。

```l
class Engine {
    pub fun start() { print("Vroom!") }
}

class Car {
    delegate Engine          // 匿名字段 engine_；不写 getter 也不写转发
}

fun main() -> i32 {
    c := Car()
    c.start()                // → Engine.start()，隐性 this 指向 car.engine_
    return 0
}
```

- **方法提升**：`c.start()` 在本类找不到时，依次查所有 `delegate` 字段的方法。
- **冲突**：多个 delegate 提供同名方法 → **编译错误**，必须显式消歧：`c.Engine.start()` / `c.Motor.start()`。
- **同类型铁律**：一个类**禁止**委派两个及以上同类型对象（如两个 `delegate Engine`）。因为字段名就是类型名，`c.Engine.start()` 无法消歧，所以这是**致命编译错误**：

  ```
  [ERROR] file:line:col - Multiple delegates of the same type 'Engine' are forbidden. Use a named field instead.
  ```

  分部类（`partial class`）也受同样约束：若两个分部各写了一个 `delegate Engine`，**合并阶段**同样报此错。确实需要两个实例时，请改用命名字段（`Engine helper_a` / `Engine helper_b`）。
- **隐性 this**：提升调用传入的是内嵌字段的地址（GEP），不是外层对象——组件方法里的 `this` 指向自己。
- 与「无继承」并不矛盾：这是**组合 + 语法转发**，内存布局仍是扁平结构体，没有任何 vptr。

---

## 关键字参数（Keyword Arguments）

调用时可以写 `形参名: 值`，少记位置、少造重载——**可读性换心智负担**。

### 规则

1. **位置参数必须在前**，关键字参数在后。  
2. 关键字之间**可以乱序**；Codegen 会按函数签名顺序重排。  
3. **禁止**在关键字之后再跟裸位置参数（解析期直接拒绝）。  
4. 关键字名必须是真实形参名；与签名对不上会报错。

```l
fun create_user(String name, i32 age, String role) {
    print(name)
    print(age)
    print(role)
}

fun main() -> i32 {
    // 位置 + 关键字混用；关键字不必按声明顺序
    create_user("Alice", role: "admin", age: 20)

    // 也可以全关键字
    create_user(name: "Bob", age: 18, role: "user")

    // create_user("Bob", age: 20, "user")  // 错误：关键字后面不能再跟位置参数
    return 0
}
```

这和属性写法同一哲学：表面少废话，底层仍是普通强类型调用——没有运行时「按名查参」的动态税。

---

## 元编程（LCMD / Hook / comptime）

L 没有文本宏。元编程跑在**前端**：树行走解释器执行 L 回调，返回源码字符串 → 再 parse → **注入当前作用域**。降 LLVM 之前全部结束，**运行时零成本**。

### 指令：`//LCMD_name(args)`

```l
pub fun __compile_time_handler(String path, String cmd, String args) -> String {
    if cmd == "make_answer" {
        return "i32 the_answer = 42;"   // 同一行多语句要用 ;
    }
    return ""
}

fun main() -> i32 {
    //LCMD_make_answer()
    print(the_answer)                  // 注入后的符号
    return 0
}
```

### 层级上下文路径

回调拿到完整路径（如 `User.generate`）。在类体里注入的节点会成为**该类成员**：

```l
pub fun __compile_time_handler(String path, String cmd, String args) -> String {
    if path == "User.generate" && cmd == "make_hello" {
        return "pub fun say_hello() -> i32 { return 42; }"
    }
    return ""
}

class User {
    fun generate() {
        //LCMD_make_hello()
    }
}
```

### 按需 Hook（默认关闭）

不会对每个 AST 节点盲触发。类/函数 Hook 必须显式打开，否则解释器整条短路：

```l
pub fun __on_class_parsed(String className) -> String {
    if className == "UserAuto" {
        return "pub fun print_auto_log() -> i32 { return 888; }"
    }
    return ""
}

//LCMD_enable_class_hook()
class UserAuto { i32 id }
//LCMD_disable_class_hook()
class ShouldNotTrigger { i32 x }   // 不再触发
```

### 源码位置与 `uid_counter()`（编译期专用）

Lexer 为每个 Token / AST 节点记录精确的 **行号、列号**。触发 LCMD 时，这些位置进入前端解释器上下文：

- 内建 **`source_line()` / `source_column()`** → 当前指令处的行列（`i64`）  
- 也可把 handler 写成 5 参：`(path, cmd, args, line, column)`

**`uid_counter() -> i64`**：编译器内全局单调计数器；每次在 LCMD / `comptime` 里调用，返回当前值再 `+1`，结果**内联为常量**——**绝不**生成运行时 LLVM 调用，也无共享可变状态，避免并发错乱。

```l
pub fun __compile_time_handler(String path, String cmd, String args) -> String {
    if cmd == "gen_var" {
        id := uid_counter()
        return "i32 temp_" + id + " = 100;"   // → temp_0, temp_1, …
    }
    return ""
}

fun main() -> i32 {
    //LCMD_gen_var()
    //LCMD_gen_var()
    print(temp_0 + temp_1)   // 200
    return 0
}
```

**严格拦截**：在普通函数（如 `main`）里写 `uid_counter()` → 编译失败：

`[ERROR] file:line:col - uid_counter() is only allowed in compile-time context`

### `comptime { ... }` 怎么用

和 `//LCMD_` 的分工：**LCMD 负责生成/注入代码**；**`comptime` 负责编译期跑逻辑、检查、否决**。块在前端执行一遍后整段剥掉——**零 LLVM、零机器码**。

```l
const ENABLE_EXPERIMENTAL = false   // 改成 true → 下面 assert 失败，整树编不过

// 1) 编译期检查：失败则根本编不过
comptime {
    comptime_assert(ENABLE_EXPERIMENTAL == false,
        "experimental build is locked; flip ENABLE_EXPERIMENTAL only intentionally")
}

// 2) 无条件否决（例如禁编某平台/某配置）
comptime {
    // compile_error("Build forbidden on this tree.")
}

// 3) 自省 + 编译期唯一 ID（同样只在 comptime / LCMD 合法）
comptime {
    id := uid_counter()
    // 调试宏展开时看得到；进不了最终二进制
    print("comptime in " + __FILE__ + " @" + __LINE__ + " uid=" + id)
}

fun main() -> i32 {
    print("runtime: " + __FILE__ + ":" + __LINE__ + " in " + __FUNC__)
    return 0
}
```

**自省常量**（任意 L 表达式可用，含普通函数 / `comptime` / LCMD）：

| 常量 | 类型 | 含义 |
| --- | --- | --- |
| `__FILE__` | `String` | 当前 Token 所在源文件（虚拟名如 `<eval>` 亦可） |
| `__LINE__` | `i64` | 当前 Token 行号 |
| `__FUNC__` | `String` | 当前函数名（`comptime` 块内为 `<comptime>`） |

**`compile_error(msg)` / `comptime_assert(cond, msg)`** 只允许出现在 `comptime` / LCMD 上下文。失败时报：

```text
[ERROR] tests/cases/comptime_fail_test.l:2:5 - Compile-time error: Build forbidden: This is a forced compile error.
```

在 `main` 里写 `compile_error(...)` / `uid_counter()` → 同样被拒：`… is only allowed in compile-time context`。

### 边界

| 可以 | 不可以 |
| --- | --- |
| 返回常量、生成声明/方法字符串并注入 | 运行时 I/O、调外部 C DLL |
| `comptime { ... }` 前端求值后剥离 | 把 comptime/LCMD 降到 LLVM IR |
| `compile_error` / `comptime_assert` 否决非法配置 | 在普通运行时代码里调用它们 |
| `uid_counter()` / `source_line()` / `source_column()` 仅在编译期上下文 | 在 `main` 等运行时代码里调用 `uid_counter()` |
| handler 可写成 5 参 `(path, cmd, args, line, column)` | — |
| `gen-interface` 剥实现、只留 `pub` 签名 | 把私有/`extern`/LCMD 泄漏进 `.li` |
| `l doc` 从 `pub` + `#`/`//` 注释出 Markdown | 把 `//LCMD_` 或私有实现写进文档 |

---

## 工程实践

### 仓库分层

| 目录 | 职责 |
| --- | --- |
| `compiler/` | C++ 前端 + LLVM Codegen |
| `runtime/` | **纯 C** 系统调用（分配、线程、Socket、TLS、文件…） |
| `stdlib/` | **纯 L** 业务与容器；C 绑定只出现在 `extern module` |
| `tests/` | Ruby runner + `.l` 用例 |
| `examples/` | 示例；总览见 `all_samples.l` |

纪律：**Codegen 禁止硬编码** `l_print` / `l_alloc` 一类符号——一律由 `stdlib/memory.l`、`io.l` 等 `extern module` 声明，链接名保持原样。

### 模块与包

- 文件即模块：`import "foo.l"`；IR 符号扁平为 `@module_func`。  
- 多文件：`l build -o app.exe a.l b.l …` 合成**一个** LLVM Module；`import` 有模块缓存，避免重复解析。  
- 依赖：`l.toml` 的 `[dependencies]` → 扫 `vendor/<name>/`（`.li` + `lib*.a`）；环依赖直接报错。

### 对外导出（C ABI）

```l
pub fun add_numbers(i32 a, i32 b) -> i32 { return a + b }

pub fun counter_create(i32 v) -> ptr {
    c := heap Counter(v)
    return c as ptr          // 所有权交给外部；移出 auto-free
}

pub fun counter_destroy(ptr p) { l_free(p) }
```

```bash
./l build --shared tests/cases/ffi_export.l -o libl_ffi.dll
./l gen-interface stdlib/string.l -o string.li
./l doc stdlib/string.l -o string.md
```

`Class as ptr` / `ptr as Class` 是零成本 bitcast。跨语言回调函数必须 `CallingConv::C`。

**`l doc`**：扫 AST 里的 `pub fun` / `pub class` / `pub interface`，把签名写成 Markdown；声明正上方的 `#` / `//` 注释当作说明（`//LCMD_` 忽略）。适合给 stdlib 或对外 API 出一页可读文档，和 `gen-interface`（给机器看的 `.li`）成对。

### 错误与空值纪律

- API 查找 → `Optional<T>`（`Map.get` 等）  
- 可恢复失败 → `Result<T,E>` + `?` 早归  
- 禁止隐式 null；配置缺失用 `Optional`，类型不对用 `Result`，不要静默默认值掩盖错误

### 并发与测试习惯

- 共享可变状态：`sync { ... }`；锁次数可用 runtime 计数校验（不只看最终数值）。  
- 全量：**123 条**自动化用例 — `ruby tests/run_tests.rb`（Lexer→AST→可执行/JIT，含拒绝用例）。  
- 性能：`benchmarks/`，固定 `-O3`，**不**并进上述套件。  
- 探索：`l repl` / `l eval`（ORC JIT，不落临时 exe）。

### 内存习惯速查

| 场景 | 做法 |
| --- | --- |
| 普通堆对象 | `heap T(...)`，靠 auto-free 或 `defer l_free` |
| 返回给 FFI | `as ptr` 前确保移出 auto-free，并提供 `destroy` |
| 短生命周期批量分配 | `arena`；返回值要拷出池外 |
| 查泄漏 | `l_get_alloc_count()` |

---

## 其它核心能力（速览）

**编译型** — 整条链路是 Lexer → 类型检查 → LLVM IR → 机器码；`repl`/`eval` 也是 JIT 编译，不是逐行解释。

**强类型** — 形参、字段、返回值均有类型；`:=` 只是推断，推断结果仍是静态类型。泛型必须带约束（`<T: Interface>`），单态化生成专用机器码。`i32 < i64 < f64` 可提升，`String`/类与数字之间禁止隐式混算；`i64` 赋值不会默默截成 `i32`。

**内存** — `heap` / `defer` / 作用域 `l_free`；临时串与容器递归清理；Arena 整块释放。

**对象** — 类是纯数据；`Array<IReader>` 存胖指针；`r as Point` → `Optional`（比 TypeID）。

**并发** — `spawn` = OS 线程；写 `!` 必须 `sync`；原子 API 可经 `&` 豁免检查。

**错误** — 无异常表；`Result` + `?` 冲刷 defer 后早归。

**双模式** — 同为编译产物：AOT（`build`/`run`/`--shared`）与 JIT（`repl`/`eval`）。

```l
count! := 0
count! = count + 1
arr := [1, 2, 3]
arr.each { |x| print(x) }
```

---

## 架构

```
.l ──► Lexer/ASI ──► Parser/AST ──► 语义（单态化 / 逃逸 / 竞争 / LCMD）
                                      │
                                      ▼
                               LLVM IR ──► AOT | ORC JIT
                                      ▲
              stdlib/*.l (纯 L)  +  runtime/*.c (纯 C ABI)
```

```bash
ruby tests/run_tests.rb    # 123 automated tests — must stay green
```

---

## 快速开始

**依赖**：CMake 3.20+ · LLVM 22.1.0 · C++17

```powershell
. .\tools\bin\use-llvm-env.ps1   # Windows：VS + LLVM
```

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release

./l run tests/cases/hello_world.l
./l build --shared tests/cases/ffi_export.l -o libl_ffi.dll
./l gen-interface stdlib/string.l -o string.li
./l doc stdlib/string.l -o string.md
./l repl
```

---

少写一行废话，就少一分心智负担。**123 tests green.** Built with LLVM, discipline, and AI pair-programming.

---

## 版权声明

**Copyright (c) 2026 DFTan. All rights reserved.**  
版权所有 · 保留一切权利。开源许可证尚未确定（**License TBD**）。

未经书面授权，不得复制、修改、分发或商用。完整条款见 [`COPYRIGHT`](COPYRIGHT)。
