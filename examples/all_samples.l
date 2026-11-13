//这里面包含114个例子

//------------------------------------------------
// examples/config_parser.l
//------------------------------------------------
// Simple #comment / key = value config parser (ENV / TOML-lite).
// Ignores blank lines and full-line comments; trims keys and values.

fun is_space(i32 c) -> bool {
    return c == 32 || c == 9 || c == 13
}

fun trim(String s) -> String {
    n := s.length()
    start! := 0
    while start < n && is_space(s.char_at(start)) {
        start! = start + 1
    }
    end! := n
    while end > start && is_space(s.char_at(end - 1)) {
        end! = end - 1
    }
    return s.slice(start, end - start)
}

fun index_of_char(String s, i32 ch) -> i32 {
    n := s.length()
    i! := 0
    while i < n {
        if s.char_at(i) == ch {
            return i
        }
        i! = i + 1
    }
    return 0 - 1
}

pub fun parse_config(String content) -> Map<String> {
    cfg := Map<String>()
    lines := content.split(10)
    for line in lines {
        t := trim(line)
        if t.length() == 0 {
            // blank — skip
        } else {
            if t.char_at(0) == 35 {
                // # comment — skip
            } else {
                eq := index_of_char(t, 61)
                if eq < 0 {
                    // no '=' — skip
                } else {
                    key := trim(t.slice(0, eq))
                    value := trim(t.slice(eq + 1, t.length() - eq - 1))
                    if key.length() > 0 {
                        cfg.put(key, value)
                    }
                }
            }
        }
    }
    return cfg
}

//------------------------------------------------
// examples/http_client.l
//------------------------------------------------
// Minimal HTTP/1.1 GET client on top of L TCP (IP + port; no DNS yet).

pub fun http_get(String host, i32 port, String path) -> String {
    i! := 0
    cfd! := 0 - 1
    while i < 40 {
        fd := l_socket_create()
        if l_socket_connect(fd, host.c_str(), port) == 0 {
            cfd! = fd
            i! = 40
        } else {
            l_socket_close(fd)
            l_sleep_ms(50)
            i! = i + 1
        }
    }
    if cfd < 0 {
        return String("")
    }

    nl := "\r\n"
    req! := String("GET ") + path + String(" HTTP/1.1") + nl
    req! = req + String("Host: ") + host + nl
    req! = req + String("Connection: close") + nl + nl

    l_socket_send(cfd, req.c_str(), req.length())
    buf := l_alloc(4096)
    n := l_socket_recv(cfd, buf, 4096)
    l_socket_close(cfd)
    if n <= 0 {
        l_free(buf)
        return String("")
    }
    // Copy before free — String wraps the buffer pointer.
    tmp := String(buf)
    out := tmp.slice(0, n)
    l_free(buf)
    return out
}

//------------------------------------------------
// examples/http_server.l
//------------------------------------------------
// Minimal HTTP/1.1 server on top of L TCP (Phase 1: one request per accept, no Keep-Alive).

fun index_of_char(String s, i32 ch) -> i32 {
    n := s.length()
    i! := 0
    while i < n {
        if s.char_at(i) == ch {
            return i
        }
        i! = i + 1
    }
    return 0 - 1
}

fun first_line(String req) -> String {
    n := req.length()
    nl := index_of_char(req, 10)
    if nl < 0 {
        return req
    }
    end! := nl
    if end > 0 {
        if req.char_at(end - 1) == 13 {
            end! = end - 1
        }
    }
    return req.slice(0, end)
}

fun make_response(String status, String body) -> String {
    nl := "\r\n"
    len_s := from(body.length())
    h1 := status + nl
    h2 := String("Content-Length: ") + len_s + nl
    h3 := nl
    return h1 + h2 + h3 + body
}

fun request_path(String line) -> String {
    parts := line.split(32)
    if parts.len() >= 2 {
        return parts.get(1)
    }
    return String("/")
}

fun handle_http_client(i32 cfd, Map<String> routes) -> i32 {
    buf := l_alloc(2048)
    n := l_socket_recv(cfd, buf, 2048)
    if n <= 0 {
        l_free(buf)
        l_socket_close(cfd)
        return 0 - 1
    }

    req := String(buf)
    line := first_line(req)
    path := request_path(line)

    body_opt := routes.get(path)
    if body_opt.is_some() {
        body := body_opt.unwrap()
        resp := make_response(String("HTTP/1.1 200 OK"), body)
        l_socket_send(cfd, resp.c_str(), resp.length())
    } else {
        resp404 := make_response(String("HTTP/1.1 404 Not Found"), String("Not Found"))
        l_socket_send(cfd, resp404.c_str(), resp404.length())
    }

    l_socket_close(cfd)
    l_free(buf)
    return 0
}

// Accept exactly one HTTP request then shut down the listen socket (test-friendly).
pub fun serve_once(i32 port) -> i32 {
    routes := Map<String>()
    routes.put(String("/"), String("Hello from L HTTP Server!"))

    sfd := l_socket_create()
    if sfd < 0 {
        return 0 - 1
    }
    if l_socket_bind_and_listen(sfd, port) != 0 {
        l_socket_close(sfd)
        return 0 - 2
    }

    cfd := l_socket_accept(sfd)
    if cfd >= 0 {
        handle_http_client(cfd, routes)
    }
    l_socket_close(sfd)
    return 0
}

//------------------------------------------------
// examples/mini_lexer.l
//------------------------------------------------
// Minimal space-splitting lexer in pure L.
// Classifies each part as number (Variant i32) or identifier/symbol (Variant String).

fun is_number_token(String s) -> bool {
    n := s.length()
    if n == 0 {
        return false
    }
    i! := 0
    while i < n {
        if is_digit(s.char_at(i)) == false {
            return false
        }
        i! = i + 1
    }
    return true
}

fun is_ident_token(String s) -> bool {
    n := s.length()
    if n == 0 {
        return false
    }
    if is_alpha(s.char_at(0)) == false {
        return false
    }
    i! := 1
    while i < n {
        c := s.char_at(i)
        if is_alpha(c) == false {
            if is_digit(c) == false {
                return false
            }
        }
        i! = i + 1
    }
    return true
}

pub fun tokenize(String src) -> Array<Variant> {
    tokens := Array<Variant>()
    parts := src.split(32)
    i! := 0
    while i < parts.len() {
        part := parts.get(i)
        if is_number_token(part) {
            tokens.push(Variant.from_i32(part.to_i32()))
        } else {
            tokens.push(Variant.from_string(part))
        }
        i! = i + 1
    }
    return tokens
}

//------------------------------------------------
// examples/mini_parser.l
//------------------------------------------------
// Minimal var-decl parser in pure L (self-hosting warm-up).
// Recognizes: `i32 a = 10`  and  `b := 20` / `name := "L"`

interface AstNode {
    fun node_kind() -> String
}

class VarDeclNode : AstNode {
    String type_name
    String name
    String value

    pub fun node_kind() -> String {
        return "VarDecl"
    }
}

fun strip_value_quotes(String s) -> String {
    n := s.length()
    if n >= 2 {
        // Ordinary quoted literal: "L"
        if s.char_at(0) == 34 {
            if s.char_at(n - 1) == 34 {
                return s.slice(1, n - 2)
            }
        }
        // L source embeds \" without unescaping: \"L\"
        if n >= 4 {
            if s.char_at(0) == 92 {
                if s.char_at(1) == 34 {
                    if s.char_at(n - 2) == 92 {
                        if s.char_at(n - 1) == 34 {
                            return s.slice(2, n - 4)
                        }
                    }
                }
            }
        }
    }
    return s
}

