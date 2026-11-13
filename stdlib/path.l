// Cross-platform path helpers (byte strings; Windows accepts '/' in most APIs).

pub fun path_sep() -> String {
    return String("/")
}

pub fun path_join(String a, String b) -> String {
    if a.length() == 0 {
        return b
    }
    if b.length() == 0 {
        return a
    }
    last := a.char_at(a.length() - 1)
    if last == 47 || last == 92 {
        return a + b
    }
    return a + path_sep() + b
}

pub fun path_basename(String path) -> String {
    n := path.length()
    i! := n - 1
    while i >= 0 {
        c := path.char_at(i)
        if c == 47 || c == 92 {
            return path.slice(i + 1, n - i - 1)
        }
        i! = i - 1
    }
    return path
}

pub fun path_dirname(String path) -> String {
    n := path.length()
    i! := n - 1
    while i >= 0 {
        c := path.char_at(i)
        if c == 47 || c == 92 {
            if i == 0 {
                return path.slice(0, 1)
            }
            return path.slice(0, i)
        }
        i! = i - 1
    }
    return String(".")
}

pub fun path_ext(String path) -> String {
    base := path_basename(path)
    n := base.length()
    i! := n - 1
    while i >= 0 {
        if base.char_at(i) == 46 {
            return base.slice(i, n - i)
        }
        i! = i - 1
    }
    return String("")
}
