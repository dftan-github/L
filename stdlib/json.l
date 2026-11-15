// JSON parser / serializer over CPropertys.
//
// Mapping:
//   object → node with named properties          {"a":1}  → root.a
//   array  → node whose properties are "0".."n"  [1,2]    → root.0, root.1
//   null   → node with no payload and no properties (a bare hole)
//   scalar → node carrying a Variant payload
//
// Scalars use Variant tags 1 (i32), 3 (f64), 2 (String), 4 (bool). Integral
// literals become i32 unless they overflow, so `5432` round-trips as `5432`
// instead of `5432.000000`.
//
// Self-contained: importing this file alone pulls in everything it needs.
import "stdlib/memory.l"
import "stdlib/io.l"
import "stdlib/array.l"
import "stdlib/string.l"
import "stdlib/optional.l"
import "stdlib/map.l"
import "stdlib/variant.l"
import "stdlib/result.l"
import "stdlib/properties.l"

// Byte constants (no raw quote/brace literals in the source).
const CH_QUOTE = 34
const CH_BSLASH = 92
const CH_SLASH = 47
const CH_LBRACE = 123
const CH_RBRACE = 125
const CH_LBRACKET = 91
const CH_RBRACKET = 93
const CH_COLON = 58
const CH_COMMA = 44
const CH_MINUS = 45
const CH_PLUS = 43
const CH_DOT = 46
const CH_E_LOWER = 101
const CH_E_UPPER = 69

// Must stay `pub`: a private module-level helper in this file is emitted as
// `ch` (unmangled) by call sites inside the `JsonScan` methods, and the private
// symbol (`json::ch`) never resolves there — a pre-existing codegen limitation.
pub fun ch(i32 c) -> String {
    return String(l_str_from_byte(c))
}

// --- scanner -------------------------------------------------------------
// A cursor over the source text. One heap cell holds `pos`, so every helper
// shares the same position without returning a tuple (L has no tuples).

pub class JsonScan {
    String src
    ptr pos

    pub fun init(String text) {
        src = text
        pos = l_alloc(sizeof(i32))
        l_store_i32(pos, 0)
    }

    pub fun at() -> i32 {
        return l_load_i32(pos);
    }

    pub fun seek(i32 i) {
        l_store_i32(pos, i)
    }

    pub fun size() -> i32 {
        return src.length();
    }

    // Character at cursor+offset, or -1 past the end.
    pub fun peek(i32 offset) -> i32 {
        i := at() + offset
        if i >= size() {
            return 0 - 1;
        }
        return src.char_at(i);
    }

    pub fun skip_ws() {
        skipping! := true
        while skipping && at() < size() {
            c := src.char_at(at())
            if c == 32 || c == 9 || c == 10 || c == 13 {
                seek(at() + 1)
            } else {
                skipping! = false
            }
        }
    }

    // Cursor on the opening quote: returns the unescaped body and consumes the
    // closing quote.
    pub fun read_string() -> String {
        seek(at() + 1)
        out! := String("")
        while at() < size() {
            c := src.char_at(at())
            if c == CH_QUOTE {
                seek(at() + 1)
                return out;
            }
            if c == CH_BSLASH {
                seek(at() + 1)
                e := src.char_at(at())
                if e == 110 {
                    out! = out.concat(String("\n"))
                } else {
                    if e == 116 {
                        out! = out.concat(String("\t"))
                    } else {
                        if e == 114 {
                            out! = out.concat(String("\r"))
                        } else {
                            // \" \\ \/ keep the escaped character; unknown
                            // escapes keep both bytes verbatim.
                            if e == CH_QUOTE || e == CH_BSLASH || e == CH_SLASH {
                                out! = out.concat(ch(e))
                            } else {
                                out! = out.concat(ch(CH_BSLASH)).concat(ch(e))
                            }
                        }
                    }
                }
                seek(at() + 1)
            } else {
                out! = out.concat(ch(c))
                seek(at() + 1)
            }
        }
        return out;
    }

    // Scans a number / true / false / null token and returns its raw text.
    pub fun read_token() -> String {
        start := at()
        while at() < size() {
            c := src.char_at(at())
            if is_digit(c) || is_alpha(c) || c == CH_MINUS || c == CH_PLUS ||
               c == CH_DOT {
                seek(at() + 1)
            } else {
                return src.slice(start, at() - start);
            }
        }
        return src.slice(start, at() - start);
    }
}

// --- scalar decoding -----------------------------------------------------

// No strtod in the C runtime, so decimal text is folded by hand. Only digits
// before the first '.'/exponent are integral.
fun is_integral_text(String t) -> bool {
    if t.length() == 0 {
        return false;
    }
    i32 i = 0
    while i < t.length() {
        c := t.char_at(i)
        if i == 0 && (c == CH_MINUS || c == CH_PLUS) {
            if t.length() == 1 {
                return false;
            }
        } else {
            if !is_digit(c) {
                return false;
            }
        }
        i = i + 1
    }
    return true;
}

