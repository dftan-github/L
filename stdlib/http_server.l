// Minimal HTTP/1.1 server (one request per accept).

fun http_index_of_char(String s, i32 ch) -> i32 {
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

fun http_first_line(String req) -> String {
    n := req.length()
    nl := http_index_of_char(req, 10)
    if nl < 0 {
        ptr p0 = l_str_substr(req.c_str(), 0, n)
        return String(p0)
    }
    end! := nl
    if end > 0 {
        if req.char_at(end - 1) == 13 {
            end! = end - 1
        }
    }
    ptr p = l_str_substr(req.c_str(), 0, end)
    return String(p)
}

fun http_make_response(String status, String body) -> String {
    nl := String("\r\n")
    len_s := from(body.length())
    h1 := status.concat(nl)
    cl := String("Content-Length: ")
    h2 := cl.concat(len_s)
    h3 := h2.concat(nl)
    h4 := h3.concat(nl)
    h5 := h1.concat(h4)
    return h5.concat(body)
}

fun http_request_path(String line) -> String {
    parts := line.split(32)
    if parts.len() >= 2 {
        // Avoid Array.get type-infer pitfall: rebuild path from indices via c_str scan.
        // GET /path HTTP/1.1 — path is between first and second space.
        sp1 := http_index_of_char(line, 32)
        if sp1 < 0 {
            return String("/")
        }
        rest_start := sp1 + 1
        // find next space from rest_start
        n := line.length()
        j! := rest_start
        sp2! := 0 - 1
        while j < n {
            if line.char_at(j) == 32 {
                sp2! = j
                j! = n
            } else {
                j! = j + 1
            }
        }
        if sp2 < 0 {
            ptr p = l_str_substr(line.c_str(), rest_start, n - rest_start)
            return String(p)
        }
        ptr p2 = l_str_substr(line.c_str(), rest_start, sp2 - rest_start)
        return String(p2)
    }
    return String("/")
}

pub fun http_handle_client(i32 cfd, Map<String> routes) -> i32 {
    buf := l_alloc(2048)
    n := l_socket_recv(cfd, buf, 2048)
    if n <= 0 {
        l_free(buf)
        l_socket_close(cfd)
        return 0 - 1
    }

    req := String(buf)
    line := http_first_line(req)
    path := http_request_path(line)

    body_opt := routes.get(path)
    body := String("not found")
    status := String("HTTP/1.1 404 Not Found")
    if body_opt.is_some() {
        // Rebind via fresh locals (avoid mutating typed String slots).
        ok_body := body_opt.unwrap()
        ok_status := String("HTTP/1.1 200 OK")
        resp_ok := http_make_response(ok_status, ok_body)
        l_socket_send(cfd, resp_ok.c_str(), resp_ok.length())
        l_free(buf)
        l_socket_close(cfd)
        return 0
    }
    resp := http_make_response(status, body)
    l_socket_send(cfd, resp.c_str(), resp.length())
    l_free(buf)
    l_socket_close(cfd)
    return 0
}

pub fun http_serve(i32 port, Map<String> routes) -> i32 {
    l_install_shutdown_handler()
    sfd := l_socket_create()
    if l_socket_bind_and_listen(sfd, port) != 0 {
        l_socket_close(sfd)
        return 0 - 1
    }
    while l_is_running() != 0 {
        cfd := l_socket_accept(sfd)
        if cfd >= 0 {
            http_handle_client(cfd, routes)
        }
    }
    l_socket_close(sfd)
    return 0
}

pub fun http_serve_once(i32 port, Map<String> routes) -> i32 {
    sfd := l_socket_create()
    if l_socket_bind_and_listen(sfd, port) != 0 {
        l_socket_close(sfd)
        return 0 - 1
    }
    cfd := l_socket_accept(sfd)
    if cfd < 0 {
        l_socket_close(sfd)
        return 0 - 1
    }
    http_handle_client(cfd, routes)
    l_socket_close(sfd)
    return 0
}
