# L · Loong (龙语言)

**A modern, ultra-minimal, high-performance system programming language built on LLVM.**

![Build Passing](https://img.shields.io/badge/build-passing-brightgreen)
![LLVM](https://img.shields.io/badge/LLVM-22.1.0-blue)
![Tests](https://img.shields.io/badge/tests-148%20cases-success)

> Ship the few lines the business actually needs. Leave the ceremony, the boilerplate
> and the memorisation tax to the compiler.

---

## Table of Contents

| # | Feature | Can you see it run? |
| --- | --- | --- |
| 1 | [Ultra-Minimal (`?.` / `??`)](#1-ultra-minimal--safe-navigation-and-null-coalescing) | [`examples/http_parse.l`](examples/http_parse.l) · [`tests/cases/coalesce_test.l`](tests/cases/coalesce_test.l) |
| 2 | [Implicit Delegation (`delegate`)](#2-implicit-delegation--delegate) | [`examples/plugin_framework.l`](examples/plugin_framework.l) · [`tests/cases/delegate_test.l`](tests/cases/delegate_test.l) |
| 3 | [Implicit Trailing Underscore](#3-implicit-trailing-underscore) | [`tests/cases/field_syntax_test.l`](tests/cases/field_syntax_test.l) |
| 4 | [Code-as-Config (`CPropertys` / Typed Config)](#4-code-as-config--cpropertys--typed-config) | [`examples/typed_config.l`](examples/typed_config.l) · [`tests/cases/typed_cfg.l`](tests/cases/typed_cfg.l) |
| 5 | [Metaprogramming (`LCMD` / `comptime`)](#5-metaprogramming--lcmd-and-comptime) | [`tests/cases/lcmd_path.l`](tests/cases/lcmd_path.l) · [`tests/cases/lcmd_l_callback.l`](tests/cases/lcmd_l_callback.l) |
| 6 | [Enterprise-Grade Engineering](#6-enterprise-grade-engineering) | [`tests/run_tests.rb`](tests/run_tests.rb) · [`compiler/`](compiler) |

---

## 1. Ultra-Minimal — Safe Navigation and Null Coalescing

No implicit nulls. Ever. Absence is an `Optional<T>` you have to unwrap — and
unwrapping is two characters, not an `if` pyramid.

| Without L | With L |
| --- | --- |
| `if (opt != null) { if (opt.x != null) opt.x.y }` | `opt?.x?.y` |
| `if (opt.is_some()) opt.unwrap() else 0` | `opt ?? 0` |

```l
val := opt ?? 42              // missing → 42, left side evaluated exactly once
name := empty_name ?? "guest"
role := Optional<String>(true, "admin") ?? "guest"
```

The left operand of `??` must be `Optional<T>`; the whole expression yields a plain
`T`. `?.` short-circuits an entire chain and still hands you back an `Optional`, so a
single `??` at the end covers the whole path.

**Real source:** [`tests/cases/coalesce_test.l`](tests/cases/coalesce_test.l) ·
[`examples/http_parse.l`](examples/http_parse.l) (`header_text` strips two `Optional`
layers with `props.get(name) ?? String("")`)

---

## 2. Implicit Delegation — `delegate`

No inheritance, no vtable hidden inside every object. Composition is the only
structural reuse mechanism — and it is promoted transparently.

```l
class Engine {
    fun start() {
        print("Vroom!")
    }
}

class Car {
    delegate Engine          // anonymous embedded field
}

fun main() -> i32 {
    c := Car()
    c.start()                // resolved on Engine, `this` = the embedded field
    return 0
}
```

Method promotion is only the surface: the hidden `this` passed to `Engine.start` is the
GEP address of the embedded field, so the receiver sees the right memory. Ambiguous
names across two delegates are a compile error, disambiguated with
`obj.EmbeddedType.method()`.

**Real source:** [`tests/cases/delegate_test.l`](tests/cases/delegate_test.l) ·
[`examples/plugin_framework.l`](examples/plugin_framework.l) (`App` delegates
`CPropertys`, so `set` / `get` become `App` methods for free)

---

## 3. Implicit Trailing Underscore

You never write `age_`. You declare `i32 age`; the compiler emits `age_` in memory.

```l
class User {
    i32 age                  // physical field is age_ in LLVM IR
    String name

    fun init(i32 a, String n) {
        age = a              // inside the class: direct field access
        name = n
    }

    pub fun get_age() -> i32 {
        print("Accessor called")
        return age
    }
}

class Plain {
    pub i32 score
}

fun main() -> i32 {
    u := User(20, "L")
    print(u.age)             // accessor wins → "Accessor called", 20
    p := Plain()
    p.score = 99             // no accessor, field is pub → raw GEP store
    // u.age_ = 30           // compile error: trailing '_' is banned
    return 0
}
```

Resolution order for `u.age`: `get_age()` / `set_age()` first, then the `pub` physical
field, then a compile error. One spelling for the reader, one layout for the machine.

**Real source:** [`tests/cases/field_syntax_test.l`](tests/cases/field_syntax_test.l)

---

## 4. Code-as-Config — `CPropertys` / Typed Config

Configuration is L source, not YAML reinterpreted at runtime. Nested blocks rebind
`this` to the section's type, so a bare `port = 5432` lands on `Database.port`.

```l
import "stdlib/properties.l"

pub class Database {
    i32 port
    String host
    i32 pool_size
}

pub class Server {
    Database db
    String name
}

pub class App {
    Server server
    String name
}

fun app(fun() body)          // makes `app(...) { ... }` a config block

fun main() -> i32 {
    cfg := app(type: App) {
        server {
            db {
                port = 5432
                host = "127.0.0.1"
            }
        }
        name = "svc"
    }
    print(cfg.server.db.port)      // 5432
    print(cfg.server.db.host)      // 127.0.0.1
    return 0
}
```

Layered on top of the same container, `CPropertys` gives you a dynamic tree with dotted
paths (`app.set("db.pool.max", 16)`) and a full nested JSON parser / serialiser built
entirely in L ([`stdlib/json.l`](stdlib/json.l)).

**Real source:** [`tests/cases/typed_cfg.l`](tests/cases/typed_cfg.l) ·
[`examples/typed_config.l`](examples/typed_config.l) · [`stdlib/properties.l`](stdlib/properties.l)

---

## 5. Metaprogramming — `LCMD` and `comptime`

No second macro language. Directives are L functions returning L source, injected into
the AST at the exact node they annotate.

```l
// The handler receives the hierarchical path, the command name and its args.
pub fun __compile_time_handler(String path, String cmd, String args) -> String {
    if path == "User.generate" && cmd == "make_hello" {
        return "pub fun say_hello() -> i32 { return 42; }"
    }
    return ""
}

class User {
    i32 id
    fun generate() {
        //LCMD_make_hello()       // injects say_hello() into User
    }
}

fun main() -> i32 {
    u := User()
    print(u.say_hello())          // 42
    return 0
}
```

`comptime { ... }` runs in the frontend and is stripped before codegen — zero runtime
cost (`compile_error` / `comptime_assert` veto the build outright). `uid_counter()`
hands a monotonic `i64` to generated code so injected symbols never collide.

**Real source:** [`tests/cases/lcmd_path.l`](tests/cases/lcmd_path.l) ·
[`tests/cases/lcmd_l_callback.l`](tests/cases/lcmd_l_callback.l)

---

## 6. Enterprise-Grade Engineering

The language is not a toy: 148 end-to-end cases gate every commit, covering lexer,
parser, codegen, concurrency, FFI, metaprogramming and the doc generator.

```console
$ ruby tests/run_tests.rb              # incremental: skips unchanged PASS cases
$ ruby tests/run_tests.rb --filter=json
$ ruby tests/run_tests.rb --all        # full sweep
```

| Layer | Location |
| --- | --- |
| Lexer / Parser / AST | [`compiler/lexer/`](compiler/lexer) · [`compiler/parser/`](compiler/parser) · [`compiler/ast/`](compiler/ast) |
| Semantics & diagnostics | [`compiler/semantic/`](compiler/semantic) · [`compiler/diagnostic/`](compiler/diagnostic) |
| LLVM codegen + JIT | [`compiler/codegen/`](compiler/codegen) · [`compiler/jit/`](compiler/jit) |
| Module / package manager | [`compiler/module/`](compiler/module) · [`compiler/package/`](compiler/package) |
| C runtime (pure C) | [`runtime/`](runtime) |
| Standard library (pure L) | [`stdlib/`](stdlib) |
| Test runner (Ruby) | [`tests/run_tests.rb`](tests/run_tests.rb) |
| Benchmarks (`-O3`) | [`benchmarks/`](benchmarks) · [`benchmarks/run_bench.rb`](benchmarks/run_bench.rb) |

**Real source:** [`tests/run_tests.rb`](tests/run_tests.rb) · [`compiler/`](compiler)

---

## Why L? (The Philosophy)

Most languages ask you to pay a tax before the first useful line is written: semicolons,
`let`/`var`/`mut`, trailing underscores leaking out of the memory layout, getter
boilerplate, YAML re-parsed into an untyped map at 3 a.m., a second macro language with
its own grammar, and a `null` that can appear anywhere a value was promised.

L removes the tax instead of documenting it.

1. **Ultra-minimal absence handling.** `?.` and `??` make the empty case the short one.
   There is no implicit null: if something can be missing, the type says so, and
   unwrapping costs two characters, not an `if` pyramid.
2. **Composition without inheritance.** `delegate` promotes an embedded type's methods
   onto its owner, with the receiver pointed at the embedded field. Behaviour reuse
   without object headers full of vtable pointers.
3. **One spelling per concept.** You declare `i32 age`; the compiler owns `age_`.
   Accessors take priority, `pub` fields fall back to raw memory, and a trailing
   underscore in user code is a compile error.
4. **Configuration is code.** Typed config blocks rebind `this` to the nested section,
   so a flat `port = 5432` writes into `Database.port`. Your configuration gets checked
   by the same compiler that checks your program.
5. **Metaprogramming in one language.** `//LCMD_` directives call L functions that
   return L source injected into the AST; `comptime` executes in the frontend and is
   stripped before codegen. No runtime cost, no foreign macro dialect.
6. **Engineering that holds.** 148 end-to-end Ruby cases, incremental execution,
   separate `-O3` benchmarks, LLVM 22.1.0 codegen with ORC JIT for `--eval` and `--repl`.

L is compiled and statically, strongly typed. `:=` only lets you omit a type name the
compiler already knows — it never turns the language dynamic. Short like a script, fast
like C, checked at compile time.

---

## Quick start

```console
$ l -o hello.exe hello.l          # AOT native executable
$ l run hello.l                   # compile and run
$ l --eval 'print("hi")'          # ORC JIT, no temporary .exe
$ l --repl                        # interactive `L> `
```

```l
fun main() -> i32 {
    print("Hello, L")
    return 0
}
```

A wider tour lives in [`examples/all_samples.l`](examples/all_samples.l).
