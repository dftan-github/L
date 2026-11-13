fun main() -> i32 {
    dir := String("build/clang-debug/test_out")
    path := path_join(dir, String("log_config_test.json"))
    lb := String(l_str_from_byte(123))
    rb := String(l_str_from_byte(125))
    q := String(l_str_from_byte(34))
    body := lb + q + String("host") + q + String(":") + q + String("127.0.0.1") + q + String(",") + q + String("port") + q + String(":") + q + String("8080") + q + rb
    write_all(path, body)
    cfg_res := load_json_config(path)
    if cfg_res.is_ok() == false {
        print("fail config")
        return 1
    }
    cfg := cfg_res.unwrap()
    host := config_get(cfg, String("host"), String(""))
    print(host)
    log_info(String("config loaded"))
    remove_file(path)
    print(String("log_config_ok"))
    return 0
}
