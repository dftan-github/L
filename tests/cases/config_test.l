fun main() -> i32 {
    // Robustness: multi-space, blank-with-spaces, no-'=', // vs #, multi-'='.
    src := "# Database Config\nhost = 127.0.0.1\nport    =    8080\n\n   \nthis_is_a_bad_line\n// not_a_hash_comment\n//fake = still_stored\nexpr = a=b=c\n# App\nname = L-Lang\n"
    cfg := parse_config(src)

    h := cfg.get("host")
    print(h ?? String(""))

    p := cfg.get("port")
    print(p ?? String(""))

    n := cfg.get("name")
    print(n ?? String(""))

    // First '=' only: value keeps remaining equals
    e := cfg.get("expr")
    print(e ?? String(""))

    // //fake = still_stored → not a # comment, so key "//fake" is stored
    fake := cfg.get("//fake")
    print(fake ?? String(""))

    // Malformed / space-only / # comments must not invent keys
    bad := cfg.get("this_is_a_bad_line")
    print(bad ?? String("skipped"))

    slash := cfg.get("// not_a_hash_comment")
    print(slash ?? String("skipped"))

    missing := cfg.get("Database")
    print(missing ?? String("ok"))

    return 0
}
