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
