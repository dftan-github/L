// L-native test framework: passing assertions, failing assertions must be
// counted but never abort the run, and reset must clear state.
fun main() -> i32 {
    test_group(String("passing"))

    expect_true(1 < 2, String("one_lt_two"))
    expect_false(1 > 2, String("one_gt_two_false"))
    expect_eq(String("ab"), String("ab"), String("string_eq"))
    expect_int(4, 4, String("int_eq"))
    expect_eq(from(2 + 2), String("4"), String("two_plus_two"))

    opt := Optional<String>()
    expect_none(opt, String("empty_optional"))

    ok_res := Result<String, String>()
    ok_res.is_ok! = true
    ok_res.ok_val! = String("fine")
    expect_ok(ok_res, String("result_ok"))

    err_res := Result<String, String>()
    err_res.is_ok! = false
    err_res.err_val! = String("boom")
    expect_err(err_res, String("result_err"))

    if test_failed != 0 {
        print(String("fail: unexpected failure in passing group"))
        return 1
    }
    if test_passed != 8 {
        print(String("fail: wrong pass count ") + from(test_passed))
        return 1
    }

    // --- Deliberate failures: must be counted, must not stop execution. ---
    test_group(String("failing"))
    expect_true(false, String("deliberate_true_fail"))
    expect_eq(String("actual"), String("expected"), String("deliberate_eq_fail"))
    expect_int(1, 2, String("deliberate_int_fail"))

    if test_failed != 3 {
        print(String("fail: expected 3 failures, got ") + from(test_failed))
        return 1
    }
    if test_passed != 8 {
        print(String("fail: failures must not increment pass count"))
        return 1
    }

    // --- Reset returns the suite to a clean slate. ---
    test_reset()
    if test_passed != 0 {
        print(String("fail: reset pass"))
        return 1
    }
    if test_failed != 0 {
        print(String("fail: reset fail"))
        return 1
    }

    // --- A clean suite reports exit code 0; a dirty one reports 1. ---
    expect_int(1, 1, String("post_reset"))
    code := test_report()
    if code != 0 {
        print(String("fail: clean report should be 0"))
        return 1
    }

    has_failed := test_failed
    expect_true(false, String("one_more_fail"))
    if test_failed != has_failed + 1 {
        print(String("fail: failure not counted after report"))
        return 1
    }

    print(String("test_framework_ok"))
    return 0
}
