fun main() -> i32 {
    text := "My email is test@example.com"

    // Substring / literal match
    if contains_pattern(text, "example") {
        print("Found example!")
    }

    // Simple class + quantifier pattern
    if contains_pattern(text, "[a-z]+@[a-z]+") {
        print("Found email pattern!")
    }

    return 0
}