fun frac_digits(String t, i32 start, i32 stop) -> f64 {
    v! := 0.0
    scale! := 0.1
    i32 i = start
    while i < stop {
        digit := t.char_at(i) - 48
        v! = v + digit * scale
        scale! = scale * 0.1
        i = i + 1
    }
    return v;
}

fun parse_number(String tok) -> f64 {
    n := tok.length()
    i32 i = 0
    negative! := false
    if i < n && (tok.char_at(i) == CH_MINUS || tok.char_at(i) == CH_PLUS) {
        negative! = tok.char_at(i) == CH_MINUS
        i = i + 1
    }
    mantissa! := 0.0
    int_start := i
    while i < n && is_digit(tok.char_at(i)) {
        i = i + 1
    }
    if i > int_start {
        digits := tok.slice(int_start, i - int_start)
        mantissa! = digits.to_i32() * 1.0
    }
    if i < n && tok.char_at(i) == CH_DOT {
        i = i + 1
        frac_start := i
        while i < n && is_digit(tok.char_at(i)) {
            i = i + 1
        }
        mantissa! = mantissa + frac_digits(tok, frac_start, i)
    }
    if i < n && (tok.char_at(i) == CH_E_LOWER || tok.char_at(i) == CH_E_UPPER) {
        i = i + 1
        exp_negative! := false
        if i < n && (tok.char_at(i) == CH_MINUS || tok.char_at(i) == CH_PLUS) {
            exp_negative! = tok.char_at(i) == CH_MINUS
            i = i + 1
        }
        exp_start := i
        while i < n && is_digit(tok.char_at(i)) {
            i = i + 1
        }
        if i > exp_start {
            e := tok.slice(exp_start, i - exp_start).to_i32()
            i32 k = 0
            while k < e {
                if exp_negative {
                    mantissa! = mantissa / 10.0
                } else {
                    mantissa! = mantissa * 10.0
                }
                k = k + 1
            }
        }
    }
    if negative {
        return 0.0 - mantissa;
    }
    return mantissa;
}

// Tag 0 marks JSON null; the caller leaves such a node payload-less.
// (Result<Variant, ...> is avoided: a two-word first argument does not parse.)
fun parse_scalar(String tok) -> Variant {
    if tok == "null" {
        return Variant(0, l_null_ptr());
    }
    if tok == "true" {
        return Variant.from_bool(true);
    }
    if tok == "false" {
        return Variant.from_bool(false);
    }
    if is_integral_text(tok) {
        return Variant.from_i32(tok.to_i32());
    }
    return Variant.from_f64(parse_number(tok));
}

// --- parser --------------------------------------------------------------
// The recursive helpers return `Result<bool, String>`: `true` = ok.

fun parse_object(JsonScan sc, CPropertys node, i32 depth) -> Result<bool, String> {
    node.set_kind(1)
    sc.seek(sc.at() + 1)
    sc.skip_ws()
    if sc.peek(0) == CH_RBRACE {
        sc.seek(sc.at() + 1)
        return Result<bool, String>(true, true, String(""));
    }
    while true {
        sc.skip_ws()
        if sc.peek(0) != CH_QUOTE {
            return Result<bool, String>(false, false, String("json: expected object key"));
        }
        key := sc.read_string()
        sc.skip_ws()
        if sc.peek(0) != CH_COLON {
            return Result<bool, String>(false, false, String("json: expected ':'"));
        }
        sc.seek(sc.at() + 1)
        child := CPropertys()
        node.add_property(key, child)
        inner := parse_value(sc, child, depth + 1)
        if !inner.is_ok() {
            return Result<bool, String>(false, false, inner.unwrap_err());
        }
        sc.skip_ws()
        c := sc.peek(0)
        if c == CH_RBRACE {
            sc.seek(sc.at() + 1)
            return Result<bool, String>(true, true, String(""));
        }
        if c != CH_COMMA {
            return Result<bool, String>(false, false, String("json: expected ',' or '}'"));
        }
        sc.seek(sc.at() + 1)
    }
    return Result<bool, String>(false, false, String("json: internal"));
}

fun parse_array(JsonScan sc, CPropertys node, i32 depth) -> Result<bool, String> {
    node.set_kind(2)
    sc.seek(sc.at() + 1)
    sc.skip_ws()
    if sc.peek(0) == CH_RBRACKET {
        sc.seek(sc.at() + 1)
        return Result<bool, String>(true, true, String(""));
    }
    i32 index = 0
    while true {
        child := CPropertys()
        node.add_property(String.from(index), child)
        inner := parse_value(sc, child, depth + 1)
        if !inner.is_ok() {
            return Result<bool, String>(false, false, inner.unwrap_err());
        }
        index = index + 1
        sc.skip_ws()
        c := sc.peek(0)
        if c == CH_RBRACKET {
            sc.seek(sc.at() + 1)
            return Result<bool, String>(true, true, String(""));
        }
        if c != CH_COMMA {
            return Result<bool, String>(false, false, String("json: expected ',' or ']'"));
        }
        sc.seek(sc.at() + 1)
    }
    return Result<bool, String>(false, false, String("json: internal"));
}