fun is_ident_name(String s) -> bool {
    n := s.length()
    if n == 0 {
        return false
    }
    if is_alpha(s.char_at(0)) == false {
        return false
    }
    i! := 1
    while i < n {
        c := s.char_at(i)
        if is_alpha(c) == false {
            if is_digit(c) == false {
                if c != 95 {
                    return false
                }
            }
        }
        i! = i + 1
    }
    return true
}

pub fun parse_var_decl(String line) -> Optional<VarDeclNode> {
    parts := line.split(32)
    n := parts.len()

    // Typed: TYPE NAME = VALUE
    if n == 4 {
        ty := parts.get(0)
        nm := parts.get(1)
        eq := parts.get(2)
        val := parts.get(3)
        if eq == "=" {
            if is_ident_name(ty) {
                if is_ident_name(nm) {
                    sv := strip_value_quotes(val)
                    node := VarDeclNode()
                    node.type_name = ty
                    node.name = nm
                    node.value = sv
                    return Optional<VarDeclNode>(true, node)
                }
            }
        }
    }

    // Inferred: NAME := VALUE
    if n == 3 {
        nm := parts.get(0)
        op := parts.get(1)
        val := parts.get(2)
        if op == ":=" {
            if is_ident_name(nm) {
                sv := strip_value_quotes(val)
                inferred := String("inferred")
                node := VarDeclNode()
                node.type_name = inferred
                node.name = nm
                node.value = sv
                return Optional<VarDeclNode>(true, node)
            }
        }
    }

    return Optional<VarDeclNode>()
}

//------------------------------------------------
// tests/cases/app_cfg.l
//------------------------------------------------
app.rs232 {
    .baud = 115200
    .parity = "none"
    .data_bits = 8
}

//------------------------------------------------
// tests/cases/arena_test.l
//------------------------------------------------
fun build_string(i32 n) -> String {
    // 申请一个大内存池
    a := arena(1024)

    // 在该内存池上疯狂分配，不需要逐次 free
    s1 := a.new_string("Hello")
    s2 := a.new_string(" World")

    res := s1 + s2 // 结果走普通堆，可安全逃离 arena 作用域
    return res
    // 函数结束时，a 内存池会自动 l_arena_free，一次性清空 bump 分配
}

fun main() -> i32 {
    res := build_string(1)
    print(res)
    return 0
}

//------------------------------------------------
// tests/cases/array_poly_test.l
//------------------------------------------------
interface IReader {
    fun read_value() -> i32
}

class Point : IReader {
    i32 x
    fun init(i32 v) {
        x = v
    }
    fun read_value() -> i32 {
        return x
    }
}

class User : IReader {
    i32 id
    fun init(i32 v) {
        id = v
    }
    fun read_value() -> i32 {
        return id
    }
}

fun main() -> i32 {
    arr := Array<IReader>()

    arr.push(Point(42))
    arr.push(User(99))

    i32 sum! = 0
    for item in arr {
        sum! += item.read_value()
    }

    print(sum)
    return 0
}

//------------------------------------------------
// tests/cases/array_sugar.l
//------------------------------------------------
fun main() -> i32 {
    // 1. Array literal
    arr := [10, 20, 30]

    // 2. Trailing-block each
    arr.each { |x|
        print(x * 2)
    }
    return 0
}

//------------------------------------------------
// tests/cases/array_test.l
//------------------------------------------------
fun main() -> i32 {
    arr := Array<i32>()
    arr.push(11)
    arr.push(22)
    arr.push(33)
    val := arr.get(1)
    print(val)
    return 0
}

//------------------------------------------------
// tests/cases/atomic_stress.l
//------------------------------------------------
i32 global_atomic_counter! = 0

fun worker_atomic() {
    for i in 0..10000 {
        // Lock-free: no sync — C atomics are the synchronization.
        l_atomic_add_i32(&global_atomic_counter, 1)
    }
}

fun main() -> i32 {
    for i in 0..10 {
        spawn { worker_atomic() }
    }
    l_sleep_ms(1000)
    print(global_atomic_counter)
    return 0
}

//------------------------------------------------
// tests/cases/auto_free.l
//------------------------------------------------
class Box {
    i32 val
    fun init(i32 v) {
        val = v
    }
}

fun create_and_leak() {
    b := heap Box(42)
    // Intentionally no defer l_free(b) — compiler must auto-inject.
    print(b.val)
}

fun main() -> i32 {
    create_and_leak()
    print("Done")
    return 0
}

//------------------------------------------------
// tests/cases/callback_test.l
//------------------------------------------------
extern module c_runtime {
    fun l_register_callback(fun(i32) cb)
    fun l_trigger_callback(i32 val)
}

fun my_callback(i32 v) {
    print("Callback triggered with: ")
    print(v)
}

fun main() -> i32 {
    l_register_callback(my_callback)
    l_trigger_callback(42)
    return 0
}

//------------------------------------------------
// tests/cases/cast_test.l
//------------------------------------------------
interface IReader {
    fun read_value() -> i32
}

class Point : IReader {
    i32 x
    fun init(i32 v) {
        x = v
    }
    fun read_value() -> i32 {
        return x
    }
}

class User : IReader {
    i32 id
    fun init(i32 v) {
        id = v
    }
    fun read_value() -> i32 {
        return id
    }
}

fun main() -> i32 {
    // Upcast (auto-box to fat pointer)
    IReader r = Point(42)

    // Downcast (success)
    p_opt := r as Point
    if p_opt.is_some() {
        print(p_opt.unwrap().x)
    }

    // Downcast (fail: r is Point, not User)
    u_opt := r as User
    if !u_opt.is_some() {
        print("Not a User")
    }
    return 0
}

//------------------------------------------------
// tests/cases/class_basic.l
//------------------------------------------------
class Point {
    i32 x
    i32 y
}

fun main() -> i32 {
    p := Point()
    p.x = 10
    p.y = 20
    sum := p.x + p.y
    print(sum)
    return 0
}

//------------------------------------------------
// tests/cases/cli_args_test.l
//------------------------------------------------
fun main() -> i32 {
    args := get_args()
    print("Arguments count: {args.len()}")
    return 0
}

//------------------------------------------------
// tests/cases/cli_flags_test.l
//------------------------------------------------
fun main() -> i32 {
    flags := parse_flags()

    // Locals: method chaining on temporaries is not supported yet.
    ver_opt := flags.get("version")
    ver := ver_opt.unwrap_or("false")
    if ver == "true" {
        print("L Language v1.0.0")
    }

    name_opt := flags.get("name")
    if name_opt.is_some() {
        print("Hello, " + name_opt.unwrap())
    }
    return 0
}

//------------------------------------------------
// tests/cases/clone_test.l
//------------------------------------------------
class Point {
    i32 x
    i32 y
    fun init(i32 a, i32 b) {
        x = a
        y = b
    }
}

fun main() -> i32 {
    a := Point(10, 20)
    // 使用编译器自动生成的 clone
    b := a.clone()

    b.x = 99

    print(a.x)
    print(b.x)
    return 0
}

//------------------------------------------------
// tests/cases/closure_escape_good.l
//------------------------------------------------
fun get_closure() -> fun() {
    a := 10
    return || a * 2
}

