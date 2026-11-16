// P1-5: layered config — default < file < environment < command line.
fun main() -> i32 {
    dir := String("build/clang-debug/test_out")
    create_dir(dir)
    def_path := path_join(dir, String("cfg_default.json"))
    file_path := path_join(dir, String("cfg_file.json"))

    // Defaults provide the base; the file overrides two keys; env overrides one;
    // cli wins over everything it names.
    write_all(def_path, String("{\"host\":\"0.0.0.0\",\"port\":\"80\",\"debug\":\"false\"}"))
    write_all(file_path, String("{\"host\":\"127.0.0.1\",\"port\":\"8080\"}"))

    cli := Map<String>()
    cli.put(String("port"), String("9999"))

    r := config_layered(def_path, file_path, cli)
    if !r.is_ok() {
        print(String("fail: layered resolve"))
        print(r.unwrap_err())
        remove_file(def_path)
        remove_file(file_path)
        return 1
    }
    cfg := r.unwrap()

    // file overrides default
    if config_get(cfg, String("host"), String("")) != String("127.0.0.1") {
        print(String("fail: file should override default"))
        return 1
    }
    // cli overrides file
    if config_get(cfg, String("port"), String("")) != String("9999") {
        print(config_get(cfg, String("port"), String("")))
        print(String("fail: cli should override file"))
        return 1
    }
    // untouched key survives from default
    if config_get(cfg, String("debug"), String("")) != String("false") {
        print(String("fail: default key lost"))
        return 1
    }

    // --- Typed accessors ---
    if config_i32(cfg, String("port"), 0) != 9999 {
        print(String("fail: config_i32"))
        return 1
    }
    if config_i32(cfg, String("missing"), 42) != 42 {
        print(String("fail: config_i32 default"))
        return 1
    }
    if config_bool(cfg, String("debug"), true) {
        print(String("fail: config_bool"))
        return 1
    }
    if !config_bool(cfg, String("missing_bool"), true) {
        print(String("fail: config_bool default"))
        return 1
    }

    // --- Env layer overrides file, sits below cli ---
    set_env(String("L_HOST"), String("10.0.0.1"))
    cli2 := Map<String>()
    r2 := config_layered(def_path, file_path, cli2)
    if !r2.is_ok() {
        print(String("fail: layered with env"))
        return 1
    }
    cfg2 := r2.unwrap()
    if config_get(cfg2, String("host"), String("")) != String("10.0.0.1") {
        print(config_get(cfg2, String("host"), String("")))
        print(String("fail: env should override file"))
        return 1
    }
    // cli still outranks env
    if config_get(cfg2, String("port"), String("")) != String("8080") {
        print(String("fail: file port under env run"))
        return 1
    }

    // --- Env name derivation ---
    if config_env_name(String("database.port")) != String("L_DATABASE_PORT") {
        print(config_env_name(String("database.port")))
        print(String("fail: env name"))
        return 1
    }

    // --- A named-but-broken config must fail loudly, not silently ---
    bad_path := path_join(dir, String("cfg_bad.json"))
    write_all(bad_path, String("not json at all"))
    r3 := config_layered(def_path, bad_path, cli2)
    if r3.is_ok() {
        print(String("fail: broken config should error"))
        return 1
    }

    remove_file(def_path)
    remove_file(file_path)
    remove_file(bad_path)
    print(String("config_layered_ok"))
    return 0
}
