// Flat JSON-object config loader: top-level members flattened to text.
// Strings keep their bare value; nested containers keep their serialized JSON.
import "stdlib/json.l"

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