fun main() -> i32 {
    cb := get_closure()
    res := cb()
    print(res)
    return 0
}

//------------------------------------------------
// tests/cases/code_as_config/lconf.l
//------------------------------------------------
fun main() -> i32 {
    // Register project deps via host AbsoluteSymbols (C++ add_dependency).
    add_dependency("crypto", "1.0")
    add_dependency("network", "0.2")

    if get_build_mode() == "Debug" {
        add_dependency("debug_utils", "0.1")
    }
    return 0
}

//------------------------------------------------
// tests/cases/code_as_config/main.l
//------------------------------------------------
fun main() -> i32 {
    return 0
}

//------------------------------------------------
// tests/cases/codegen_simple.l
//------------------------------------------------
fun main() -> i32 {
    x := 10
    y := 20
    return x + y
}

//------------------------------------------------
// tests/cases/compound_assign.l
//------------------------------------------------
fun main() -> i32 {
    i32 a! = 10
    a! += 5
    a! -= 3
    a! *= 2
    a! /= 4

    print(a)
    return 0
}

//------------------------------------------------
// tests/cases/concurrency_stress.l
//------------------------------------------------
i32 global_counter! = 0

fun worker() {
    for i in 0..10000 {
        sync {
            global_counter! = global_counter + 1
        }
    }
}

fun main() -> i32 {
    l_reset_lock_counts()

    // Launch 10 workers asynchronously, keep handles for join.
    h0 := spawn { worker() }
    h1 := spawn { worker() }
    h2 := spawn { worker() }
    h3 := spawn { worker() }
    h4 := spawn { worker() }
    h5 := spawn { worker() }
    h6 := spawn { worker() }
    h7 := spawn { worker() }
    h8 := spawn { worker() }
    h9 := spawn { worker() }

    l_thread_join(h0)
    l_thread_join(h1)
    l_thread_join(h2)
    l_thread_join(h3)
    l_thread_join(h4)
    l_thread_join(h5)
    l_thread_join(h6)
    l_thread_join(h7)
    l_thread_join(h8)
    l_thread_join(h9)

    print(global_counter)
    print(l_get_lock_count())
    return 0
}

//------------------------------------------------
// tests/cases/config_test.l
//------------------------------------------------
fun main() -> i32 {
    // Robustness: multi-space, blank-with-spaces, no-'=', // vs #, multi-'='.
    src := "# Database Config\nhost = 127.0.0.1\nport    =    8080\n\n   \nthis_is_a_bad_line\n// not_a_hash_comment\n//fake = still_stored\nexpr = a=b=c\n# App\nname = L-Lang\n"
    cfg := parse_config(src)

    h := cfg.get("host")
    print(h.unwrap_or(String("")))

    p := cfg.get("port")
    print(p.unwrap_or(String("")))

    n := cfg.get("name")
    print(n.unwrap_or(String("")))

    // First '=' only: value keeps remaining equals
    e := cfg.get("expr")
    print(e.unwrap_or(String("")))

    // //fake = still_stored → not a # comment, so key "//fake" is stored
    fake := cfg.get("//fake")
    print(fake.unwrap_or(String("")))

    // Malformed / space-only / # comments must not invent keys
    bad := cfg.get("this_is_a_bad_line")
    print(bad.unwrap_or(String("skipped")))

    slash := cfg.get("// not_a_hash_comment")
    print(slash.unwrap_or(String("skipped")))

    missing := cfg.get("Database")
    print(missing.unwrap_or(String("ok")))

    return 0
}

//------------------------------------------------
// tests/cases/constructor.l
//------------------------------------------------
class User {
    i32 age
    fun init(i32 a) {
        age = a + 10
    }
    pub fun get_age() -> i32 {
        return age
    }
}

fun main() -> i32 {
    u := User(5)
    print(u.get_age())
    return 0
}

//------------------------------------------------
// tests/cases/control_flow.l
//------------------------------------------------
fun main() -> i32 {
    sum := 0
    i := 1
    while i <= 10 {
        sum = sum + i
        i = i + 1
    }
    if sum == 55 {
        print(999)
    } else {
        print(0)
    }
    return 0
}

//------------------------------------------------
// tests/cases/destructure.l
//------------------------------------------------
class Pair {
    i32 first
    i32 second
    fun init(i32 a, i32 b) {
        first = a
        second = b
    }
}

fun main() -> i32 {
    p := Pair(10, 20)
    // Destructure class fields in declaration order
    (x, y) := p

    print(x)
    print(y)
    return 0
}

//------------------------------------------------
// tests/cases/downcast_test.l
//------------------------------------------------
interface IReader {
    fun read_value() -> i32
}

class Point : IReader {
    i32 x
    fun init(i32 v) {
        x = v
    }
    fun read_value() -> i32 {
        return x
    }
}

class User : IReader {
    i32 id
    fun init(i32 v) {
        id = v
    }
    fun read_value() -> i32 {
        return id
    }
}

fun main() -> i32 {
    p := Point(42)
    r := p as IReader

    // Downcast success → Optional<Point>
    p_opt := r as Point
    if p_opt.is_some() {
        print(p_opt.unwrap().x)
    }

    // Wrong class → empty Optional
    u_opt := r as User
    if !u_opt.is_some() {
        print("safe")
    }
    return 0
}

//------------------------------------------------
// tests/cases/dynamic_dispatch.l
//------------------------------------------------
interface IReader {
    fun read_value() -> i32
}

class Point : IReader {
    i32 x
    fun init(i32 v) {
        x = v
    }
    fun read_value() -> i32 {
        return x
    }
}

class User : IReader {
    i32 id
    fun init(i32 v) {
        id = v
    }
    fun read_value() -> i32 {
        return id
    }
}

// Receives an interface fat pointer — no knowledge of the concrete class.
fun print_any_reader(IReader r) {
    print(r.read_value())
}

fun main() -> i32 {
    p := Point(42)
    u := User(99)

    print_any_reader(p)
    print_any_reader(u)
    return 0
}

//------------------------------------------------
// tests/cases/env_client_test.l
//------------------------------------------------
// get_env(PATH) + localhost HTTP GET via examples/http_client.l + serve_once.

fun main() -> i32 {
    path := get_env(String("PATH"))
    if path.is_some() {
        print("PATH exists")
    } else {
        print("PATH missing")
    }

    port := 8091
    hs := spawn {
        serve_once(port)
    }

    l_sleep_ms(200)

    resp := http_get(String("127.0.0.1"), port, String("/"))
    print(resp)

    l_thread_join(hs)
    return 0
}

//------------------------------------------------
// tests/cases/expr.l
//------------------------------------------------
i32 a = 1 + 2 * 3
b := 10 - 5 + 2

//------------------------------------------------
// tests/cases/extern_test.l
//------------------------------------------------
extern module c_lib {
    fun strlen(ptr s) -> i64
}

fun main() -> i32 {
    len := strlen("Hello")
    print(len)
    return 0
}

//------------------------------------------------
// tests/cases/ffi_export.l
//------------------------------------------------
// C-ABI exports for foreign callers (Ruby Fiddle / Python ctypes / C++).

pub fun add_numbers(i32 a, i32 b) -> i32 {
    return a + b
}

class Counter {
    i32 val
    fun init(i32 v) {
        val = v
    }
    pub fun increment(i32 step) {
        val = val + step
    }
    pub fun get_val() -> i32 {
        return val
    }
}

