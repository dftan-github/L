import "stdlib/optional.l"

// Destructuring patterns (`A474`):
//   `User { name, age } => ...` binds fields by name;
//   `User { addr: Addr { city } }` nests;
//   `some(u) => u { ... }`? No — nesting goes through the pattern itself:
//   `match o { some(User { name }) => ... }` destructures the Optional payload.

class Addr {
    pub String city
    pub i32 zip
    fun init(String c, i32 z) {
        city = c
        zip = z
    }
}

class User {
    pub String name
    pub i32 age
    pub Addr addr
    fun init(String n, i32 a, Addr ad) {
        name = n
        age = a
        addr = ad
    }
}

class Admin {
    pub String name
    fun init(String n) {
        name = n
    }
}

fun describe(User u) {
    match u {
        User { name, age } => {
            print(name)
            print(age)
        }
        _ => { print("not a user") }
    }
}

// Nested destructuring: `addr: Addr { city }` walks two GEPs.
fun city_of(User u) {
    match u {
        User { addr: Addr { city, zip } } => {
            print(city)
            print(zip)
        }
        _ => { print("none") }
    }
}

// Field skipping with `_` and rebinding with `field: alias`.
fun skip_and_alias(User u) {
    match u {
        User { name: who, age: _, addr: _ } => { print(who) }
        _ => { print("none") }
    }
}

// Value-producing destructuring arm: the arm yields a bound field.
fun name_of(User u) -> String {
    n := match u {
        User { name } => name
        _ => "anon"
    }
    return n
}

fun age_plus(User u, i32 d) -> i32 {
    v := match u {
        User { age } => age + d
        _ => 0
    }
    return v
}

fun main() -> i32 {
    a := Addr("Paris", 75001)
    u := User("Ada", 36, a)
    describe(u)
    city_of(u)
    skip_and_alias(u)
    print(name_of(u))
    print(age_plus(u, 4))

    // Binding scope: after the arm, the shadowed global name is restored.
    o := Optional<User>(true, u)
    match o {
        some(x) => {
            match x {
                User { name, age } => {
                    print(name)
                    print(age)
                }
                _ => { print("?") }
            }
        }
        none => { print("empty") }
    }

    ad := Optional<Admin>(true, Admin("Root"))
    match ad {
        some(v) => { print(v.name) }
        none => { print("no admin") }
    }
    return 0
}
