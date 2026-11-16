// HTTP/1.1 client helpers (TCP + optional HTTPS via Schannel on Windows).
//
// All request paths take an explicit timeout so a silent peer can never hang the
// caller: l_socket_recv_timeout returns -2 on deadline instead of blocking forever.

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

// Copy n bytes of a NUL-terminated C buffer into a fresh L String.
fun http_take_str(ptr buf, i32 n) -> String {
    if n <= 0 {
        return String("")
    }
    ptr copy = l_str_substr(buf, 0, n)
    return String(copy)
}

// Case-insensitive prefix test for header names (HTTP header names are
// case-insensitive per RFC 7230; servers emit any casing).
pub fun http_starts_with_ci(String s, String prefix) -> bool {
    n := prefix.length()
    if n > s.length() {
        return false
    }
    i32 i = 0
    while i < n {
        c! := s.char_at(i)
        p! := prefix.char_at(i)
        if c >= 65 && c <= 90 {
            c! = c + 32
        }
        if p >= 65 && p <= 90 {
            p! = p + 32
        }
        if c != p {
            return false
        }
        i = i + 1
    }
    return true
}

// Parse `Content-Length: N` out of a header block; -1 when absent or malformed.
fun http_content_length(String response) -> i32 {
    hdr_end := response.index_of(String("\r\n\r\n"))
    head := response
    if hdr_end >= 0 {
        head = response.slice(0, hdr_end)
    }
    lower_key := String("content-length:")
    n := head.length()
    i! := 0
    while i < n {
        // Header names only appear at line starts.
        if i == 0 || head.char_at(i - 1) == 10 {
            rem := head.slice(i, n - i)
            if http_starts_with_ci(rem, lower_key) {
                val := rem.slice(lower_key.length(), rem.length() - lower_key.length())
                colon := val.index_of(String("\r\n"))
                if colon >= 0 {
                    val = val.slice(0, colon)
                }
                return val.trim().to_i32()
            }
        }
        i! = i + 1
    }
    return 0 - 1
}

// Read one full HTTP response: headers until CRLFCRLF, then exactly
// Content-Length body bytes (or until the peer closes when length is absent).
// Accumulates chunk Strings rather than pointer-bumping the raw buffer, so the
// runtime's NUL-termination contract is never violated.
fun http_read_response(i32 fd, i32 timeout_ms) -> String {
    chunk_cap := 8192
    buf := l_alloc(chunk_cap)
    acc := String("")
    header_at! := 0 - 1
    want! := 0 - 1
    eof! := false

    while !eof {
        n := l_socket_recv_timeout(fd, buf, chunk_cap, timeout_ms)
        if n == 0 {
            eof! = true
        }
        if n < 0 {
            // -1 error and -2 timeout both end the read; keep what arrived.
            eof! = true
        }
        if n > 0 {
            acc = acc.concat(http_take_str(buf, n))
            if header_at < 0 {
                h := acc.index_of(String("\r\n\r\n"))
                if h >= 0 {
                    header_at! = h + 4
                    want! = http_content_length(acc)
                }
            }
            if header_at >= 0 && want >= 0 && acc.length() - header_at >= want {
                eof! = true
            }
        }
    }

    l_free(buf)
    return acc
}

// Connect with a bounded retry budget (used for race-free server startup in tests).
fun http_connect(String host, i32 port) -> i32 {
    i! := 0
    while i < 40 {
        fd := l_socket_create()
        if l_socket_connect_host(fd, host.c_str(), port) == 0 {
            return fd
        }
        l_socket_close(fd)
        l_sleep_ms(50)
        i! = i + 1
    }
    return 0 - 1
}

fun http_build_request(String method, String host, String path, String headers, String body) -> String {
    nl := String("\r\n")
    s0 := method.concat(String(" "))
    s1 := s0.concat(path)
    s2 := s1.concat(String(" HTTP/1.1"))
    s3 := s2.concat(nl)
    s4 := s3.concat(String("Host: "))
    s5 := s4.concat(host)
    s6 := s5.concat(nl)
    s7 := s6.concat(String("Connection: close"))
    s8 := s7.concat(nl)
    s9 := s8.concat(String("Content-Length: "))
    s10 := s9.concat(from(body.length()))
    s11 := s10.concat(nl)
    s12 := s11.concat(headers)
    s13 := s12.concat(nl)
    return s13.concat(body)
}

