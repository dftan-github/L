// P1-4: chunked decoding, PUT / DELETE verbs, hex parsing edge cases.

// Listener that answers one request with a fixed raw response over TCP.
fun raw_server(i32 port, String payload) -> i32 {
    sfd := l_socket_create()
    if l_socket_bind_and_listen(sfd, port) != 0 {
        l_socket_close(sfd)
        return 0 - 1
    }
    cfd := l_socket_accept(sfd)
    if cfd >= 0 {
        l_socket_send(cfd, payload.c_str(), payload.length())
        l_socket_close(cfd)
    }
    l_socket_close(sfd)
    return 0
}

fun main() -> i32 {
    // --- Hex parsing: valid, mixed case, invalid ---
    if http_hex_to_i32(String("1a")) != 26 {
        print(String("fail: hex lower"))
        return 1
    }
    if http_hex_to_i32(String("1A")) != 26 {
        print(String("fail: hex upper"))
        return 1
    }
    if http_hex_to_i32(String("ff")) != 255 {
        print(String("fail: hex ff"))
        return 1
    }
    if http_hex_to_i32(String("")) >= 0 {
        print(String("fail: empty hex should be negative"))
        return 1
    }
    if http_hex_to_i32(String("zz")) >= 0 {
        print(String("fail: invalid hex should be negative"))
        return 1
    }

    // --- Chunked decode: two chunks + terminator ---
    body := String("5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n")
    got := http_dechunk(body)
    if got != String("hello world") {
        print(got)
        print(String("fail: chunked decode"))
        return 1
    }

    // Empty chunked body ("0\r\n\r\n" only) yields "".
    if http_dechunk(String("0\r\n\r\n")) != String("") {
        print(String("fail: empty chunked"))
        return 1
    }

    // Truncated chunk (declares 10 bytes, supplies 3) returns what exists.
    trunc := http_dechunk(String("a\r\nabc"))
    if trunc != String("abc") {
        print(trunc)
        print(String("fail: truncated chunk should return partial"))
        return 1
    }

    // --- Transfer-Encoding detection ---
    hdr_chunked := String("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n")
    if !http_starts_with_ci(String("Transfer-Encoding: chunked"), String("transfer-encoding:")) {
        print(String("fail: ci prefix"))
        return 1
    }
    if !http_is_chunked(hdr_chunked) {
        print(String("fail: should detect chunked"))
        return 1
    }
    hdr_plain := String("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nhi")
    if http_is_chunked(hdr_plain) {
        print(String("fail: plain should not be chunked"))
        return 1
    }

    // http_body_auto dechunks transparently.
    if http_body_auto(hdr_chunked) != String("hello") {
        print(http_body_auto(hdr_chunked))
        print(String("fail: body_auto dechunk"))
        return 1
    }
    if http_body_auto(hdr_plain) != String("hi") {
        print(String("fail: body_auto plain"))
        return 1
    }

    // --- PUT reaches the server and the response parses ---
    routes := Map<String>()
    routes.put(String("/item"), String("put_ok"))
    hs := spawn {
        http_serve_once(18194, routes)
    }
    sleep(150)    put_resp := http_put(String("127.0.0.1"), 18194, String("/item"), String("text/plain"), String("v"))
    join(hs)
    if !http_status_ok(put_resp) {
        print(String("fail: put status"))
        return 1
    }
    if http_body(put_resp) != String("put_ok") {
        print(String("fail: put body"))
        return 1
    }

    // --- DELETE verb ---
    routes2 := Map<String>()
    routes2.put(String("/item"), String("deleted"))
    hs2 := spawn {
        http_serve_once(18195, routes2)
    }
    sleep(150)
    del_resp := http_delete(String("127.0.0.1"), 18195, String("/item"))
    join(hs2)
    if !http_status_ok(del_resp) {
        print(String("fail: delete status"))
        return 1
    }
    if http_body(del_resp) != String("deleted") {
        print(String("fail: delete body"))
        return 1
    }

    print(String("http_chunked_ok"))
    return 0
}
