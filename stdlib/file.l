// Minimal File I/O — wraps C FILE* from runtime/io.c.
// Static-style: File.read_all(path) / File.write_all(path, content)
import "stdlib/result.l"

class File {
    ptr handle
}

pub fun write_all(String path, String content) -> bool {
    f := l_file_open(path.c_str(), "wb")
    if l_ptr_is_null(f) != 0 {
        return false
    }
    n := l_file_write(f, content.c_str(), content.length())
    l_file_close(f)
    if n != content.length() {
        return false
    }
    return true
}

// Append to `path`, creating it when absent. Returns Err with a readable message
// so callers can report *why* logging could not be redirected.
pub fun append_all(String path, String content) -> Result<bool, String> {
    f := l_file_open(path.c_str(), "ab")
    if l_ptr_is_null(f) != 0 {
        return Result<bool, String>(false, false, String("cannot open file for append: ") + path)
    }
    if content.length() == 0 {
        l_file_close(f)
        return Result<bool, String>(true, true, String(""))
    }
    n := l_file_write(f, content.c_str(), content.length())
    l_file_close(f)
    if n != content.length() {
        return Result<bool, String>(false, false, String("short write: ") + path)
    }
    return Result<bool, String>(true, true, String(""))
}

pub fun read_all(String path) -> Optional<String> {    f := l_file_open(path.c_str(), "rb")
    if l_ptr_is_null(f) != 0 {
        return Optional<String>()
    }
    sz := l_file_size(f)
    if sz < 0 {
        l_file_close(f)
        return Optional<String>()
    }
    buf := l_alloc(sz + 1)
    if l_ptr_is_null(buf) != 0 {
        l_file_close(f)
        return Optional<String>()
    }
    n := l_file_read(f, buf, sz)
    l_file_close(f)
    if n < 0 {
        l_free(buf)
        return Optional<String>()
    }
    l_store_u8(buf, n, 0)
    tmp := String(buf)
    out := tmp.slice(0, n)
    l_free(buf)
    return Optional<String>(true, out)
}
