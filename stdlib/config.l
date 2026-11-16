// Flat JSON-object config loader: top-level members flattened to text.
// Strings keep their bare value; nested containers keep their serialized JSON.
import "stdlib/json.l"
import "stdlib/env.l"

// Text form of one member: bare for a string, serialized JSON otherwise.
fun member_text(CPropertys node) -> String {
    v := node.get_val()
    if v.is_some() {
        if v.unwrap().is_string() {
            return v.unwrap().as_string().unwrap()
        }
    }
    return stringify(node)
}

pub fun load_json_config(String path) -> Result<Map<String>, String> {
    m := Map<String>()
    opt := read_all(path)
    if !opt.is_some() {
        return Result<Map<String>, String>(false, m, String("config: cannot read file"))
    }
    r := parse(opt.unwrap())
    if !r.is_ok() {
        return Result<Map<String>, String>(false, m, r.unwrap_err())
    }
    root := r.unwrap()
    if root.get_kind() != 1 {
        return Result<Map<String>, String>(false, m, String("config: expected object"))
    }
    names := root.property_keys()
    i32 i = 0
    while i < names.len() {
        key := names.get(i)
        m.put(key, member_text(root.get_property(key).unwrap()))
        i = i + 1
    }
    return Result<Map<String>, String>(true, m, String(""))
}

pub fun config_get(Map<String> cfg, String key, String default_val) -> String {
    return cfg.get(key) ?? default_val
}

// ---------------------------------------------------------------------------
// Layered config: default < file < environment < command line.
//
// Each layer is optional; later layers win. The chain is resolved once into a
// flat Map so lookups stay O(1) instead of walking four layers per call.
// [performance-first] No allocation happens in config_layered_get.
// [early-exit] Each layer is applied by a guard-checked helper.
// ---------------------------------------------------------------------------

// Copy every entry of `src` into `dst`, overwriting existing keys.
pub fun config_overlay(Map<String> dst, Map<String> src) -> void {
    keys := src.keys()
    i32 i = 0
    while i < keys.len() {
        k := keys.get(i)
        v := src.get(k)
        if v.is_some() {
            dst.put(k, v.unwrap())
        }
        i = i + 1
    }
}

// Environment variable name for a key: `database.port` -> `L_DATABASE_PORT`.
// Dots become underscores, letters upper-cased; other chars are kept as-is.
pub fun config_env_name(String key) -> String {
    n := key.length()
    out := String("L_")
    i32 i = 0
    while i < n {
        c := key.char_at(i)
        if c == 46 {
            out = out.concat(String("_"))
        } else if c >= 97 && c <= 122 {
            // lower -> upper
            out = out.concat(String(l_str_from_byte(c - 32)))
        } else {
            out = out.concat(String(l_str_from_byte(c)))
        }
        i = i + 1
    }
    return out
}

// Resolve the chain. `cli` may be an empty Map. Missing files are not errors —
// a project may ship defaults only.
pub fun config_layered(String default_path, String file_path, Map<String> cli) -> Result<Map<String>, String> {
    m := Map<String>()

    if default_path.length() > 0 {
        r := load_json_config(default_path)
        if r.is_ok() {
            config_overlay(m, r.unwrap())
        }
    }

    if file_path.length() > 0 {
        r2 := load_json_config(file_path)
        if !r2.is_ok() {
            // An explicitly named config that fails to parse must not be silent.
            return Result<Map<String>, String>(false, m, r2.unwrap_err())
        }
        config_overlay(m, r2.unwrap())
    }

    // Environment overrides are applied only for keys the config already knows,
    // so a stray L_* variable cannot inject surprise settings.
    known := m.keys()
    i32 i = 0
    while i < known.len() {
        k := known.get(i)
        env_name := config_env_name(k)
        e := get_env(env_name)
        if e.is_some() {
            m.put(k, e.unwrap())
        }
        i = i + 1
    }

    config_overlay(m, cli)
    return Result<Map<String>, String>(true, m, String(""))
}

// Typed accessors — each returns the caller's default when absent.
pub fun config_i32(Map<String> cfg, String key, i32 default_val) -> i32 {
    v := cfg.get(key)
    if !v.is_some() {
        return default_val
    }
    return v.unwrap().to_i32()
}

pub fun config_bool(Map<String> cfg, String key, bool default_val) -> bool {
    v := cfg.get(key)
    if !v.is_some() {
        return default_val
    }
    s := v.unwrap()
    // [style] Constants on the left (Yoda): a constant can never be null, and a
    // stray `=` written for `==` is a compile error instead of a silent assign.
    if "true" == s {
        return true
    }
    if "1" == s {
        return true
    }
    if "false" == s {
        return false
    }
    if "0" == s {
        return false
    }
    return default_val
}