// Core request driver. Returns the raw response text, or "" on failure.
pub fun http_request(String method, String host, i32 port, String path, String headers, String body, i32 timeout_ms) -> String {
    fd := http_connect(host, port)
    if fd < 0 {
        return String("")
    }
    req := http_build_request(method, host, path, headers, body)
    l_socket_send(fd, req.c_str(), req.length())
    resp := http_read_response(fd, timeout_ms)
    l_socket_close(fd)
    return resp
}

// Convenience wrappers — timeout defaults to 10s when the caller omits it.
pub fun http_get(String host, i32 port, String path) -> String {
    return http_request(String("GET"), host, port, path, String(""), String(""), 10000)
}

pub fun http_get_timeout(String host, i32 port, String path, i32 timeout_ms) -> String {
    return http_request(String("GET"), host, port, path, String(""), String(""), timeout_ms)
}

pub fun http_post(String host, i32 port, String path, String content_type, String body) -> String {
    hdr := String("Content-Type: ").concat(content_type).concat(String("\r\n"))
    return http_request(String("POST"), host, port, path, hdr, body, 10000)
}

pub fun http_put(String host, i32 port, String path, String content_type, String body) -> String {
    hdr := String("Content-Type: ").concat(content_type).concat(String("\r\n"))
    return http_request(String("PUT"), host, port, path, hdr, body, 10000)
}

pub fun http_delete(String host, i32 port, String path) -> String {
    return http_request(String("DELETE"), host, port, path, String(""), String(""), 10000)
}

// True when the response headers advertise `Transfer-Encoding: chunked`.
pub fun http_is_chunked(String response) -> bool {
    hdr_end := response.index_of(String("\r\n\r\n"))
    head := response
    if hdr_end >= 0 {
        head = response.slice(0, hdr_end)
    }
    key := String("transfer-encoding:")
    n := head.length()
    i! := 0
    while i < n {
        if i == 0 || head.char_at(i - 1) == 10 {
            rem := head.slice(i, n - i)
            if http_starts_with_ci(rem, key) {
                if http_starts_with_ci(rem.slice(key.length(), rem.length() - key.length()), String(" chunked")) {
                    return true
                }
            }
        }
        i! = i + 1
    }
    return false
}

// Decode `Transfer-Encoding: chunked` body content into the plain payload.
// Malformed length lines stop decoding and return what was read so far —
// a truncated body is better than a hang or a crash.
pub fun http_dechunk(String chunked_body) -> String {
    out := String("")
    n := chunked_body.length()
    pos! := 0
    while pos < n {
        // Read the hex size line.
        nl := chunked_body.index_of(String("\r\n"))
        line := String("")
        if nl >= 0 {
            // index_of finds the first CRLF; we need the one at/after `pos`.
            scan! := pos
            found! := 0 - 1
            while scan < n - 1 {
                if chunked_body.char_at(scan) == 13 && chunked_body.char_at(scan + 1) == 10 {
                    found! = scan
                    scan! = n
                } else {
                    scan! = scan + 1
                }
            }
            if found < 0 {
                return out
            }
            line = chunked_body.slice(pos, found - pos)
            pos = found + 2
        }
        size := http_hex_to_i32(line.trim())
        if size <= 0 {
            // 0 marks the end of the body ("0\r\n\r\n" trailer section).
            return out
        }
        if pos + size > n {
            out = out.concat(chunked_body.slice(pos, n - pos))
            return out
        }
        out = out.concat(chunked_body.slice(pos, size))
        pos = pos + size
        // Skip the CRLF that follows each chunk's data.
        if pos + 1 < n && chunked_body.char_at(pos) == 13 && chunked_body.char_at(pos + 1) == 10 {
            pos = pos + 2
        }
    }
    return out
}

// Hex digits → integer. Returns -1 on any invalid character.
pub fun http_hex_to_i32(String s) -> i32 {
    n := s.length()
    if n == 0 {
        return 0 - 1
    }
    acc := 0
    i32 i = 0
    while i < n {
        c := s.char_at(i)
        d := 0 - 1
        if c >= 48 && c <= 57 {
            d = c - 48
        } else if c >= 97 && c <= 102 {
            d = c - 87
        } else if c >= 65 && c <= 70 {
            d = c - 55
        }
        if d < 0 {
            return 0 - 1
        }
        acc = acc * 16 + d
        i = i + 1
    }
    return acc
}

// Body accessor that transparently dechunks when the response says so.
pub fun http_body_auto(String response) -> String {
    raw := http_body(response)
    if http_is_chunked(response) {
        return http_dechunk(raw)
    }
    return raw
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

// Status code as an integer (0 when the line is malformed).
pub fun http_status_code(String response) -> i32 {
    if response.length() < 12 {
        return 0
    }
    return response.slice(9, 3).to_i32()
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
