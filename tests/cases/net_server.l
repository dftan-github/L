// Fixture for tests/cases/namespace_test.l (imported as `net_server`,
// aliased with `use net_server as net` and `use net_server::Config as Cfg`).

pub fun greet(String name) -> String {
  return "hello {name}"
}

pub fun port() -> i32 {
  return 8080
}

pub class Config {
  i32 port
}

pub fun make_config(i32 p) -> Config {
  return Config(p)
}
