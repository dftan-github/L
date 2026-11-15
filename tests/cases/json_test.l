// `json.l` is self-contained (it pulls in memory/io/array/string/optional/map/
// variant/result/properties), so importing it twice via `properties.l` would
// redefine its functions — list only what is not already reached through it.
import "stdlib/json.l"

// JSON text is assembled from byte codes: `{`/`}` inside a string literal would
// be parsed as template interpolation delimiters.
fun lb() -> String {
    return String(l_str_from_byte(123))
}

fun rb() -> String {
    return String(l_str_from_byte(125))
}

fun it(String s) -> String {
    return String(l_str_from_byte(91)) + s + String(l_str_from_byte(93))
}

fun qu(String s) -> String {
    String q = String(l_str_from_byte(34))
    return q + s + q
}

// Prints `ok <label>` / `fail <label>` and returns 1 on mismatch.
fun expect(String label, String got, String want) -> i32 {
    if got == want {
        print("ok " + label)
        return 0
    }
    print("fail " + label + ": got [" + got + "] want [" + want + "]")
    return 1
}

fun expect_parse_error(String label, String text) -> i32 {
    r := parse(text)
    if r.is_ok() {
        print("fail " + label + ": expected an error")
        return 1
    }
    print("ok " + label)
    return 0
}

// Round-trips `text` through parse + stringify and compares with `want`.
fun round_trip(String label, String text, String want) -> i32 {
    r := parse(text)
    if !r.is_ok() {
        print("fail " + label + ": " + r.unwrap_err())
        return 1
    }
    return expect(label, stringify(r.unwrap()), want)
}

fun main() -> i32 {
    O := lb()
    C := rb()
    i32 bad = 0

    // 1. Three-level nesting with an array — the A448 acceptance shape.
    nested := O + qu("a") + String(":") + O + qu("b") + String(":") + it("1,2") + C + C
    bad = bad + round_trip("nested3", nested, nested)

    // 2. Array order must follow the numeric keys, not the hash order.
    bad = bad + round_trip("array_order", it("10,20,30,40,50"), it("10,20,30,40,50"))

    // 3. Scalar kinds: string / i32 / f64 / bool / null. Object member order is
    //    the hash-map order, so members are checked one by one.
    scalars := O + qu("s") + String(":") + qu("hi") + String(",") +
               qu("i") + String(":42,") + qu("f") + String(":1.5,") +
               qu("t") + String(":true,") + qu("n") + String(":false,") +
               qu("z") + String(":null") + C
    sr := parse(scalars)
    if !sr.is_ok() {
        print("fail scalars: " + sr.unwrap_err())
        bad = bad + 1
    } else {
        root := sr.unwrap()
        bad = bad + expect("scalar_string",
                           stringify(root.get_property("s").unwrap()), qu("hi"))
        bad = bad + expect("scalar_i32",
                           stringify(root.get_property("i").unwrap()), "42")
        bad = bad + expect("scalar_f64",
                           stringify(root.get_property("f").unwrap()), "1.5")
        bad = bad + expect("scalar_true",
                           stringify(root.get_property("t").unwrap()), "true")
        bad = bad + expect("scalar_false",
                           stringify(root.get_property("n").unwrap()), "false")
        bad = bad + expect("scalar_null",
                           stringify(root.get_property("z").unwrap()), "null")
    }

    // 4. Whitespace is insignificant.
    bad = bad + round_trip("whitespace", "  " + O + "\r\n " + qu("a") + " : " + it(" 1 , 2 ") +
                                           " \t" + C + "\n ",
                           O + qu("a") + String(":") + it("1,2") + C)

    // 5. Empty containers (member order is hash order, so check by key).
    empty := O + qu("o") + String(":") + O + C + String(",") +
             qu("e") + String(":") + it("") + C
    er := parse(empty)
    if !er.is_ok() {
        print("fail empty: " + er.unwrap_err())
        bad = bad + 1
    } else {
        eroot := er.unwrap()
        bad = bad + expect("empty_object", stringify(eroot.get_property("o").unwrap()), O + C)
        bad = bad + expect("empty_array", stringify(eroot.get_property("e").unwrap()), it(""))
    }

    // 6. Escapes round-trip (\n, \t, \" and \\).
    esc := O + qu("k") + String(":") + qu("a\\nb\\tc\\\"d\\\\e") + C
    bad = bad + round_trip("escapes", esc, esc)

    // 7. Negative numbers and fractions.
    bad = bad + round_trip("negatives", it("-1,-2.25,0.5"), it("-1,-2.25,0.5"))

    // 8. Arrays nested inside arrays.
    bad = bad + round_trip("array_in_array", O + qu("m") + ":" + it(it("1,2") + "," + it("3")) + C,
                           O + qu("m") + ":" + it(it("1,2") + "," + it("3")) + C)

    // 9. Typed access through the tree (not just the serialized text).
    r := parse(O + qu("db") + ":" + O + qu("pool") + ":" + O + qu("max") + ":64" + C + C + C)
    if !r.is_ok() {
        print("fail tree_access: " + r.unwrap_err())
        bad = bad + 1
    } else {
        leaf := r.unwrap().get_path("db.pool.max")
        if !leaf.is_some() {
            print("fail tree_access: path missing")
            bad = bad + 1
        } else {
            v := leaf.unwrap().get_val()
            if !v.is_some() {
                print("fail tree_access: no payload")
                bad = bad + 1
            } else {
                bad = bad + expect("tree_access", String.from(v.unwrap().as_i32().unwrap()), "64")
            }
        }
    }

    // 10. Invalid inputs must fail instead of yielding a partial tree.
    bad = bad + expect_parse_error("err_unclosed_object", O + qu("a") + ":1")
    bad = bad + expect_parse_error("err_trailing", O + C + " " + O + C)
    bad = bad + expect_parse_error("err_missing_colon", O + qu("a") + " 1" + C)
    bad = bad + expect_parse_error("err_trailing_comma", "[1,]")
    bad = bad + expect_parse_error("err_empty_input", "")

    if bad != 0 {
        print("json_fail")
        return 1
    }
    print("json_ok")
    return 0
}
