// Load flat JSON object config from a file into Map<String>.

pub fun load_json_config(String path) -> Result<Map<String>, String> {
    opt := read_all(path)
    if !opt.is_some() {
        return Result<Map<String>, String>(false, Map<String>(), String("config: cannot read file"))
    }
    return json_parse_object(opt.unwrap())
}

pub fun config_get(Map<String> cfg, String key, String default_val) -> String {
    return cfg.get(key) ?? default_val
}
