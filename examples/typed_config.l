import "stdlib/properties.l"

pub class Database {
    i32 port
    String host
}

pub class Server {
    Database db
}

pub class App {
    Server server
}

// Registered config-block function: the leading `fun() body` parameter is what
// makes a bare `{ ... }` after `app(...)` a config block instead of a body.
pub fun app(fun() body)

fun main() -> i32 {
    cfg := app(type: App) {
        server {
            db {
                port = 5432
                host = "127.0.0.1"
            }
        }
    }
    print("port=" + cfg.server.db.port)
    print("host=" + cfg.server.db.host)
    return 0
}