// Opaque-pointer surface: Class* travels as `ptr` across the FFI boundary.
pub fun counter_create(i32 init_val) -> ptr {
    c := heap Counter(init_val)
    return c as ptr
}

pub fun counter_increment(ptr c_ptr, i32 step) {
    c := c_ptr as Counter
    c.increment(step)
}

pub fun counter_get(ptr c_ptr) -> i32 {
    c := c_ptr as Counter
    return c.get_val()
}

pub fun counter_destroy(ptr c_ptr) {
    l_free(c_ptr)
}

//------------------------------------------------
// tests/cases/field_syntax_bad.l
//------------------------------------------------
class User {
    i32 age
}

fun main() -> i32 {
    u := User()
    u.age_ = 30
    return 0
}

//------------------------------------------------
// tests/cases/field_syntax_test.l
//------------------------------------------------
class User {
    i32 age
    String name

    fun init(i32 a, String n) {
        age = a
        name = n
    }
    // Accessor present → u.age must call get_age().
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
    print(u.age)
    u.name = "Lang"

    p := Plain()
    p.score = 99
    print(p.score)

    // u.age_ = 30  // trailing _ must be a compile error
    return 0
}

//------------------------------------------------
// tests/cases/file_io_test.l
//------------------------------------------------
fun main() -> i32 {
    path := String("test_file_io.txt")
    payload := "Hello\nL-Lang!\r\n"

    ok := File.write_all(path, payload)
    if ok == false {
        print("write failed")
        return 1
    }

    res := File.read_all(path)
    if res.is_some() == false {
        print("read failed")
        return 1
    }

    text := res.unwrap()
    print(text)
    return 0
}

//------------------------------------------------
// tests/cases/for_in.l
//------------------------------------------------
fun main() -> i32 {
    arr := Array<i32>()
    arr.push(5)
    arr.push(10)
    arr.push(15)

    sum := 0
    for val in arr {
        sum = sum + val
    }
    print(sum)
    return 0
}

//------------------------------------------------
// tests/cases/fs_test.l
//------------------------------------------------
fun main() -> i32 {
    dir := String("test_temp_dir")
    nested := String("test_temp_dir/fs_nested.txt")

    if file_exists(dir) {
        print("before:true")
        return 1
    }
    print("before:false")

    mk := create_dir(dir)
    if mk.is_ok() == false {
        print("create:err")
        return 1
    }
    print("create:ok")

    if file_exists(dir) == false {
        print("after:false")
        return 1
    }
    print("after:true")

    if File.write_all(nested, String("fs-ok")) == false {
        print("write:err")
        return 1
    }

    rm := remove_file(nested)
    if rm.is_ok() == false {
        print("remove_file:err")
        return 1
    }
    print("remove_file:ok")

    rd := remove_dir(dir)
    if rd.is_ok() == false {
        print("remove_dir:err")
        return 1
    }
    print("remove_dir:ok")

    if file_exists(dir) {
        print("cleanup:true")
        return 1
    }
    print("cleanup:false")
    return 0
}

//------------------------------------------------
// tests/cases/func_call.l
//------------------------------------------------
i32 x = (1 + 2) * 3
y := add(10, 20)

//------------------------------------------------
// tests/cases/func_def.l
//------------------------------------------------
fun add(i32 a, i32 b) -> i32 {
    return a + b
}

//------------------------------------------------
// tests/cases/generic_constraint.l
//------------------------------------------------
interface IPrintable {
    fun printSelf()
}

class Good : IPrintable {
    fun printSelf() {
        print("Good")
    }
}

class Bad {
    fun printSelf() {
        print("Bad")
    }
}

// Constraint: T must implement IPrintable.
fun check_and_print<T: IPrintable>(T item) {
    item.printSelf()
}

fun main() -> i32 {
    check_and_print(Good())
    // check_and_print(Bad())  // must be rejected at compile time
    return 0
}

//------------------------------------------------
// tests/cases/generic_constraint_bad.l
//------------------------------------------------
interface IPrintable {
    fun printSelf()
}

class Good : IPrintable {
    fun printSelf() {
        print("Good")
    }
}

class Bad {
    fun printSelf() {
        print("Bad")
    }
}

fun check_and_print<T: IPrintable>(T item) {
    item.printSelf()
}

fun main() -> i32 {
    check_and_print(Bad())
    return 0
}

//------------------------------------------------
// tests/cases/generic_inference.l
//------------------------------------------------
fun main() -> i32 {
    arr := Array<i32>()
    arr.push(100)
    print(arr.get(0))
    return 0
}

//------------------------------------------------
// tests/cases/generic_test.l
//------------------------------------------------
interface IReader {
    fun read_value() -> i32
}

class Point : IReader {
    i32 x
    fun read_value() -> i32 {
        return x
    }
}

fun print_val<T: IReader>(T item) {
    v := item.read_value()
    print(v)
}

fun main() -> i32 {
    p := Point()
    p.x = 88
    print_val(p)
    return 0
}

//------------------------------------------------
// tests/cases/global_class_test.l
//------------------------------------------------
class App {
    pub i32 version
    fun init() {
        version = 1
    }
}

// Global class instance — zero-init storage, App() / init runs in __l_global_init.
App my_app = App()

fun main() -> i32 {
    print(my_app.version)
    my_app.version = 2
    print(my_app.version)

    my_app {
        .version = 3
    }
    print(my_app.version)
    return 0
}

//------------------------------------------------
// tests/cases/global_test.l
//------------------------------------------------
i32 global_count = 0
const MAX_SIZE = 10
constd GLOBAL_VERSION = 42

fun main() -> i32 {
    global_count = global_count + 5
    global_count = global_count + 5
    print(global_count)
    print(MAX_SIZE)
    print(GLOBAL_VERSION)
    return 0
}

//------------------------------------------------
// tests/cases/golden.l
//------------------------------------------------
// 1. 模块级：只用 const / constd / 显式类型（:= 仅限函数内局部推导）
const MAX_LIMIT = 100
constd start_time = get_current_time()
constd size = get_file_size("data.bin")

i32 mut_count! = 0        // 模块级可变：显式类型 + !
i32 age! = 18
mut_count! = 10

// 2. 函数定义
fun add(i32 a, i32 b) -> i32 {
    return a + b
}

// 3. 类与接口（字段禁止 :=）
interface IPrint {
    fun printSelf()
}

partial class User : IPrint {
    String name = "L"
    i32 age = 18
}

partial class User {
    fun printSelf() {
        print(name)
    }
}

// 4. 尾随块与泛型（必须带接口约束，禁止裸 <T>）
fun processList<T: IPrint>(Array<T> list, fun(T) -> void action) {
    // 内部逻辑
}

// 5. Phase 1 入口；:= 仅出现在局部作用域
fun main() -> i32 {
    list! := Array<User>(1)
    u := User()
    list.push(u)

    processList(list) { |item|
        item.printSelf()
    };  // trailing-block expr stmt: explicit ';' required (no ASI after '}')

    sum := add(1, 2)
    return sum
}

//------------------------------------------------
// tests/cases/heap_defer.l
//------------------------------------------------
class Point {
    i32 x
}

fun main() -> i32 {
    p := heap Point()
    defer l_free(p)

    p.x = 77
    print(p.x)
    return 0
}

//------------------------------------------------
// tests/cases/hello_world.l
//------------------------------------------------
fun main() -> i32 {
    print(42)
    return 0
}

