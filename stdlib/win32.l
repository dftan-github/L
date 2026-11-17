// Win32 window + complete input (keyboard / mouse / IME) + minimal GDI.
// Layer 1 C bindings only; every editor-level decision stays in L.
//
// Canonical loop:
//
//   w := win32_open("SEditor", 1200, 800)
//   if l_ptr_is_null(w) != 0 { return 1 }
//   while win32_pump() != 0 {
//       while event_peek() != 0 {
//           t := event_type()
//           ...
//           event_pop()
//       }
//       gdi_frame_begin()
//       gdi_clear(30, 30, 30)
//       gdi_text(10, 10, String("hello"), 220, 220, 220)
//       gdi_frame_end()
//   }
//
// The event reads below always describe the HEAD event; call event_pop() to
// consume it. Reading without popping is how the field accessors stay
// struct-free.

// --- event kinds -----------------------------------------------------------

const EV_NONE = 0
const EV_KEY_DOWN = 1
const EV_KEY_UP = 2
const EV_CHAR = 3
const EV_MOUSE_MOVE = 4
const EV_MOUSE_DOWN = 5
const EV_MOUSE_UP = 6
const EV_WHEEL = 7
const EV_RESIZE = 8
const EV_CLOSE = 9
const EV_IME_COMPOSE = 10
const EV_IME_COMMIT = 11

// --- virtual keys used by editors (subset of VK_*) -------------------------

const VK_BACK = 8
const VK_TAB = 9
const VK_RETURN = 13
const VK_SHIFT = 16
const VK_CONTROL = 17
const VK_MENU = 18
const VK_ESCAPE = 27
const VK_SPACE = 32
const VK_PRIOR = 33
const VK_NEXT = 34
const VK_END = 35
const VK_HOME = 36
const VK_LEFT = 37
const VK_UP = 38
const VK_RIGHT = 39
const VK_DOWN = 40
const VK_DELETE = 46
const VK_LWIN = 91
const VK_RWIN = 92

const VK_F1 = 112

extern module c_win32 {
    fun l_win32_open(ptr title, i32 width, i32 height) -> ptr
    // 1 while the window lives; drains the OS message queue first.
    fun l_win32_pump() -> i32
    fun l_win32_event_peek() -> i32
    fun l_win32_event_pop() -> i32
    fun l_win32_event_type() -> i32
    fun l_win32_event_key() -> i32
    fun l_win32_event_scancode() -> i32
    fun l_win32_event_repeat() -> i32
    fun l_win32_event_mouse_x() -> i32
    fun l_win32_event_mouse_y() -> i32
    fun l_win32_event_button() -> i32
    fun l_win32_event_wheel() -> i32
    fun l_win32_key_down(i32 vk) -> i32
    fun l_win32_key_toggled(i32 vk) -> i32
    fun l_win32_mouse_x() -> i32
    fun l_win32_mouse_y() -> i32
    fun l_win32_client_width() -> i32
    fun l_win32_client_height() -> i32
    fun l_win32_dpi() -> i32
    fun l_win32_request_close() -> void
    fun l_win32_ime_text() -> ptr
    fun l_win32_set_title(ptr utf8) -> i32

    fun l_gdi_frame_begin() -> void
    fun l_gdi_frame_end() -> void
    fun l_gdi_clear(i32 r, i32 g, i32 b) -> void
    fun l_gdi_rect(i32 x, i32 y, i32 w, i32 h, i32 r, i32 g, i32 b) -> void
    fun l_gdi_hline(i32 x, i32 y, i32 w, i32 r, i32 g, i32 b) -> void
    fun l_gdi_vline(i32 x, i32 y, i32 h, i32 r, i32 g, i32 b) -> void
    fun l_gdi_push_clip(i32 x, i32 y, i32 w, i32 h) -> void
    fun l_gdi_pop_clip() -> void
    fun l_gdi_font(ptr face, i32 size, i32 bold) -> i32
    fun l_gdi_set_font(i32 font) -> void
    fun l_gdi_text(i32 x, i32 y, ptr utf8, i32 r, i32 g, i32 b) -> void
    fun l_gdi_text_width(ptr utf8, i32 font) -> i32
    fun l_gdi_line_height(i32 font) -> i32
}

// --- window ----------------------------------------------------------------

