// Namespace test: `use` aliases for modules and types.
// Fixture: tests/cases/net_server.l (imported as `net_server`).

import net_server
use net_server as net
use net_server::Config as Cfg

fun main() -> i32 {
  // Fully-qualified module call (no alias).
  String a = net_server::greet("a")
  print(a)
  // Module alias: `net::fn()` -> `net_server::fn()`.
  String b = net::greet("b")
  print(b)
  i32 p = net::port()
  print(p)
  // Type alias: `Cfg` -> `net_server::Config`.
  Cfg c = net::make_config(9)
  print(c.port)
  return 0
}
