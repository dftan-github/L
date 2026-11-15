import "stdlib/properties.l"

pub class Cfg {
    delegate CPropertys
}

fun section(Cfg parent, String name, fun() body) {
    n := Cfg()
    parent.add_property(name, n)
    body()
}

fun main() -> i32 {
    root := Cfg()
    server := Cfg()
    root.add_property("server", server)

    // A: plain closure over a captured object, using `this` inside it
    runner := || {
        this.set("port", 8080)
    }
    runner()
    print(server.get("port")?.as_i32() ?? -1)

    // B: nested call passing a captured object through a callback param
    section(root, "db") { ||
        print("db block\n")
    }
    print(root.property_count())
    return 0
}
