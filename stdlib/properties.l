// CPropertys — a string-keyed bag of dynamically-typed values.
// Values are boxed into Variant; retrieval goes through Optional<Variant> so
// callers combine it with `??` for defaults ([coalesce]).
//
// Self-contained: importing this file alone pulls in everything it needs, so a
// user file only writes `import "stdlib/properties.l"`.
import "stdlib/memory.l"
import "stdlib/io.l"
import "stdlib/array.l"
import "stdlib/string.l"
import "stdlib/optional.l"
import "stdlib/map.l"
import "stdlib/variant.l"

pub class CPropertys {
    
    Map<Variant> props

    fun init() {
        props = Map<Variant>()
    }

    pub fun set(String key, Variant val) {
        props.put(key, val)
    }

    pub fun get(String key) -> Optional<Variant> {
        return props.get(key)
    }

    // All stored keys (order undefined). Used to enumerate subtrees by prefix.
    pub fun keys() -> Array<String> {
        return props.keys()
    }

    pub fun len() -> i32 {
        return props.len()
    }
}
