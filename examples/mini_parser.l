// Minimal var-decl parser in pure L (self-hosting warm-up).
// Recognizes: `i32 a = 10`  and  `b := 20` / `name := "L"`

interface AstNode {
    fun node_kind() -> String
}

class VarDeclNode : AstNode {
    String type_name
    String name
    String value

    pub fun node_kind() -> String {
        return "VarDecl"
    }
}

fun strip_value_quotes(String s) -> String {
    n := s.length()
    if n >= 2 {
        // Ordinary quoted literal: "L"
        if s.char_at(0) == 34 {
            if s.char_at(n - 1) == 34 {
                return s.slice(1, n - 2)
            }
        }
        // L source embeds \" without unescaping: \"L\"
        if n >= 4 {
            if s.char_at(0) == 92 {
                if s.char_at(1) == 34 {
                    if s.char_at(n - 2) == 92 {
                        if s.char_at(n - 1) == 34 {
                            return s.slice(2, n - 4)
                        }
                    }
                }
            }
        }
    }
    return s
}

fun is_ident_name(String s) -> bool {
    n := s.length()
    if n == 0 {
        return false
    }
    if is_alpha(s.char_at(0)) == false {
        return false
    }
    i! := 1
    while i < n {
        c := s.char_at(i)
        if is_alpha(c) == false {
            if is_digit(c) == false {
                if c != 95 {
                    return false
                }
            }
        }
        i! = i + 1
    }
    return true
}

pub fun parse_var_decl(String line) -> Optional<VarDeclNode> {
    parts := line.split(32)
    n := parts.len()

    // Typed: TYPE NAME = VALUE
    if n == 4 {
        ty := parts.get(0)
        nm := parts.get(1)
        eq := parts.get(2)
        val := parts.get(3)
        if eq == "=" {
            if is_ident_name(ty) {
                if is_ident_name(nm) {
                    sv := strip_value_quotes(val)
                    node := VarDeclNode()
                    node.type_name = ty
                    node.name = nm
                    node.value = sv
                    return Optional<VarDeclNode>(true, node)
                }
            }
        }
    }

    // Inferred: NAME := VALUE
    if n == 3 {
        nm := parts.get(0)
        op := parts.get(1)
        val := parts.get(2)
        if op == ":=" {
            if is_ident_name(nm) {
                sv := strip_value_quotes(val)
                inferred := String("inferred")
                node := VarDeclNode()
                node.type_name = inferred
                node.name = nm
                node.value = sv
                return Optional<VarDeclNode>(true, node)
            }
        }
    }

    return Optional<VarDeclNode>()
}