//------------------------------------------------
// tests/cases/http_server_test.l
//------------------------------------------------
// Spawn a one-shot HTTP server, then GET / from the main thread.

fun http_client_get(i32 port, String path) -> String {
    i! := 0
    cfd! := 0 - 1
    while i < 40 {
        fd := l_socket_create()
        if l_socket_connect(fd, "127.0.0.1", port) == 0 {
            cfd! = fd
            i! = 40
        } else {
            l_socket_close(fd)
            l_sleep_ms(50)
            i! = i + 1
        }
    }
    if cfd < 0 {
        return String("")
    }

    nl := "\r\n"
    req! := String("GET ") + path + String(" HTTP/1.1") + nl
    req! = req + String("Host: 127.0.0.1") + nl + nl

    l_socket_send(cfd, req.c_str(), req.length())
    buf := l_alloc(2048)
    l_socket_recv(cfd, buf, 2048)
    l_socket_close(cfd)
    // Copy before free — String wraps the buffer pointer.
    tmp := String(buf)
    out := tmp.slice(0, tmp.length())
    l_free(buf)
    return out
}

fun main() -> i32 {
    port := 8088

    hs := spawn {
        serve_once(port)
    }

    l_sleep_ms(200)

    resp := http_client_get(port, String("/"))
    print(resp)

    l_thread_join(hs)
    return 0
}

//------------------------------------------------
// tests/cases/http_stdlib_test.l
//------------------------------------------------
fun main() -> i32 {
    routes := Map<String>()
    routes.put(String("/"), String("hello_stdlib_http"))
    hs := spawn {
        http_serve_once(18088, routes)
    }
    sleep(100)
    resp := http_get(String("127.0.0.1"), 18088, String("/"))
    join(hs)
    if http_status_ok(resp) == false {
        print("fail status")
        print(resp)
        return 1
    }
    body := http_body(resp)
    print(body)
    needle := String("hello_stdlib_http")
    if body.contains(needle) == false {
        print("fail body")
        return 1
    }
    print(String("http_stdlib_ok"))
    return 0
}

//------------------------------------------------
// tests/cases/inc_dec.l
//------------------------------------------------
fun main() -> i32 {
    i32 a! = 10

    // 复合赋值
    a! += 5
    a! -= 3

    // 自增/自减（作为语句）
    a!++
    ++a!
    a!--

    print(a)

    // 作为表达式
    i32 b! = 5
    i32 c = b!++
    i32 d = ++b!

    print(c)
    print(b)
    print(d)

    return 0
}

//------------------------------------------------
// tests/cases/input_test.l
//------------------------------------------------
fun main() -> i32 {
    print("Please enter your name:")
    name := read_line()
    print("Hello, " + name + "!")
    return 0
}

//------------------------------------------------
// tests/cases/interface_test.l
//------------------------------------------------
interface IReader {
    fun read_value() -> i32
}

class Point : IReader {
    i32 x
    fun read_value() -> i32 {
        return x
    }
}

fun main() -> i32 {
    p := Point()
    p.x = 42

    // Interface binding needs an explicit type (:= would infer Point).
    IReader iface = p
    result := iface.read_value()
    print(result)
    return 0
}

//------------------------------------------------
// tests/cases/join_test.l
//------------------------------------------------
fun main() -> i32 {
    h := spawn {
        sleep(50)
        print(String("worker"))
    }
    join(h)
    print(String("joined"))
    return 0
}

//------------------------------------------------
// tests/cases/json_test.l
//------------------------------------------------
fun main() -> i32 {
    // Build {"name":"L","n":42,"ok":true,"x":null} without '{' in L string literals.
    lb := String(l_str_from_byte(123))
    rb := String(l_str_from_byte(125))
    q := String(l_str_from_byte(34))
    raw := lb + q + String("name") + q + String(":") + q + String("L") + q + String(",") + q + String("n") + q + String(":42,") + q + String("ok") + q + String(":true,") + q + String("x") + q + String(":null") + rb
    parsed := json_parse_object(raw)
    if parsed.is_ok() == false {
        print("fail parse")
        return 1
    }
    m := parsed.unwrap()
    name_key := String("name")
    name := m.get(name_key)
    if name.is_some() == false {
        print("fail name")
        return 1
    }
    print(name.unwrap())
    n_key := String("n")
    n := m.get(n_key)
    print(n.unwrap())
    round := json_stringify_object(m)
    print(round)
    if m.len() < 1 {
        print("fail len")
        return 1
    }
    print(String("json_ok"))
    return 0
}

//------------------------------------------------
// tests/cases/kw_bad.l
//------------------------------------------------
fun create_user(String name, i32 age, String role) {
    print(name)
    print(age)
    print(role)
}

fun main() -> i32 {
    // Positional after keyword must be a parse error.
    create_user("Bob", age: 20, "user")
    return 0
}

//------------------------------------------------
// tests/cases/kw_mixed.l
//------------------------------------------------
fun create_user(String name, i32 age, String role) {
    print(name)
    print(age)
    print(role)
}

fun main() -> i32 {
    // Positional + keyword mix; keywords out of declaration order.
    create_user("Alice", role: "admin", age: 20)
    return 0
}

//------------------------------------------------
// tests/cases/lambda_capture.l
//------------------------------------------------
fun main() -> i32 {
    i32 base = 100

    // 捕获了外部的 base 变量
    add_base := |x| x + base

    res := add_base(5)
    print(res)
    return 0
}

//------------------------------------------------
// tests/cases/lambda_state.l
//------------------------------------------------
fun main() -> i32 {
    arr := [1, 2, 3]
    i32 sum! = 0
    arr.each { |x|
        sum! = sum + x
    }
    print(sum)
    return 0
}

//------------------------------------------------
// tests/cases/lambda_test.l
//------------------------------------------------
// fun(i32) alone means void callback (e.g. Array.each); returning a value needs ->i32.
fun apply(fun(i32)->i32 f, i32 val) -> i32 {
    return f(val)
}

fun main() -> i32 {
    // 1. 直接作为参数传入
    res1 := apply(|x| x * 2, 10)
    print(res1)

    // 2. 赋值给变量
    double := |x| x * 2
    res2 := double(5)
    print(res2)
    return 0
}

//------------------------------------------------
// tests/cases/lcmd_l_callback.l
//------------------------------------------------
pub fun __compile_time_handler(String path, String cmd, String args) -> String {
    if cmd == "make_answer" {
        return "i32 the_answer = 42;"
    }
    return ""
}

fun main() -> i32 {
    //LCMD_make_answer()
    print(the_answer)
    return 0
}

//------------------------------------------------
// tests/cases/lcmd_path.l
//------------------------------------------------
pub fun __compile_time_handler(String path, String cmd, String args) -> String {
    if path == "User.generate" && cmd == "make_hello" {
        return "pub fun say_hello() -> i32 { return 42; }"
    }
    return ""
}

class User {
    i32 id
    fun generate() {
        //LCMD_make_hello()
    }
}

fun main() -> i32 {
    u := User()
    print(u.say_hello())
    return 0
}

//------------------------------------------------
// tests/cases/lexer_demo_test.l
//------------------------------------------------
fun main() -> i32 {
    src := "let x = 42"
    tokens := tokenize(src)

    i! := 0
    while i < tokens.len() {
        t := tokens.get(i)
        if t.is_i32() {
            opt := t.as_i32()
            print(opt.unwrap_or(0))
        } else {
            opts := t.as_string()
            s := opts.unwrap_or(String(""))
            print(s)
        }
        i! = i + 1
    }
    return 0
}

