fun main() -> i32 {
    dir := String("test_temp_dir")
    nested := String("test_temp_dir/fs_nested.txt")

    if file_exists(dir) {
        print("before:true")
        return 1
    }
    print("before:false")

    mk := create_dir(dir)
    if mk.is_ok() == false {
        print("create:err")
        return 1
    }
    print("create:ok")

    if file_exists(dir) == false {
        print("after:false")
        return 1
    }
    print("after:true")

    if File.write_all(nested, String("fs-ok")) == false {
        print("write:err")
        return 1
    }

    rm := remove_file(nested)
    if rm.is_ok() == false {
        print("remove_file:err")
        return 1
    }
    print("remove_file:ok")

    rd := remove_dir(dir)
    if rd.is_ok() == false {
        print("remove_dir:err")
        return 1
    }
    print("remove_dir:ok")

    if file_exists(dir) {
        print("cleanup:true")
        return 1
    }
    print("cleanup:false")
    return 0
}