// Parses the value at the cursor into `node`. `depth` guards against runaway
// nesting from hostile input.
fun parse_value(JsonScan sc, CPropertys node, i32 depth) -> Result<bool, String> {
    if depth > 64 {
        return Result<bool, String>(false, false, String("json: nesting too deep"));
    }
    sc.skip_ws()
    c := sc.peek(0)
    if c < 0 {
        return Result<bool, String>(false, false, String("json: unexpected end of input"));
    }
    if c == CH_LBRACE {
        return parse_object(sc, node, depth);
    }
    if c == CH_LBRACKET {
        return parse_array(sc, node, depth);
    }
    if c == CH_QUOTE {
        node.set_val(Variant.from_string(sc.read_string()))
        return Result<bool, String>(true, true, String(""));
    }
    tok := sc.read_token()
    if tok.length() == 0 {
        return Result<bool, String>(false, false, String("json: unexpected character"));
    }
    scalar := parse_scalar(tok)
    // Tag 0 is JSON null: leave the node payload-less so it stays a hole.
    if scalar.type_tag != 0 {
        node.set_val(scalar)
    }
    return Result<bool, String>(true, true, String(""));
}

// --- public parse entry --------------------------------------------------

pub fun parse(String text) -> Result<CPropertys, String> {
    root := CPropertys()
    sc := JsonScan(text)
    r := parse_value(sc, root, 0)
    if !r.is_ok() {
        return Result<CPropertys, String>(false, root, r.unwrap_err());
    }
    sc.skip_ws()
    if sc.at() != sc.size() {
        return Result<CPropertys, String>(false, root, String("json: trailing characters"));
    }
    return Result<CPropertys, String>(true, root, String(""));
}

// --- serializer ----------------------------------------------------------

fun escape_json(String s) -> String {
    out! := String("")
    bs := ch(CH_BSLASH)
    i32 i = 0
    while i < s.length() {
        c := s.char_at(i)
        if c == CH_QUOTE {
            out! = out.concat(bs).concat(ch(CH_QUOTE))
        } else {
            if c == CH_BSLASH {
                out! = out.concat(bs).concat(bs)
            } else {
                if c == 10 {
                    out! = out.concat(String("\\n"))
                } else {
                    if c == 9 {
                        out! = out.concat(String("\\t"))
                    } else {
                        if c == 13 {
                            out! = out.concat(String("\\r"))
                        } else {
                            out! = out.concat(ch(c))
                        }
                    }
                }
            }
        }
        i = i + 1
    }
    return out;
}

// An array-shaped node has exactly the keys "0".."n-1"; anything else is an
// object. O(n) and allocation-free, so the writer decides inline.
fun looks_like_array(CPropertys node) -> bool {
    n := node.property_count()
    if n == 0 {
        return false;
    }
    i32 i = 0
    while i < n {
        if !node.has_property(String.from(i)) {
            return false;
        }
        i = i + 1
    }
    return true;
}

fun stringify_value(CPropertys node) -> String {
    if node.has_value() {
        v := node.get_val().unwrap()
        if v.is_string() {
            return ch(CH_QUOTE).concat(escape_json(v.as_string().unwrap())).concat(ch(CH_QUOTE));
        }
        if v.is_bool() {
            return str_from_bool(v.as_bool().unwrap());
        }
        if v.is_f64() {
            return str_from_f64(v.as_f64().unwrap());
        }
        if v.is_i32() {
            return String.from(v.as_i32().unwrap());
        }
    }
    n := node.property_count()
    if n == 0 {
        // Kind disambiguates `{}` / `[]` / null, which all have no payload.
        kind := node.get_kind()
        if kind == 2 {
            return ch(CH_LBRACKET).concat(ch(CH_RBRACKET))
        }
        if kind == 1 {
            return ch(CH_LBRACE).concat(ch(CH_RBRACE))
        }
        return String("null");
    }
    is_array := node.get_kind() == 2
    if node.get_kind() == 0 {
        is_array = looks_like_array(node)
    }
    out! := ch(CH_LBRACE)
    if is_array {
        out! = ch(CH_LBRACKET)
        i32 i = 0
        while i < n {
            if i > 0 {
                out! = out.concat(String(","))
            }
            out! = out.concat(stringify_value(node.get_property(String.from(i)).unwrap()))
            i = i + 1
        }
        return out.concat(ch(CH_RBRACKET));
    }
    // `CPropertys` indexes properties in a hash map, so object order is the
    // map's bucket order (stable for a given set of keys, not insertion order).
    names := node.property_keys()
    i32 i = 0
    while i < n {
        key := names.get(i)
        if i > 0 {
            out! = out.concat(String(","))
        }
        out! = out.concat(ch(CH_QUOTE)).concat(escape_json(key)).concat(ch(CH_QUOTE))
        out! = out.concat(String(":"))
        out! = out.concat(stringify_value(node.get_property(key).unwrap()))
        i = i + 1
    }
    return out.concat(ch(CH_RBRACE));
}

pub fun stringify(CPropertys root) -> String {
    return stringify_value(root);
}
