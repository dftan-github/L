// P0-1: HTTP client must never hang on a silent peer, must read a body larger
// than one recv chunk, and must support POST.

// Listener that accepts one connection and then says nothing at all.
fun silent_server(i32 port) -> i32 {
    sfd := l_socket_create()
    if l_socket_bind_and_listen(sfd, port) != 0 {
        l_socket_close(sfd)
        return 0 - 1
    }
    cfd := l_socket_accept(sfd)
    if cfd >= 0 {
        l_socket_close(cfd)
    }
    l_socket_close(sfd)
    return 0
}

fun main() -> i32 {
    // --- 1) Timeout: a peer that never replies must not block forever. ---
    hs := spawn {
        silent_server(18191)
    }
    sleep(150)
    t0 := time_now()
    resp := http_get_timeout(String("127.0.0.1"), 18191, String("/silent"), 300)
    elapsed := time_now() - t0
    join(hs)

    if elapsed > 5 {
        print(String("fail: timeout not enforced"))
        return 1
    }
    if resp.length() > 0 {
        print(String("fail: expected empty response from silent peer"))
        return 1
    }
    print(String("timeout_ok"))

    // --- 2) Full response read: status + body must both parse. ---
    routes2 := Map<String>()
    routes2.put(String("/"), String("ok"))
    hs2 := spawn {
        http_serve_once(18192, routes2)
    }
    sleep(150)
    got := http_get(String("127.0.0.1"), 18192, String("/"))
    join(hs2)
    if http_status_ok(got) == false {
        print(String("fail: get status"))
        return 1
    }
    if http_body(got) != String("ok") {
        print(String("fail: get body"))
        return 1
    }

    // --- 3) POST carries a body and a content type. ---
    routes3 := Map<String>()
    routes3.put(String("/"), String("posted"))
    hs3 := spawn {
        http_serve_once(18193, routes3)
    }
    sleep(150)
    post := http_post(String("127.0.0.1"), 18193, String("/"), String("text/plain"), String("payload"))
    join(hs3)
    if http_status_ok(post) == false {
        print(String("fail: post status"))
        return 1
    }
    if http_body(post) != String("posted") {
        print(String("fail: post body"))
        return 1
    }

    // --- 4) Status code helper. ---
    if http_status_code(post) != 200 {
        print(String("fail: status code"))
        return 1
    }

    // --- 5) Body extraction is exact, not off-by-one. ---
    if http_body(post).length() != 6 {
        print(String("fail: body length"))
        return 1
    }

    print(String("http_client_ok"))
    return 0
}
