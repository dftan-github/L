# SEditor

用 L 写的源码树编辑器骨架。目标是把一个源文件拆成**有序**的节点树：
类/函数是分类节点，其余每一行是行节点；把子项按顺序拼回去，必须**逐字节**
等于原文件。

## 为什么是 tag 而不是 Map

`stdlib/properties.l` 的 `CPropertys` 用 `Map<ptr>` 存子节点，而 `Map` 是开放
寻址哈希表，`property_keys()` 的注释明确写着 **order undefined**。拼接依赖顺序，
所以这里用平坦的堆数组（`Array<ptr>` / `Array<i32>`）——顺序是数据结构本身的一部分。

## 文件

| 文件 | 作用 |
| --- | --- |
| `strtab.l` | 全局字符串表。同一行只存一份，节点只存 i32 tag |
| `tree.l` | `Node` 定义、子节点/行 tag 的追加、`join()`、`free_tree()` |
| `parser.l` | 按行切分 + 列 0 声明开分类 + `classify()` |
| `row.l` | 大纲行（缩进层级 + 标签 + 行号 + 节点句柄 + 折叠状态） |
| `ui.l` | 目录递归扫描、`build_dir_tree()`、GDI 大纲面板 + 源码面板 |
| `main.l` | 窗口 + 事件循环 + 绘制 + 大纲点击折叠 |
| `check.l` | 只做 round-trip 断言（文件或目录），不开窗口 |

## 树形约定

- 只有**列 0** 的 `class` / `interface` / `fun` / `delegate` 声明会开新分类。
- 其余行（含缩进注释）作为行 tag 挂在当前分类下。
- 根节点 `entry_tag` 存的是**整个文件**（供显示），`join()` 会跳过它，
  只让分类节点贡献自己的那一行。
- 拼接顺序：`行 tag → 子节点 entry → 子节点递归`，与 `build_text` 赋值顺序一致。
- 文件末尾是否有 `\n` 由 `build_into` 记在根节点的 `ends_nl` 上，`join()` 读它
  决定是否剪掉多写的那一个字节。**不要**再从存下来的行去猜——CRLF 文件的
  最后一行留着 `\r`，`split` 也不保证末尾一定有空元素，猜错过好几次。

## 打开目录

`seditor.exe <目录>` 会递归扫描该目录下所有 `*.l`，每个文件成为一个顶层分类
节点，展开即为该文件的树。点大纲行可折叠/展开（再次展开会清掉子节点的折叠
状态）。递归深度上限 16，跳过不可读的目录。

## 构建

```bat
:: 仓库根目录（自动加载 MSVC/SDK 环境）
build_env.bat app apps\seditor

:: 只跑 round-trip
apps\seditor\seditor_check.exe .            :: 整个仓库
apps\seditor\seditor_check.exe examples     :: 单个目录（递归）
apps\seditor\seditor_check.exe stdlib\map.l :: 单个文件

:: 打开
apps\seditor\seditor.exe apps\seditor
apps\seditor\seditor.exe stdlib\map.l
```

`apps\seditor\build.bat` 仍可用，但它指向 `build\compiler\l.exe`（旧路径），
推荐改用根目录的 `build_env.bat`。

## 输入能力（runtime/win32.c）

输入是完整的一套，绘制只够用：

- 键盘：`WM_KEYDOWN/UP` + `WM_CHAR` + `WM_UNICHAR`，含 auto-repeat 标记
- 鼠标：移动 / 三键按下抬起 / 滚轮（滚轮坐标已转客户端空间）
- IME：`WM_IME_COMPOSITION` 的 `GCS_COMPSTR`（拼写中）与 `GCS_RESULTSTR`（提交）
- 修饰键：`GetAsyncKeyState` 实时查询
- 窗口：`WM_SIZE` / `WM_DPICHANGED` / `WM_CLOSE`

绘制只有：清屏、矩形、横竖线、裁剪栈、字体、UTF-8 文本、文本宽度、行高。
双缓冲 `WM_PAINT`，调用方永远不接触 `HDC` 生命周期。

## 尚未实现

- 光标 / 选区 / 编辑（当前只读）
- 语法高亮（`classify()` 已分类，尚未按类别上色）
- 大纲点击跳转
- 源码面板一次只显示拼接后的整棵树；目录模式下列表很长，需要按需拼某个文件
