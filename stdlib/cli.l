// Pure-L CLI flag parsing on top of get_args() + Map / String.

pub fun parse_flags() -> Map<String> {
    flags := Map<String>()
    args := get_args()
    // Skip argv[0] (program name); half-open range 1..len.
    for i in 1..args.len() {
        a := args.get(i)
        if a.starts_with("--") {
            parts := a.split(61)
            if parts.len() >= 2 {
                raw_key := parts.get(0)
                key := raw_key.slice(2, raw_key.length() - 2)
                // Join remaining parts so values may contain '='.
                val! := parts.get(1)
                k! := 2
                while k < parts.len() {
                    val! = val + String("=") + parts.get(k)
                    k! = k + 1
                }
                flags.put(key, val)
            } else {
                key := a.slice(2, a.length() - 2)
                flags.put(key, String("true"))
            }
        }
    }
    return flags
}
