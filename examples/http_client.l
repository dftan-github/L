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
