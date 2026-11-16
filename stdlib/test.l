// Minimal L-native test framework.
//
//   import "stdlib/test.l"
//
//   fun main() -> i32 {
//       test_group("math")
//       expect_eq(from(2 + 2), String("4"), String("two plus two"))
//       expect_true(1 < 2, String("one is less than two"))
//       expect_str_eq(String("a").concat(String("b")), String("ab"), String("concat"))
//       return test_report()          // 0 when everything passed
//   }
//
// Failures print immediately with the group name and the assertion label, so the
// first red line in the output already points at the broken expectation.

// Mutable counters — module globals, explicit type + `!` (Rule 39).
i32 test_passed! = 0
i32 test_failed! = 0
i32 test_group_len! = 0
String test_group_name! = String("")

// Name the following assertions. Failures are prefixed with it.
pub fun test_group(String name) -> void {
    test_group_name! = name
    test_group_len! = name.length()
    print(String("-- ").concat(name))
}

fun test_fail(String label, String expected, String actual) -> void {
    test_failed! = test_failed + 1
    prefix := String("[FAIL] ")
    if test_group_len > 0 {
        prefix = prefix.concat(test_group_name).concat(String(": "))
    }
    print(prefix.concat(label))
    print(String("       expected: ").concat(expected))
    print(String("       actual:   ").concat(actual))
}

pub fun test_pass() -> void {
    test_passed! = test_passed + 1
}

pub fun expect_true(bool cond, String label) -> void {
    if cond {
        test_pass()
        return
    }
    test_fail(label, String("true"), String("false"))
}

pub fun expect_false(bool cond, String label) -> void {
    if !cond {
        test_pass()
        return
    }
    test_fail(label, String("false"), String("true"))
}

pub fun expect_eq(String actual, String expected, String label) -> void {
    if actual == expected {
        test_pass()
        return
    }
    test_fail(label, expected, actual)
}

pub fun expect_int(i32 actual, i32 expected, String label) -> void {
    if actual == expected {
        test_pass()
        return
    }
    test_fail(label, from(expected), from(actual))
}

pub fun expect_i64(i64 actual, i64 expected, String label) -> void {
    if actual == expected {
        test_pass()
        return
    }
    test_fail(label, from_i64(expected), from_i64(actual))
}

// Optional must be present; reports the message when it is empty.
pub fun expect_some(Optional<String> opt, String label) -> void {
    if opt.is_some() {
        test_pass()
        return
    }
    test_fail(label, String("<some>"), String("<none>"))
}

pub fun expect_none(Optional<String> opt, String label) -> void {
    if !opt.is_some() {
        test_pass()
        return
    }
    test_fail(label, String("<none>"), String("<some>"))
}

// Result must be Ok; the error text is shown when it is not.
pub fun expect_ok(Result<String, String> res, String label) -> void {
    if res.is_ok() {
        test_pass()
        return
    }
    test_fail(label, String("<ok>"), res.unwrap_err())
}

// Result must be Err; the ok value is shown when it is not.
pub fun expect_err(Result<String, String> res, String label) -> void {
    if !res.is_ok() {
        test_pass()
        return
    }
    test_fail(label, String("<err>"), res.unwrap())
}

// Reset counters so a program can run several independent suites.
pub fun test_reset() -> void {
    test_passed! = 0
    test_failed! = 0
    test_group_len! = 0
    test_group_name! = String("")
}

pub fun test_failed_count() -> i32 {
    return test_failed
}

pub fun test_passed_count() -> i32 {
    return test_passed
}

// Print the summary and return a process exit code (0 = all passed).
pub fun test_report() -> i32 {
    total := test_passed + test_failed
    line := String("total ") + from(total)
    line = line.concat(String(", passed ")).concat(from(test_passed))
    line = line.concat(String(", failed ")).concat(from(test_failed))
    if test_failed == 0 {
        print(String("PASS: ").concat(line))
        return 0
    }
    print(String("FAIL: ").concat(line))
    return 1
}