//------------------------------------------------
// tests/cases/lifecycle_test.l
//------------------------------------------------
fun __init() {
    print("System Initialized")
}

fun __deinit() {
    print("System Cleaned up")
}

fun main() -> i32 {
    print("Main is running")
    return 0
}

//------------------------------------------------
// tests/cases/log_config_test.l
//------------------------------------------------
fun main() -> i32 {
    dir := String("build/clang-debug/test_out")
    path := path_join(dir, String("log_config_test.json"))
    lb := String(l_str_from_byte(123))
    rb := String(l_str_from_byte(125))
    q := String(l_str_from_byte(34))
    body := lb + q + String("host") + q + String(":") + q + String("127.0.0.1") + q + String(",") + q + String("port") + q + String(":") + q + String("8080") + q + rb
    write_all(path, body)
    cfg_res := load_json_config(path)
    if cfg_res.is_ok() == false {
        print("fail config")
        return 1
    }
    cfg := cfg_res.unwrap()
    host := config_get(cfg, String("host"), String(""))
    print(host)
    log_info(String("config loaded"))
    remove_file(path)
    print(String("log_config_ok"))
    return 0
}

//------------------------------------------------
// tests/cases/main_import.l
//------------------------------------------------
import "mod_a.l"

fun main() -> i32 {
    a := get_num()
    print(a)
    return 0
}

//------------------------------------------------
// tests/cases/map_test.l
//------------------------------------------------
fun main() -> i32 {
    m := Map<i32>()
    m.put("one", 1)
    m.put("two", 2)
    m.put("three", 3)

    v1 := m.get("two")
    print(v1.unwrap_or(0))

    v2 := m.get("four")
    print(v2.unwrap_or(99))
    return 0
}

//------------------------------------------------
// tests/cases/match_test.l
//------------------------------------------------
fun check(i32 x) {
    match x {
        1 => { print("One") }
        2 => { print("Two") }
        _ => { print("Other") }
    }
}

fun main() -> i32 {
    check(1)
    check(2)
    check(99)
    return 0
}

//------------------------------------------------
// tests/cases/memory_leak_hard.l
//------------------------------------------------
interface IReader {
    fun read_value() -> i32
}

class Point : IReader {
    i32 x
    fun init(i32 v) {
        x = v
    }
    fun read_value() -> i32 {
        return x
    }
}

fun do_heavy_work() {
    for i in 0..1000 {
        // Scene 1: string concat intermediate objects
        s1 := String("Hello")
        s2 := String(" World")
        s3 := s1 + s2

        // Scene 2: interface boxing into Array
        arr := Array<IReader>()
        arr.push(Point(i))

        // Scene 3: capturing closure call
        closure := |x| x + i
        closure(10)
    }
}

fun main() -> i32 {
    before := l_get_alloc_count()

    do_heavy_work()

    after := l_get_alloc_count()

    print("Before: " + before)
    print("After: " + after)

    if before == after {
        print("No memory leaks!")
    } else {
        print("Memory leak detected!")
    }
    return 0
}

//------------------------------------------------
// tests/cases/memory_stress.l
//------------------------------------------------
fun do_work(i32 times) {
    // 循环创建堆对象并自动回收
    for i in 0..times {
        // 使用 heap 和 String 拼接
        s := heap String("Test")
        t := s + " String"
        // 注意：t 也是 heap 分配的，如果没有释放，这里会泄漏
    }
}

fun main() -> i32 {
    before := l_get_alloc_count()

    do_work(1000)

    after := l_get_alloc_count()

    if before == after {
        print("No memory leaks!")
    } else {
        print("Memory leak detected!")
    }
    return 0
}

//------------------------------------------------
// tests/cases/mini_parser_test.l
//------------------------------------------------
fun print_decl(VarDeclNode d) {
    print("Type: {d.type_name}")
    print("Name: {d.name}")
    print("Value: {d.value}")
}

fun main() -> i32 {
    // 1) Typed declaration
    opt1 := parse_var_decl("i32 age = 18")
    if opt1.is_some() {
        print_decl(opt1.unwrap())
    } else {
        print("parse failed: typed")
    }

    // 2) Inferred declaration with a string literal value.
    // L keeps \" in the string body (no unescape yet), so the line is:
    //   name := \"L\"
    opt2 := parse_var_decl("name := \"L\"")
    if opt2.is_some() {
        print_decl(opt2.unwrap())
    } else {
        print("parse failed: inferred")
    }

    return 0
}

//------------------------------------------------
// tests/cases/mod_a.l
//------------------------------------------------
pub fun get_num() -> i32 {
    return 99
}

//------------------------------------------------
// tests/cases/multi_a.l
//------------------------------------------------
pub fun print_a() {
    print("A")
}

//------------------------------------------------
// tests/cases/multi_b.l
//------------------------------------------------
pub fun print_b() {
    print("B")
}

//------------------------------------------------
// tests/cases/multi_main.l
//------------------------------------------------
import "multi_a.l"
import "multi_b.l"

fun main() -> i32 {
    print_a()
    print_b()
    return 0
}

//------------------------------------------------
// tests/cases/nested_init.l
//------------------------------------------------
class RS232 {
    pub i32 baud
    pub String parity
    pub i32 data_bits
    fun init(i32 b, String p, i32 d) {
        baud = b
        parity = p
        data_bits = d
    }
}

class App {
    pub RS232 rs232
    fun init() {
        rs232 = RS232(0, "", 0)
    }
}

App app = App()

import "app_cfg.l"

fun main() -> i32 {
    print(app.rs232.baud)
    print(app.rs232.parity)
    return 0
}

//------------------------------------------------
// tests/cases/net_test.l
//------------------------------------------------
// TCP loopback: server + client OS threads.
// Port 38473 (8080 is often taken by other local services).

fun client_connect() -> i32 {
    i! := 0
    while i < 40 {
        fd := l_socket_create()
        if l_socket_connect(fd, "127.0.0.1", 38473) == 0 {
            return fd
        }
        l_socket_close(fd)
        l_sleep_ms(50)
        i! = i + 1
    }
    return 0 - 1
}

fun run_server(ptr out_buf) -> i32 {
    sfd := l_socket_create()
    if sfd < 0 {
        return 0 - 1
    }
    if l_socket_bind_and_listen(sfd, 38473) != 0 {
        l_socket_close(sfd)
        return 0 - 2
    }
    cfd := l_socket_accept(sfd)
    if cfd >= 0 {
        l_socket_recv(cfd, out_buf, 64)
        ptr reply = "Hello Client"
        l_socket_send(cfd, reply, l_str_len(reply))
        l_socket_close(cfd)
    }
    l_socket_close(sfd)
    return 0
}

fun run_client(ptr out_buf) -> i32 {
    cfd := client_connect()
    if cfd < 0 {
        return 0 - 1
    }
    ptr hello = "Hello Server"
    l_socket_send(cfd, hello, l_str_len(hello))
    l_socket_recv(cfd, out_buf, 64)
    l_socket_close(cfd)
    return 0
}

fun main() -> i32 {
    ptr server_buf = l_alloc(64)
    ptr client_buf = l_alloc(64)

    hs := spawn {
        run_server(server_buf)
    }

    l_sleep_ms(200)

    hc := spawn {
        run_client(client_buf)
    }

    l_thread_join(hs)
    l_thread_join(hc)

    print(String(server_buf))
    print(String(client_buf))

    l_free(server_buf)
    l_free(client_buf)
    return 0
}

