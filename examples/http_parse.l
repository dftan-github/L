// Minimal HTTP response parser — pure string processing, no sockets.
//
//   split_head(response) -> Array<String>   // CRLF-split lines
//   parse_status(response) -> i32
//   parse_headers(response) -> CPropertys   // name -> Variant(String)
//   header_text(props, name) -> String      // "" when absent

// Split a raw HTTP response into lines. HTTP uses CRLF; we split on the bare
// CR (13) and let trim() drop the trailing LF.
pub fun split_head(String response) -> Array<String> {
    return response.split(13)
}

// Numeric status code: "HTTP/1.1 200 OK" -> 200.
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

// Walk the header block (line 1..blank) and store each "Name: value" pair as a
// Variant in CPropertys. The first ':' splits name from value, so URLs with
// ports ("http://host:8080/x") keep their colons in the value.
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

// Header value as text; "" when the header is absent or not a String.
// Uses `?? ""` on the two Optional layers: the lookup and the Variant tag.
pub fun header_text(CPropertys props, String name) -> String {
    hit := props.get(name) ?? Variant.from_string(String(""))
    return hit.as_string() ?? String("")
}

fun main() -> i32 {
    raw := "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 18\r\n\r\n{\"ok\":true}"

    print(parse_status(raw))

    headers := parse_headers(raw)
    print(header_text(headers, "Content-Type"))
    print(header_text(headers, "Content-Length"))
    return 0
}
