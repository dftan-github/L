// Edge cases for the minimal HTTP response parser.
// Each block parses one response and prints the fields under test, so the
// expected output pins the exact behaviour of every branch.

pub fun split_head(String response) -> Array<String> {
    return response.split(13)
}

pub fun parse_status(String response) -> i32 {
    lines := split_head(response)
    if lines.len() == 0 {
        return 0
    }
    line := lines.get(0).trim()
    if line.length() == 0 {
        return 0
    }
    sp := line.index_of(String(" "))
    if sp < 0 {
        return 0
    }
    rest := line.slice(sp + 1, line.length() - sp - 1)
    sp2 := rest.index_of(String(" "))
    if sp2 < 0 {
        return rest.trim().to_i32()
    }
    return rest.slice(0, sp2).trim().to_i32()
}

pub fun parse_headers(String response) -> CPropertys {
    props := CPropertys()
    lines := split_head(response)
    if lines.len() < 2 {
        return props
    }
    i! := 1
    running! := true
    while i < lines.len() && running {
        line := lines.get(i).trim()
        if line.length() == 0 {
            running! = false
        } else {
            colon := line.index_of(String(":"))
            if colon > 0 {
                name := line.slice(0, colon).trim()
                tail := colon + 1
                value := line.slice(tail, line.length() - tail).trim()
                props.set(name, Variant.from_string(value))
            }
            i! = i + 1
        }
    }
    return props
}

pub fun header_text(CPropertys props, String name) -> String {
    hit := props.get(name) ?? Variant.from_string(String(""))
    return hit.as_string() ?? String("")
}

fun main() -> i32 {
    // 1) Normal 200 with two headers
    ok := "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 18\r\n\r\n{\"ok\":true}"
    print(parse_status(ok))
    h1 := parse_headers(ok)
    print(header_text(h1, "Content-Type"))
    print(header_text(h1, "Content-Length"))

    // 2) 404 without Content-Type; missing key must yield ""
    nf := "HTTP/1.1 404 Not Found\r\nServer: L\r\n\r\n"
    print(parse_status(nf))
    print(header_text(parse_headers(nf), "Content-Type"))
    print(header_text(parse_headers(nf), "Server"))

    // 3) Empty response: no status line, no headers
    print(parse_status(""))
    print(header_text(parse_headers(""), "X"))

    // 4) Status line only, no CRLF anywhere
    print(parse_status("HTTP/1.0 500"))

    // 5) ':' inside a header value survives (URL with port)
    col := "HTTP/1.1 201 Created\r\nLocation: http://x/y:8080/z\r\n\r\n"
    print(header_text(parse_headers(col), "Location"))

    // 6) Repeated header: last write wins
    rep := "HTTP/1.1 200 OK\r\nX-A: first\r\nX-A: second\r\n\r\n"
    print(header_text(parse_headers(rep), "X-A"))

    // 7) Whitespace around the name and the value is trimmed
    ws := "HTTP/1.1 200 OK\r\n  X-B  :   padded   \r\n\r\n"
    print(header_text(parse_headers(ws), "X-B"))

    return 0
}
