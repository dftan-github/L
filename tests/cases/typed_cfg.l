import "stdlib/properties.l"

// [type-config] Three-level typed config tree: App → Server → Database.
// Every level is a distinct class, so each `name { ... }` block must rebind
// `this` to the nested field type and bare `port = 5432` must land on
// `Database.port`, not on the root.
pub class Database {
    i32 port
    String host
    i32 pool_size
}

pub class Server {
    Database db
    String name
}

pub class App {
    Server server
    String name
}

// Registered config-block function: a leading `fun() body` parameter is what
// makes a bare `{ ... }` after `app(...)` a config block instead of a body.
fun app(fun() body)

fun main() -> i32 {
    // ---- happy path: the tree from the spec ----
    cfg := app(type: App) {
        server {
            db {
                port = 5432
                host = "127.0.0.1"
            }
        }
        name = "svc"
    }
    print(cfg.server.db.port)                       // 5432
    print(cfg.server.db.host)                       // 127.0.0.1
    print(cfg.name)                                 // svc

    // ---- edge 1: untouched sibling fields stay zero / empty ----
    print(cfg.server.name.length())                 // 0 (never assigned)
    print(cfg.server.db.pool_size)                  // 0

    // ---- edge 2: two separate roots never share state ----
    other := app(type: App) {
        server {
            db {
                port = 1
            }
        }
    }
    print(other.server.db.port)                     // 1
    print(cfg.server.db.port)                       // 5432 (unchanged)

    // ---- edge 3: same section entered twice keeps the first write's node ----
    twice := app(type: App) {
        server {
            db {
                port = 7
            }
        }
        server {
            db {
                port = 8
            }
        }
    }
    print(twice.server.db.port)                     // 8 (later write wins)

    // ---- edge 4: section with no statements at all ----
    bare := app(type: App) {
        server {
        }
    }
    print(bare.server.db.port)                      // 0 (zero-initialised)

    // ---- edge 5: root fields set after sections still work ----
    later := app(type: App) {
        server {
            db {
                host = "h"
            }
        }
        server {
            name = "srv"
        }
        name = "root"
    }
    print(later.name)                               // root
    print(later.server.name)                        // srv
    print(later.server.db.host)                     // h

    return 0
}
