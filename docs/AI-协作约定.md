# AI 协作约定（跨会话恢复用）

> 新会话开始处理本仓库时，先读本文件，按「当前状态」继续，不要重新协商约定。

## 1. 指令流程（Axxx）

1. **我生成指令**：在对话中输出 `[Axxx]` 指令全文，编号从 `[A001]` 递增，先给用户审核。
2. **用户确认**：用户说「执行 Axxx」后，我才把指令原文写入 `.agent-instructions/Axxx.md`（永久保留，用于追溯，不删除）并立即执行。
3. **执行完出报告**：执行结束把报告写入 `.agent-reports/Axxx.md`，同时在对话里回显。
4. **自动分析**：报告之后直接给出分析结论与下一条指令草案，不需要用户再说「分析 Axxx」。
5. **「分析 Axxx」的含义**：仅用于回看旧编号，或上下文丢失后重新读取报告。

编号规则：指令在*展示*时就已定号，但未落盘。若用户否决，该编号作废不占用。

## 2. 目录与文件

- `.agent-instructions/Axxx.md` — 指令原文，永不删除
- `.agent-reports/Axxx.md` — 执行报告，与指令编号一一对应
- 两者均在 `.gitignore` 中（`.agent-instructions/`、`.agent-reports/`）
- 不使用 Cursor hooks：单窗口交互即可，曾建过 `sessionStart` 钩子，已按用户要求删除

## 3. 报告模板

```text
# [Axxx] 执行报告

## 1. 指令摘要
## 2. 实际改动（文件 + 改动内容表）
## 3. 关键决策（含取舍理由）
## 4. 执行过的命令与结果（表格，失败也照实写）
## 5. 达到目标？（达成 / 部分达成 / 未达成 + 依据）
## 6. 遗留问题与风险
## 7. 下一步建议
```

报告只写实际发生的事实，不写推测。

## 4. 轮次报告格式（项目规则要求）

- 每条回复末尾输出 Turn Change Report，整块包在一个 ```text 代码块里
- 分 Tier 1（摘要）/ Tier 2（细节），最后一行给状态标记：
  - `👉 [STATUS] PASS`
  - `🚨 [STATUS] HUMAN ACTION REQUIRED`
  - `❌ [STATUS] FAIL`
- 提示带 `[Axxx]` 时报告首行为 `=== [Axxx] Turn Change Report ===`，否则 `=== [MANUAL] Turn Change Report ===`

## 5. 构建

```powershell
cd d:\Projects\Vendors\DFTan\L
. .\tools\bin\use-llvm-env.ps1
Invoke-LlvmEnv "cmake --build build/clang-debug"
```

直接 `cmake --build` 会因缺 `crtdbg.h` 失败（未走 vcvars），这是环境已知问题。

## 6. 测试

- 只跑相关过滤用例：`ruby tests/run_tests.rb --filter=<keyword> --all`
- **绝不自行跑全量套件**（项目规则 100 / 106）
- 手动调试小脚本：`ruby tools/run_l.rb <file.l>`（会自动补 stdlib）

## 7. 硬性禁令

- 不修改 `.cursor/rules/*.mdc`（规则 999.1）；发现需要改动时只输出补丁块让用户手动粘贴
- 不自动 commit / push
- L 代码风格遵循规则 111：布尔判断优先 `!expr`；非布尔常量放左侧；`!= false` / `!= true` 一律改写为 `!X`

## 8. 当前状态（截至 A007）

### 已完成

| 编号 | 内容 | 结果 |
|---|---|---|
| A001 | 按规则 111 统一 `== false` / `== true` 为 `!expr`（52 处，11 文件） | 达成 |
| A002 | 排查「bool 局部变量真值随后续代码变化」 | 前提不成立，无缺陷 |
| A003 | 处理剩余 4 处：`config_layered_test.l` 两处 `!=`、`stdlib/map.l` 两处 `false == X` | 达成 |
| A004 | 修复 `run_l.rb` 的 stdlib 自动检测漏判字符串字面量 | 达成 |
| A005 | `inferTypeFromExpr` 的 StringLiteral `ptr` 回退改为明确报错 | 达成 |
| A006 | 修正 `cannot infer type` 诊断位置（改用 `decl.loc`） | 达成 |
| A007 | 删除无位置 `error` 重载 | **撤销**（需改 231 处，用户决定不做） |

A007 撤销后已验证：构建通过，`string` 80 / `bad` 18 / `reject` 14 全部通过（112 用例，0 失败）。`codegen.cpp` 中只剩 A005 与 A006 的改动。

### A002 的重要更正

A001 报告第 6.1 节说发现「codegen 缺陷」是**错的**。真实原因是 A001 把 `if X != false { fail }` 误改成 `if !X { fail }`，断言方向反了。以 A002 报告为准。

### 当前编译器代码改动（相对 HEAD）

- `compiler/codegen/codegen.cpp`：
  - A005：`inferTypeFromExpr` 的 StringLiteral 分支，String 类型不可用时 `error(...)` 并返回 `{}`，不再返回 `"ptr"`
  - A006：局部 `:=` 路径两处 `error(...)` 补上 `decl.loc`

### 当前测试文件改动

- `tests/cases/string_infer_test.l`（A004 新增，正向）
- `tests/cases/string_literal_no_stdlib_bad.l`（A005 新增，负向）
- `tests/run_tests.rb`：新增 `STRING_INFER_TEST`、`STRING_NO_STDLIB_BAD` 常量与两个测试方法及注册
- A001 / A003 改写的 11 个文件的 `!expr` 形式

### 已知遗留

1. `defaultLoc()` 机制仍在：未来新增的无 loc `error()` 仍会显示「当前模块:1:1」。A007 已评估，改动面 231 处，暂不做。
2. `inferTypeFromExpr` 对未解析 MethodCall 仍回退到 `i32`（`codegen.cpp` 约 11529 行注释处），未处理。
3. `stdlib/map.l` 的规律：任何 `while` / `if` 条件改写都要跑 `map` 相关 23 个用例回归。
4. 规则 111 的布尔与非布尔规则已明确，但涉及 `String(...)` 构造调用的比较仍需逐处人工判断，不能正则批量替换。

## 9. 分析回答风格

- 报告/分析先给结论，再给依据
- 发现问题如实说明，包括「我的判断错了」这类结论
- 不猜测：给出结论前用工具核实（读代码、编译、实测）
