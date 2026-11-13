// get_env(PATH) + localhost HTTP GET via examples/http_client.l + serve_once.

fun main() -> i32 {
    path := get_env(String("PATH"))
    if path.is_some() {
        print("PATH exists")
    } else {
        print("PATH missing")
    }

    port := 8091
    hs := spawn {
        serve_once(port)
    }

    l_sleep_ms(200)

    resp := http_get(String("127.0.0.1"), port, String("/"))
    print(resp)

    l_thread_join(hs)
    return 0
}