//------------------------------------------------
// tests/cases/node_hook_optin.l
//------------------------------------------------
pub fun __on_class_parsed(String className) -> String {
    if className == "UserAuto" {
        return "pub fun print_auto_log() -> i32 { return 888; }"
    }
    return ""
}

// 开启 Hook
//LCMD_enable_class_hook()

class UserAuto {
    i32 id
}

// 关闭 Hook
//LCMD_disable_class_hook()

class ShouldNotTrigger {
    i32 x
}

fun main() -> i32 {
    print(print_auto_log())
    return 0
}

//------------------------------------------------
// tests/cases/node_hook_optin_off.l
//------------------------------------------------
// Proves //LCMD_disable_class_hook() skips the interpreter: if ShouldNotTrigger
// were hooked, leaked_from_disabled_hook would exist and this would link.
pub fun __on_class_parsed(String className) -> String {
    if className == "UserAuto" {
        return "pub fun print_auto_log() -> i32 { return 888; }"
    }
    if className == "ShouldNotTrigger" {
        return "pub fun leaked_from_disabled_hook() -> i32 { return 1; }"
    }
    return ""
}

//LCMD_enable_class_hook()

class UserAuto {
    i32 id
}

//LCMD_disable_class_hook()

class ShouldNotTrigger {
    i32 x
}

fun main() -> i32 {
    return leaked_from_disabled_hook()
}

//------------------------------------------------
// tests/cases/optional_test.l
//------------------------------------------------
fun main() -> i32 {
    opt1 := Optional<i32>(true, 42)
    print(opt1.unwrap_or(0))

    opt2 := Optional<i32>(false, 0)
    print(opt2.unwrap_or(99))
    return 0
}

//------------------------------------------------
// tests/cases/partial_test.l
//------------------------------------------------
partial class User {
    i32 id
    fun get_id() -> i32 {
        return id
    }
}

partial class User {
    String name
    fun init(i32 i, String n) {
        id = i
        name = n
    }
}

fun main() -> i32 {
    u := User(7, "Alice")
    print(u.get_id())
    print(u.name)
    return 0
}

//------------------------------------------------
// tests/cases/path_fs_ext_test.l
//------------------------------------------------
fun main() -> i32 {
    p := path_join(String("a"), String("b.txt"))
    print(p)
    print(path_basename(p))
    print(path_dirname(String("a/b/c.txt")))
    print(path_ext(String("a/b/c.txt")))

    dir := String("build/clang-debug/test_out/path_fs_ext_dir")
    create_dir(dir)
    f1 := path_join(dir, String("one.txt"))
    f2 := path_join(dir, String("two.txt"))
    write_all(f1, String("hi"))
    copy_file(f1, f2)
    if file_exists(f2) == false {
        print("fail copy")
        return 1
    }
    f3 := path_join(dir, String("three.txt"))
    rename_path(f2, f3)
    listing := read_dir(dir)
    if listing.is_ok() == false {
        print("fail read_dir")
        return 1
    }
    names := listing.unwrap()
    print(from(names.len()))
    remove_file(f1)
    remove_file(f3)
    remove_dir(dir)
    print(String("path_fs_ok"))
    return 0
}

//------------------------------------------------
// tests/cases/pkg_cycle/main.l
//------------------------------------------------
fun main() -> i32 {
    return 0
}

//------------------------------------------------
// tests/cases/pkg_test/main.l
//------------------------------------------------
import math_lib

fun main() -> i32 {
    print(math_lib.add(10, 20))
    return 0
}

//------------------------------------------------
// tests/cases/print_format.l
//------------------------------------------------
fun main() -> i32 {
    i64 big_num = 9223372036854775807
    f64 pi = 3.14159
    bool is_ok = true
    String name = "L-Lang"

    print(big_num)
    print(pi)
    print(is_ok)
    print(name)

    return 0
}

//------------------------------------------------
// tests/cases/process_capture_test.l
//------------------------------------------------
fun main() -> i32 {
    res := run_capture_stdout(String("echo hello_capture"))
    if res.is_ok() == false {
        print("fail launch")
        return 1
    }
    raw_out := res.unwrap()
    out := raw_out.trim()
    print(out)
    needle := String("hello_capture")
    if out.contains(needle) == false {
        print("fail stdout")
        return 1
    }
    print(String("capture_ok"))
    return 0
}

//------------------------------------------------
// tests/cases/process_test.l
//------------------------------------------------
fun main() -> i32 {
    cmd := "echo L-Process-Test"
    res := run_command(cmd)

    if res.is_ok() {
        print("Command executed successfully.")
    } else {
        print("Command failed: " + res.unwrap_err())
    }
    return 0
}

//------------------------------------------------
// tests/cases/property_fallback.l
//------------------------------------------------
class User {
    // Case A: getter/setter present → method call
    i32 score
    pub fun get_score() -> i32 {
        return score
    }
    pub fun set_score(i32 v) {
        score = v + 1
    }

    // Case B: no accessor, pub field → fallback to level_
    pub i32 level
}

fun main() -> i32 {
    u := User()

    // 1. Has setter
    u.score = 10
    print(u.score)

    // 2. Pub field fallback
    u.level = 99
    print(u.level)
    return 0
}

//------------------------------------------------
// tests/cases/ref_bad.l
//------------------------------------------------
class Box {
    i32 val
    fun init(i32 v) {
        val = v
    }
}

fun try_modify(Box b) {
    b.val = 99
    print(b.val)
}

fun main() -> i32 {
    b := Box(10)
    try_modify(b)
    return 0
}

//------------------------------------------------
// tests/cases/ref_pass.l
//------------------------------------------------
class Box {
    i32 val
    fun init(i32 v) {
        val = v
    }
}

fun try_modify(Box b) {
    print(b.val)
}

fun real_modify(Box b!) {
    b.val = 99
}

fun main() -> i32 {
    b := Box(10)
    try_modify(b)
    real_modify(b)
    print(b.val)
    return 0
}

//------------------------------------------------
// tests/cases/regex_test.l
//------------------------------------------------
fun main() -> i32 {
    text := "My email is test@example.com"

    // Substring / literal match
    if contains_pattern(text, "example") {
        print("Found example!")
    }

    // Simple class + quantifier pattern
    if contains_pattern(text, "[a-z]+@[a-z]+") {
        print("Found email pattern!")
    }

    return 0
}

//------------------------------------------------
// tests/cases/result_test.l
//------------------------------------------------
fun safe_div(i32 a, i32 b) -> Result<i32, String> {
    if b == 0 {
        return Result<i32, String>(false, 0, String("Division by zero"))
    }
    return Result<i32, String>(true, a / b, String(""))
}

fun main() -> i32 {
    res1 := safe_div(10, 2)
    print(res1.unwrap())

    res2 := safe_div(10, 0)
    print(res2.unwrap_err())

    return 0
}

//------------------------------------------------
// tests/cases/run_hello.l
//------------------------------------------------
fun main() -> i32 {
    print(42)
    return 0
}

//------------------------------------------------
// tests/cases/sort_test.l
//------------------------------------------------
fun bubble_sort(Array<i32> arr!) {
    n := arr.len()
    i! := 0
    while i < n {
        j! := 0
        while j < n - i - 1 {
            a := arr.get(j)
            b := arr.get(j + 1)
            if a > b {
                arr.set(j, b)
                arr.set(j + 1, a)
            }
            j! += 1
        }
        i! += 1
    }
}

