class RS232 {
    pub i32 baud
    pub String parity
    pub i32 data_bits
    fun init(i32 b, String p, i32 d) {
        baud = b
        parity = p
        data_bits = d
    }
}

class App {
    pub RS232 rs232
    fun init() {
        rs232 = RS232(0, "", 0)
    }
}

App app = App()

import "app_cfg.l"

fun main() -> i32 {
    print(app.rs232.baud)
    print(app.rs232.parity)
    return 0
}
