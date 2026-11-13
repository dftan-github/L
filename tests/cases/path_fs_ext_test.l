fun main() -> i32 {
    p := path_join(String("a"), String("b.txt"))
    print(p)
    print(path_basename(p))
    print(path_dirname(String("a/b/c.txt")))
    print(path_ext(String("a/b/c.txt")))

    dir := String("build/clang-debug/test_out/path_fs_ext_dir")
    create_dir(dir)
    f1 := path_join(dir, String("one.txt"))
    f2 := path_join(dir, String("two.txt"))
    write_all(f1, String("hi"))
    copy_file(f1, f2)
    if file_exists(f2) == false {
        print("fail copy")
        return 1
    }
    f3 := path_join(dir, String("three.txt"))
    rename_path(f2, f3)
    listing := read_dir(dir)
    if listing.is_ok() == false {
        print("fail read_dir")
        return 1
    }
    names := listing.unwrap()
    print(from(names.len()))
    remove_file(f1)
    remove_file(f3)
    remove_dir(dir)
    print(String("path_fs_ok"))
    return 0
}