fun main() -> i32 {
    arr := [5, 2, 9, 1, 5, 6]
    bubble_sort(arr)

    k! := 0
    while k < arr.len() {
        print(arr.get(k))
        k! += 1
    }
    return 0
}

//------------------------------------------------
// tests/cases/static_method_bad.l
//------------------------------------------------
class Bad {
    i32 x

    static fun boom() -> i32 {
        return x
    }
}

fun main() -> i32 {
    return Bad.boom()
}

//------------------------------------------------
// tests/cases/static_method_test.l
//------------------------------------------------
class MathUtil {
    static pub fun add(i32 a, i32 b) -> i32 {
        return a + b
    }
}

fun main() -> i32 {
    // Call via class name — no instance required.
    res := MathUtil.add(10, 20)
    print(res)
    return 0
}

//------------------------------------------------
// tests/cases/string_concat.l
//------------------------------------------------
fun main() -> i32 {
    a := String("Hello ")
    b := String("L!")
    c := a + b
    c.printSelf()
    return 0
}

//------------------------------------------------
// tests/cases/string_ext_test.l
//------------------------------------------------
fun main() -> i32 {
    s := String("  Hello World  ")
    t := s.trim()
    hello := String("Hello")
    world := String("World")
    if t.contains(hello) == false {
        print("fail contains")
        return 1
    }
    if t.starts_with(hello) == false {
        print("fail starts")
        return 1
    }
    if t.ends_with(world) == false {
        print("fail ends")
        return 1
    }
    r := t.replace(world, String("L"))
    print(r)
    csv := String("a,b,c")
    parts := csv.split(44)
    dash := String("-")
    // Manual join (stdlib str_join deferred — Array.get type infer edge).
    a0 := parts.get(0)
    a1 := parts.get(1)
    a2 := parts.get(2)
    t0 := a0.concat(dash)
    t1 := t0.concat(a1)
    t2 := t1.concat(dash)
    j := t2.concat(a2)
    print(j)
    print(from_i64(42))
    print(str_from_bool(true))
    return 0
}

//------------------------------------------------
// tests/cases/string_interp.l
//------------------------------------------------
fun main() -> i32 {
    name := "L Language"
    version := 1

    // 测试模板字符串（包含变量和表达式）
    greeting := "Welcome to {name}, version {version}!"
    print(greeting)

    a := 10
    b := 20
    calc := "The sum is {a + b}."
    print(calc)
    return 0
}

//------------------------------------------------
// tests/cases/string_test.l
//------------------------------------------------
fun main() -> i32 {
    s := String("Hello L!")
    s.printSelf()
    return 0
}

//------------------------------------------------
// tests/cases/sync_fail.l
//------------------------------------------------
fun main() -> i32 {
    mut_count! := 0
    spawn {
        mut_count! = 1
    }
    return 0
}

//------------------------------------------------
// tests/cases/sync_pass.l
//------------------------------------------------
fun main() -> i32 {
    mut_count! := 0
    sync {
        mut_count! = 1
    }
    print(mut_count)
    return 0
}

//------------------------------------------------
// tests/cases/thread_test.l
//------------------------------------------------
fun main() -> i32 {
    i32 counter! = 0

    // Background thread mutates shared state under sync; join the handle explicitly.
    h := spawn {
        sync {
            counter! = counter + 1
        }
    }
    l_thread_join(h)

    print(counter)
    return 0
}

//------------------------------------------------
// tests/cases/time_format_test.l
//------------------------------------------------
fun main() -> i32 {
    now := time_now()
    formatted := format_time(now, "%Y-%m-%d %H:%M:%S")
    print("Current time: " + formatted)

    // Epoch 0 → 1969 or 1970 depending on local timezone.
    epoch := format_time(0, "%Y")
    print("Epoch year: " + epoch)
    return 0
}

//------------------------------------------------
// tests/cases/time_test.l
//------------------------------------------------
fun main() -> i32 {
    start := time_now_ms()

    // Sleep ~100 ms
    sleep(100)

    end := time_now_ms()
    diff := end - start

    // i64 prints directly (String + i64 concat not yet supported).
    print(start)
    print(diff)

    if diff >= 100 && diff < 500 {
        print("Sleep works!")
    } else {
        print("Time anomaly!")
    }
    return 0
}

//------------------------------------------------
// tests/cases/timer_test.l
//------------------------------------------------
fun main() -> i32 {
    print("Start")
    start := time_now_ms()

    // Delayed callback (trailing zero-arg block; may capture `start`).
    delay(100) {
        end := time_now_ms()
        print("Timer fired after " + (end - start) + " ms")
        print("Timer callback executed!")
    }

    print("End of main")
    // Keep main alive so the background thread can finish.
    sleep(200)
    return 0
}

//------------------------------------------------
// tests/cases/to_string_test.l
//------------------------------------------------
class Point {
    i32 x
    i32 y
    fun init(i32 a, i32 b) {
        x = a
        y = b
    }
}

fun main() -> i32 {
    p := Point(10, 20)
    print(p.to_string())
    return 0
}

//------------------------------------------------
// tests/cases/try_operator.l
//------------------------------------------------
fun safe_div(i32 a, i32 b) -> Result<i32, String> {
    if b == 0 {
        return Result<i32, String>(false, 0, "Div by zero")
    }
    return Result<i32, String>(true, a / b, "")
}

// Using ? requires returning Result — errors propagate upward.
fun calc(i32 x) -> Result<i32, String> {
    val := safe_div(10, x)?
    return Result<i32, String>(true, val + 5, "")
}

fun main() -> i32 {
    r1 := calc(2)
    print(r1.unwrap())

    r2 := calc(0)
    print(r2.unwrap_err())
    return 0
}

//------------------------------------------------
// tests/cases/type_promotion.l
//------------------------------------------------
fun main() -> i32 {
    a32 := 10
    i64 b64 = 20
    c_float := 3.14

    // i32 + i64 -> 自动提升为 i64
    res1 := a32 + b64
    print(res1)

    // i32 * f64 -> 自动提升为 f64
    res2 := a32 * c_float
    print(res2)

    return 0
}

//------------------------------------------------
// tests/cases/var_decl.l
//------------------------------------------------
constd a = 10
i32 b = 20
c := 30

//------------------------------------------------
// tests/cases/variant_ext_test.l
//------------------------------------------------
fun main() -> i32 {
    v1 := Variant.from_f64(3.14)
    v2 := Variant.from_bool(true)

    if v1.is_f64() {
        opt_f := v1.as_f64()
        print(opt_f.unwrap_or(0.0))
    }
    if v2.is_bool() {
        opt_b := v2.as_bool()
        print(opt_b.unwrap_or(false))
    }

    // Wrong-tag check
    if !v1.is_bool() {
        print("v1 is not bool")
    }
    return 0
}

//------------------------------------------------
// tests/cases/variant_test.l
//------------------------------------------------
fun main() -> i32 {
    v1 := Variant.from_i32(42)
    v2 := Variant.from_string("Hello")

    // Type check
    if v1.is_i32() {
        print("v1 is i32")
    }

    // Extract matching tag
    x := v1.as_i32()
    print(x.unwrap_or(0))

    // Extract mismatch → empty Optional
    y := v2.as_i32()
    print(y.unwrap_or(99))

    return 0
}
