// Filesystem helpers over runtime/sys.c.

pub fun file_exists(String path) -> bool {
    if l_file_exists(path.c_str()) != 0 {
        return true
    }
    return false
}

pub fun is_dir(String path) -> bool {
    if l_is_dir(path.c_str()) != 0 {
        return true
    }
    return false
}

pub fun create_dir(String path) -> Result<bool, String> {
    rc := l_create_dir(path.c_str())
    if rc == 0 {
        return Result<bool, String>(true, true, String(""))
    }
    return Result<bool, String>(false, false, String("create_dir failed"))
}

pub fun remove_file(String path) -> Result<bool, String> {
    rc := l_remove_file(path.c_str())
    if rc == 0 {
        return Result<bool, String>(true, true, String(""))
    }
    return Result<bool, String>(false, false, String("remove_file failed"))
}

pub fun remove_dir(String path) -> Result<bool, String> {
    rc := l_remove_dir(path.c_str())
    if rc == 0 {
        return Result<bool, String>(true, true, String(""))
    }
    return Result<bool, String>(false, false, String("remove_dir failed"))
}

pub fun rename_path(String from, String to) -> Result<bool, String> {
    rc := l_rename_path(from.c_str(), to.c_str())
    if rc == 0 {
        return Result<bool, String>(true, true, String(""))
    }
    return Result<bool, String>(false, false, String("rename_path failed"))
}

pub fun copy_file(String from, String to) -> Result<bool, String> {
    rc := l_copy_file(from.c_str(), to.c_str())
    if rc == 0 {
        return Result<bool, String>(true, true, String(""))
    }
    return Result<bool, String>(false, false, String("copy_file failed"))
}

// Directory entries as Array<String> (skips "." / "..").
pub fun read_dir(String path) -> Result<Array<String>, String> {
    raw := String(l_read_dir(path.c_str()))
    arr := Array<String>()
    if raw.length() == 0 {
        return Result<Array<String>, String>(true, arr, String(""))
    }
    // Split on newline (10).
    parts := raw.split(10)
    i! := 0
    while i < parts.len() {
        raw_name := parts.get(i)
        name := raw_name.trim()
        if name.length() > 0 {
            arr.push(name)
        }
        i! = i + 1
    }
    return Result<Array<String>, String>(true, arr, String(""))
}
