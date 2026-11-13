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
