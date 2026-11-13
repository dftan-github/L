// HTTP/1.1 client helpers (TCP + optional HTTPS via Schannel on Windows).

pub fun http_get(String host, i32 port, String path) -> String {
    i! := 0
    cfd! := 0 - 1
    while i < 40 {
        fd := l_socket_create()
        if l_socket_connect_host(fd, host.c_str(), port) == 0 {
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

    nl := String("\r\n")
    s0 := String("GET ")
    s1 := s0.concat(path)
    s2 := s1.concat(String(" HTTP/1.1"))
    s3 := s2.concat(nl)
    s4 := s3.concat(String("Host: "))
    s5 := s4.concat(host)
    s6 := s5.concat(nl)
    s7 := s6.concat(String("Connection: close"))
    s8 := s7.concat(nl)
    req := s8.concat(nl)

    l_socket_send(cfd, req.c_str(), req.length())

    // Single recv for Phase-1 tests (response fits in 4K).
    buf := l_alloc(4096)
    n := l_socket_recv(cfd, buf, 4096)
    l_socket_close(cfd)
    if n <= 0 {
        l_free(buf)
        return String("")
    }
    ptr copy = l_str_substr(buf, 0, n)
    l_free(buf)
    return String(copy)
}

pub fun https_get(String host, String path) -> String {
    p := l_https_get(host.c_str(), 443, path.c_str())
    if l_ptr_is_null(p) != 0 {
        return String("")
    }
    n := l_str_len(p)
    ptr copy = l_str_substr(p, 0, n)
    l_free(p)
    return String(copy)
}

pub fun http_status_ok(String response) -> bool {
    a := String("HTTP/1.1 200")
    b := String("HTTP/1.0 200")
    if response.starts_with(a) {
        return true
    }
    if response.starts_with(b) {
        return true
    }
    return false
}

pub fun http_body(String response) -> String {
    sep := String("\r\n\r\n")
    idx := response.index_of(sep)
    if idx >= 0 {
        ptr p = l_str_substr(response.c_str(), idx + 4, response.length() - idx - 4)
        return String(p)
    }
    sep2 := String("\n\n")
    idx2 := response.index_of(sep2)
    if idx2 >= 0 {
        ptr p2 = l_str_substr(response.c_str(), idx2 + 2, response.length() - idx2 - 2)
        return String(p2)
    }
    return String("")
}
