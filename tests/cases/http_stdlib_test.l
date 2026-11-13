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
