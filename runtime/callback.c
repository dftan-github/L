/* Layer 1 C runtime: register / trigger L callbacks (C ABI function pointers). */
#include <stdint.h>

typedef void (*l_callback_i32_fn)(int32_t);

static l_callback_i32_fn saved_cb = 0;

void l_register_callback(l_callback_i32_fn cb) {
  saved_cb = cb;
}

void l_trigger_callback(int32_t val) {
  if (saved_cb != 0) {
    saved_cb(val);
  }
}