pub fun open(String title, i32 width, i32 height) -> ptr {
    return l_win32_open(title.c_str(), width, height)
}

// 1 = keep looping, 0 = window closed.
pub fun pump() -> bool {
    return l_win32_pump() != 0
}

pub fun request_close() -> void {
    l_win32_request_close()
}

pub fun set_title(String title) -> void {
    l_win32_set_title(title.c_str())
}

pub fun client_width() -> i32 {
    return l_win32_client_width()
}

pub fun client_height() -> i32 {
    return l_win32_client_height()
}

pub fun dpi() -> i32 {
    return l_win32_dpi()
}

// --- event queue -----------------------------------------------------------

pub fun event_peek() -> bool {
    return l_win32_event_peek() != 0
}

pub fun event_pop() -> void {
    l_win32_event_pop()
}

pub fun event_type() -> i32 {
    return l_win32_event_type()
}

// Virtual key for KEY_*, Unicode code point for CHAR.
pub fun event_key() -> i32 {
    return l_win32_event_key()
}

pub fun event_scancode() -> i32 {
    return l_win32_event_scancode()
}

// 1 = this is an auto-repeat, 0 = first press.
pub fun event_repeat() -> bool {
    return l_win32_event_repeat() != 0
}

pub fun event_mouse_x() -> i32 {
    return l_win32_event_mouse_x()
}

pub fun event_mouse_y() -> i32 {
    return l_win32_event_mouse_y()
}

// 0 = left, 1 = middle, 2 = right.
pub fun event_button() -> i32 {
    return l_win32_event_button()
}

// Signed wheel notches (+1 = up / away from user).
pub fun event_wheel() -> i32 {
    return l_win32_event_wheel()
}

// --- live state ------------------------------------------------------------

pub fun key_down(i32 vk) -> bool {
    return l_win32_key_down(vk) != 0
}

pub fun key_toggled(i32 vk) -> bool {
    return l_win32_key_toggled(vk) != 0
}

pub fun shift_down() -> bool {
    return key_down(VK_SHIFT)
}

pub fun ctrl_down() -> bool {
    return key_down(VK_CONTROL)
}

pub fun alt_down() -> bool {
    return key_down(VK_MENU)
}

pub fun mouse_x() -> i32 {
    return l_win32_mouse_x()
}

pub fun mouse_y() -> i32 {
    return l_win32_mouse_y()
}

// In-flight IME composition text (empty when not composing).
pub fun ime_text() -> String {
    return String(l_win32_ime_text())
}

// --- GDI -------------------------------------------------------------------

pub fun frame_begin() -> void {
    l_gdi_frame_begin()
}

pub fun frame_end() -> void {
    l_gdi_frame_end()
}

pub fun clear(i32 r, i32 g, i32 b) -> void {
    l_gdi_clear(r, g, b)
}

pub fun rect(i32 x, i32 y, i32 w, i32 h, i32 r, i32 g, i32 b) -> void {
    l_gdi_rect(x, y, w, h, r, g, b)
}

pub fun hline(i32 x, i32 y, i32 w, i32 r, i32 g, i32 b) -> void {
    l_gdi_hline(x, y, w, r, g, b)
}

pub fun vline(i32 x, i32 y, i32 h, i32 r, i32 g, i32 b) -> void {
    l_gdi_vline(x, y, h, r, g, b)
}

pub fun push_clip(i32 x, i32 y, i32 w, i32 h) -> void {
    l_gdi_push_clip(x, y, w, h)
}

pub fun pop_clip() -> void {
    l_gdi_pop_clip()
}

// Returns a font handle (0 = failure). Size is in points.
pub fun font(String face, i32 size, bool bold) -> i32 {
    v := 0
    if bold {
        v = 1
    }
    return l_gdi_font(face.c_str(), size, v)
}

pub fun set_font(i32 handle) -> void {
    l_gdi_set_font(handle)
}

pub fun text(i32 x, i32 y, String s, i32 r, i32 g, i32 b) -> void {
    l_gdi_text(x, y, s.c_str(), r, g, b)
}

pub fun text_width(String s, i32 handle) -> i32 {
    return l_gdi_text_width(s.c_str(), handle)
}

pub fun line_height(i32 handle) -> i32 {
    return l_gdi_line_height(handle)
}
