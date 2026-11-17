/* Layer 1 C runtime: Win32 window + input (keyboard / mouse / IME) + minimal GDI.
 *
 * Scope (deliberate): the INPUT surface is complete; the DRAW surface is the
 * minimum needed to paint text and rectangles. Callers hold an opaque `ptr` for
 * the window session.
 *
 * Event queue: the pump drains Win32 messages into a fixed ring. Callers read
 * the head event, then call l_win32_event_pop() to consume it:
 *   while (l_win32_pump() != 0) {
 *     while (l_win32_event_peek() != 0) {
 *       t := l_win32_event_type()
 *       ...
 *       l_win32_event_pop()
 *     }
 *   }
 */
#ifndef _WIN32

#include <stdint.h>

void* l_win32_open(const char* title, int32_t w, int32_t h) { (void)title; (void)w; (void)h; return 0; }
int32_t l_win32_pump(void) { return 0; }
int32_t l_win32_event_peek(void) { return 0; }
int32_t l_win32_event_pop(void) { return 0; }
int32_t l_win32_event_type(void) { return 0; }
int32_t l_win32_event_key(void) { return 0; }
int32_t l_win32_event_scancode(void) { return 0; }
int32_t l_win32_event_repeat(void) { return 0; }
int32_t l_win32_event_mouse_x(void) { return 0; }
int32_t l_win32_event_mouse_y(void) { return 0; }
int32_t l_win32_event_button(void) { return 0; }
int32_t l_win32_event_wheel(void) { return 0; }
int32_t l_win32_key_down(int32_t vk) { (void)vk; return 0; }
int32_t l_win32_key_toggled(int32_t vk) { (void)vk; return 0; }
int32_t l_win32_mouse_x(void) { return 0; }
int32_t l_win32_mouse_y(void) { return 0; }
int32_t l_win32_client_width(void) { return 0; }
int32_t l_win32_client_height(void) { return 0; }
int32_t l_win32_dpi(void) { return 96; }
void l_win32_request_close(void) {}
char* l_win32_ime_text(void) { return 0; }
int32_t l_win32_set_title(const char* t) { (void)t; return 0; }
void l_gdi_frame_begin(void) {}
void l_gdi_frame_end(void) {}
void l_gdi_clear(int32_t r, int32_t g, int32_t b) { (void)r; (void)g; (void)b; }
void l_gdi_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t g, int32_t b) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; }
void l_gdi_hline(int32_t x, int32_t y, int32_t w, int32_t r, int32_t g, int32_t b) { (void)x; (void)y; (void)w; (void)r; (void)g; (void)b; }
void l_gdi_vline(int32_t x, int32_t y, int32_t h, int32_t r, int32_t g, int32_t b) { (void)x; (void)y; (void)h; (void)r; (void)g; (void)b; }
void l_gdi_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t r, int32_t g, int32_t b) { (void)x0; (void)y0; (void)x1; (void)y1; (void)r; (void)g; (void)b; }
void l_gdi_ellipse(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t g, int32_t b) { (void)x; (void)y; (void)w; (void)h; (void)r; (void)g; (void)b; }
void l_gdi_push_clip(int32_t x, int32_t y, int32_t w, int32_t h) { (void)x; (void)y; (void)w; (void)h; }
void l_gdi_pop_clip(void) {}
int32_t l_gdi_font(const char* face, int32_t size, int32_t bold) { (void)face; (void)size; (void)bold; return 0; }
void l_gdi_set_font(int32_t font) { (void)font; }
void l_gdi_text(int32_t x, int32_t y, const char* utf8, int32_t r, int32_t g, int32_t b) { (void)x; (void)y; (void)utf8; (void)r; (void)g; (void)b; }
int32_t l_gdi_text_width(const char* utf8, int32_t font) { (void)utf8; (void)font; return 0; }
int32_t l_gdi_line_height(int32_t font) { (void)font; return 0; }

#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <imm.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "imm32.lib")

void* l_alloc(size_t size);
void l_free(void* ptr);

#define L_MAX_FONTS 8
#define L_MAX_EVENTS 512
#define L_MAX_CLIP 8

enum {
  L_EV_NONE = 0,
  L_EV_KEY_DOWN = 1,
  L_EV_KEY_UP = 2,
  L_EV_CHAR = 3,
  L_EV_MOUSE_MOVE = 4,
  L_EV_MOUSE_DOWN = 5,
  L_EV_MOUSE_UP = 6,
  L_EV_WHEEL = 7,
  L_EV_RESIZE = 8,
  L_EV_CLOSE = 9,
  L_EV_IME_COMPOSE = 10,
  L_EV_IME_COMMIT = 11
};

typedef struct LWinEvent {
  int32_t type;
  int32_t key;
  int32_t scancode;
  int32_t repeat;
  int32_t x;
  int32_t y;
  int32_t button;
  int32_t wheel;
} LWinEvent;

typedef struct LWin32 {
  HWND hwnd;
  HDC back_dc;
  HBITMAP back_bmp;
  HBITMAP back_old;
  int32_t width;
  int32_t height;
  int32_t dpi;
  int32_t running;

  LWinEvent events[L_MAX_EVENTS];
  int32_t ev_head;
  int32_t ev_tail;

  HFONT fonts[L_MAX_FONTS];
  int32_t font_count;
  int32_t active_font;

  RECT clips[L_MAX_CLIP];
  int32_t clip_depth;

  WCHAR comp[256];
  int32_t comp_len;
} LWin32;

static LWin32* g_win = NULL;
static const WCHAR kClassName[] = L"L_Win32_Window";

/* ------------------------------------------------------------------ queue */

static void push_event(const LWinEvent* ev) {
  if (g_win == NULL) return;
  LWin32* w = g_win;
  int32_t next = (w->ev_tail + 1) % L_MAX_EVENTS;
  if (next == w->ev_head) return; /* full: drop rather than stall the pump */
  w->events[w->ev_tail] = *ev;
  w->ev_tail = next;
}

static void clear_events(LWin32* w) {
  w->ev_head = 0;
  w->ev_tail = 0;
}

/* ------------------------------------------------------------- back buffer */

static void back_destroy(LWin32* w) {
  if (w->back_dc != NULL) {
    if (w->back_old != NULL) {
      SelectObject(w->back_dc, w->back_old);
      w->back_old = NULL;
    }
    if (w->back_bmp != NULL) {
      DeleteObject(w->back_bmp);
      w->back_bmp = NULL;
    }
    DeleteDC(w->back_dc);
    w->back_dc = NULL;
  }
  w->clip_depth = 0;
}

static void back_create(LWin32* w) {
  back_destroy(w);
  if (w->hwnd == NULL) return;
  HDC screen = GetDC(w->hwnd);
  w->back_dc = CreateCompatibleDC(screen);
  w->back_bmp = CreateCompatibleBitmap(screen, w->width > 0 ? w->width : 1,
                                       w->height > 0 ? w->height : 1);
  ReleaseDC(w->hwnd, screen);
  if (w->back_dc != NULL && w->back_bmp != NULL) {
    w->back_old = (HBITMAP)SelectObject(w->back_dc, w->back_bmp);
  }
  if (w->back_dc != NULL) {
    SetBkMode(w->back_dc, TRANSPARENT);
  }
}

static COLORREF to_color(int32_t r, int32_t g, int32_t b) {
  if (r < 0) r = 0;
  if (r > 255) r = 255;
  if (g < 0) g = 0;
  if (g > 255) g = 255;
  if (b < 0) b = 0;
  if (b > 255) b = 255;
  return RGB((BYTE)r, (BYTE)g, (BYTE)b);
}

/* ---------------------------------------------------------------- window */

static void update_client_size(LWin32* w) {
  RECT rc;
  if (!GetClientRect(w->hwnd, &rc)) return;
  int32_t nw = rc.right - rc.left;
  int32_t nh = rc.bottom - rc.top;
  if (nw < 0) nw = 0;
  if (nh < 0) nh = 0;
  if (nw == w->width && nh == w->height) return;
  w->width = nw;
  w->height = nh;
  if (w->back_dc != NULL) back_create(w);

  LWinEvent ev;
  memset(&ev, 0, sizeof(ev));
  ev.type = L_EV_RESIZE;
  ev.x = nw;
  ev.y = nh;
  push_event(&ev);
}

static LRESULT CALLBACK win32_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  if (g_win == NULL) return DefWindowProcW(hwnd, msg, wp, lp);
  LWin32* w = g_win;
  LWinEvent ev;
  memset(&ev, 0, sizeof(ev));

  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      if (w->back_dc != NULL) {
        BitBlt(dc, 0, 0, w->width, w->height, w->back_dc, 0, 0, SRCCOPY);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      update_client_size(w);
      return 0;
    case WM_DPICHANGED:
      w->dpi = (int32_t)HIWORD(wp);
      if (w->dpi <= 0) w->dpi = 96;
      return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      ev.type = L_EV_KEY_DOWN;
      ev.key = (int32_t)wp;
      ev.scancode = (int32_t)((lp >> 16) & 0xFF);
      ev.repeat = (lp & (1u << 30)) ? 1 : 0;
      push_event(&ev);
      return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
      ev.type = L_EV_KEY_UP;
      ev.key = (int32_t)wp;
      ev.scancode = (int32_t)((lp >> 16) & 0xFF);
      push_event(&ev);
      return 0;

    case WM_CHAR:
      ev.type = L_EV_CHAR;
      ev.key = (int32_t)wp;
      push_event(&ev);
      return 0;
    case WM_UNICHAR:
      if (wp == UNICODE_NOCHAR) return TRUE;
      ev.type = L_EV_CHAR;
      ev.key = (int32_t)wp;
      push_event(&ev);
      return 0;

    case WM_MOUSEMOVE:
      ev.type = L_EV_MOUSE_MOVE;
      ev.x = (int32_t)(int16_t)LOWORD(lp);
      ev.y = (int32_t)(int16_t)HIWORD(lp);
      push_event(&ev);
      return 0;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
      ev.type = L_EV_MOUSE_DOWN;
      ev.x = (int32_t)(int16_t)LOWORD(lp);
      ev.y = (int32_t)(int16_t)HIWORD(lp);
      ev.button = msg == WM_LBUTTONDOWN ? 0 : (msg == WM_MBUTTONDOWN ? 1 : 2);
      SetCapture(hwnd);
      push_event(&ev);
      return 0;

    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
      ev.type = L_EV_MOUSE_UP;
      ev.x = (int32_t)(int16_t)LOWORD(lp);
      ev.y = (int32_t)(int16_t)HIWORD(lp);
      ev.button = msg == WM_LBUTTONUP ? 0 : (msg == WM_MBUTTONUP ? 1 : 2);
      if ((wp & MK_LBUTTON) == 0 && (wp & MK_MBUTTON) == 0 && (wp & MK_RBUTTON) == 0) {
        ReleaseCapture();
      }
      push_event(&ev);
      return 0;

    case WM_MOUSEWHEEL:
      ev.type = L_EV_WHEEL;
      ev.wheel = (int32_t)(int16_t)HIWORD(wp) / WHEEL_DELTA;
      {
        POINT pt;
        pt.x = (LONG)(int16_t)LOWORD(lp);
        pt.y = (LONG)(int16_t)HIWORD(lp);
        ScreenToClient(hwnd, &pt);
        ev.x = (int32_t)pt.x;
        ev.y = (int32_t)pt.y;
      }
      push_event(&ev);
      return 0;

    case WM_IME_COMPOSITION: {
      HIMC imc = ImmGetContext(hwnd);
      if (imc != NULL) {
        if (lp & GCS_COMPSTR) {
          LONG n = ImmGetCompositionStringW(imc, GCS_COMPSTR, NULL, 0);
          if (n > 0) {
            int32_t chars = (int32_t)(n / (LONG)sizeof(WCHAR));
            if (chars > 255) chars = 255;
            ImmGetCompositionStringW(imc, GCS_COMPSTR, w->comp, n);
            w->comp[chars] = 0;
            w->comp_len = chars;
            ev.type = L_EV_IME_COMPOSE;
            push_event(&ev);
          } else {
            w->comp_len = 0;
          }
        }
        if (lp & GCS_RESULTSTR) {
          LONG n = ImmGetCompositionStringW(imc, GCS_RESULTSTR, NULL, 0);
          if (n > 0) {
            int32_t chars = (int32_t)(n / (LONG)sizeof(WCHAR));
            if (chars > 255) chars = 255;
            ImmGetCompositionStringW(imc, GCS_RESULTSTR, w->comp, n);
            w->comp[chars] = 0;
            w->comp_len = 0;
            ev.type = L_EV_IME_COMMIT;
            push_event(&ev);
          }
        }
        ImmReleaseContext(hwnd, imc);
      }
      return 0;
    }

    case WM_CLOSE:
      ev.type = L_EV_CLOSE;
      push_event(&ev);
      w->running = 0;
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wp, lp);
}

void* l_win32_open(const char* title, int32_t width, int32_t height) {
  if (g_win != NULL) return NULL;

  LWin32* w = (LWin32*)l_alloc(sizeof(LWin32));
  if (w == NULL) return NULL;
  memset(w, 0, sizeof(LWin32));
  w->active_font = -1;
  w->dpi = 96;

  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  WNDCLASSEXW wc;
  memset(&wc, 0, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = win32_proc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32513));  /* IDC_IBEAM */
  wc.lpszClassName = kClassName;
  if (RegisterClassExW(&wc) == 0) {
    l_free(w);
    return NULL;
  }

  WCHAR wtitle[512];
  wtitle[0] = 0;
  if (title != NULL) {
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle, 512);
  }

  RECT rc;
  rc.left = 0;
  rc.top = 0;
  rc.right = width;
  rc.bottom = height;
  AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);

  HWND hwnd = CreateWindowExW(0, kClassName, wtitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                              CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL,
                              GetModuleHandleW(NULL), NULL);
  if (hwnd == NULL) {
    l_free(w);
    return NULL;
  }

  w->hwnd = hwnd;
  g_win = w;
  w->dpi = (int32_t)GetDpiForWindow(hwnd);
  if (w->dpi <= 0) w->dpi = 96;
  update_client_size(w);
  back_create(w);
  clear_events(w);

  ShowWindow(hwnd, SW_SHOW);
  UpdateWindow(hwnd);
  w->running = 1;
  return w;
}

int32_t l_win32_pump(void) {
  if (g_win == NULL) return 0;
  MSG msg;
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      g_win->running = 0;
      return 0;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return g_win->running ? 1 : 0;
}

/* 1 when the head slot holds an event the caller has not consumed yet. */
int32_t l_win32_event_peek(void) {
  if (g_win == NULL) return 0;
  return g_win->ev_head != g_win->ev_tail ? 1 : 0;
}

int32_t l_win32_event_pop(void) {
  if (g_win == NULL) return 0;
  if (g_win->ev_head == g_win->ev_tail) return 0;
  g_win->ev_head = (g_win->ev_head + 1) % L_MAX_EVENTS;
  return 1;
}

int32_t l_win32_event_type(void) { return g_win != NULL ? g_win->events[g_win->ev_head].type : 0; }
int32_t l_win32_event_key(void) { return g_win != NULL ? g_win->events[g_win->ev_head].key : 0; }
int32_t l_win32_event_scancode(void) { return g_win != NULL ? g_win->events[g_win->ev_head].scancode : 0; }
int32_t l_win32_event_repeat(void) { return g_win != NULL ? g_win->events[g_win->ev_head].repeat : 0; }
int32_t l_win32_event_mouse_x(void) { return g_win != NULL ? g_win->events[g_win->ev_head].x : 0; }
int32_t l_win32_event_mouse_y(void) { return g_win != NULL ? g_win->events[g_win->ev_head].y : 0; }
int32_t l_win32_event_button(void) { return g_win != NULL ? g_win->events[g_win->ev_head].button : 0; }
int32_t l_win32_event_wheel(void) { return g_win != NULL ? g_win->events[g_win->ev_head].wheel : 0; }

int32_t l_win32_key_down(int32_t vk) {
  return (GetAsyncKeyState(vk) & 0x8000) ? 1 : 0;
}

int32_t l_win32_key_toggled(int32_t vk) {
  return (GetKeyState(vk) & 0x0001) ? 1 : 0;
}

int32_t l_win32_mouse_x(void) {
  POINT pt;
  if (g_win == NULL || !GetCursorPos(&pt)) return 0;
  ScreenToClient(g_win->hwnd, &pt);
  return (int32_t)pt.x;
}

int32_t l_win32_mouse_y(void) {
  POINT pt;
  if (g_win == NULL || !GetCursorPos(&pt)) return 0;
  ScreenToClient(g_win->hwnd, &pt);
  return (int32_t)pt.y;
}

int32_t l_win32_client_width(void) { return g_win != NULL ? g_win->width : 0; }
int32_t l_win32_client_height(void) { return g_win != NULL ? g_win->height : 0; }
int32_t l_win32_dpi(void) { return g_win != NULL ? g_win->dpi : 96; }

void l_win32_request_close(void) {
  if (g_win != NULL && g_win->hwnd != NULL) PostMessageW(g_win->hwnd, WM_CLOSE, 0, 0);
}

/* UTF-8 copy of the in-flight IME composition (empty string when none). */
static char* empty_string(void) {
  char* s = (char*)l_alloc(1);
  if (s != NULL) s[0] = 0;
  return s;
}

char* l_win32_ime_text(void) {
  if (g_win == NULL || g_win->comp_len <= 0) return empty_string();
  int bytes = WideCharToMultiByte(CP_UTF8, 0, g_win->comp, g_win->comp_len, NULL, 0, NULL, NULL);
  if (bytes <= 0) return empty_string();
  char* out = (char*)l_alloc((size_t)bytes + 1);
  if (out == NULL) return NULL;
  WideCharToMultiByte(CP_UTF8, 0, g_win->comp, g_win->comp_len, out, bytes, NULL, NULL);
  out[bytes] = 0;
  return out;
}

int32_t l_win32_set_title(const char* utf8) {
  if (g_win == NULL || g_win->hwnd == NULL || utf8 == NULL) return 0;
  WCHAR wtitle[512];
  if (MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wtitle, 512) <= 0) return 0;
  SetWindowTextW(g_win->hwnd, wtitle);
  return 1;
}

/* ------------------------------------------------------------------- GDI */

void l_gdi_frame_begin(void) {
  if (g_win == NULL) return;
  g_win->clip_depth = 0;
  if (g_win->back_dc != NULL) SelectClipRgn(g_win->back_dc, NULL);
}

void l_gdi_frame_end(void) {
  if (g_win == NULL || g_win->hwnd == NULL || g_win->back_dc == NULL) return;
  HDC dc = GetDC(g_win->hwnd);
  if (dc != NULL) {
    BitBlt(dc, 0, 0, g_win->width, g_win->height, g_win->back_dc, 0, 0, SRCCOPY);
    ReleaseDC(g_win->hwnd, dc);
  }
}

void l_gdi_clear(int32_t r, int32_t g, int32_t b) {
  if (g_win == NULL || g_win->back_dc == NULL) return;
  RECT full;
  full.left = 0;
  full.top = 0;
  full.right = g_win->width;
  full.bottom = g_win->height;
  HBRUSH br = CreateSolidBrush(to_color(r, g, b));
  if (br != NULL) {
    FillRect(g_win->back_dc, &full, br);
    DeleteObject(br);
  }
}

void l_gdi_rect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t g, int32_t b) {
  if (g_win == NULL || g_win->back_dc == NULL || w <= 0 || h <= 0) return;
  RECT rc;
  rc.left = x;
  rc.top = y;
  rc.right = x + w;
  rc.bottom = y + h;
  HBRUSH br = CreateSolidBrush(to_color(r, g, b));
  if (br != NULL) {
    FillRect(g_win->back_dc, &rc, br);
    DeleteObject(br);
  }
}

void l_gdi_hline(int32_t x, int32_t y, int32_t w, int32_t r, int32_t g, int32_t b) {
  l_gdi_rect(x, y, w, 1, r, g, b);
}

void l_gdi_vline(int32_t x, int32_t y, int32_t h, int32_t r, int32_t g, int32_t b) {
  l_gdi_rect(x, y, 1, h, r, g, b);
}

// Arbitrary segment. Uses MoveToEx/LineTo on the back DC so diagonals work;
// the 1px-wide pen mirrors hline/vline, which fill a 1px rect.
void l_gdi_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t r, int32_t g, int32_t b) {
  if (g_win == NULL || g_win->back_dc == NULL) return;
  HPEN pen = CreatePen(PS_SOLID, 1, to_color(r, g, b));
  if (pen == NULL) return;
  HGDIOBJ old_pen = SelectObject(g_win->back_dc, pen);
  MoveToEx(g_win->back_dc, x0, y0, NULL);
  LineTo(g_win->back_dc, x1, y1);
  SelectObject(g_win->back_dc, old_pen);
  DeleteObject(pen);
}

// Ellipse inscribed in the box; outline only, matching line()'s hairline pen.
void l_gdi_ellipse(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t g, int32_t b) {
  if (g_win == NULL || g_win->back_dc == NULL || w <= 0 || h <= 0) return;
  HPEN pen = CreatePen(PS_SOLID, 1, to_color(r, g, b));
  if (pen == NULL) return;
  HGDIOBJ old_pen = SelectObject(g_win->back_dc, pen);
  HGDIOBJ old_brush = SelectObject(g_win->back_dc, GetStockObject(NULL_BRUSH));
  Ellipse(g_win->back_dc, x, y, x + w, y + h);
  SelectObject(g_win->back_dc, old_brush);
  SelectObject(g_win->back_dc, old_pen);
  DeleteObject(pen);
}

void l_gdi_push_clip(int32_t x, int32_t y, int32_t w, int32_t h) {
  if (g_win == NULL || g_win->back_dc == NULL || g_win->clip_depth >= L_MAX_CLIP) return;
  RECT rc;
  rc.left = x;
  rc.top = y;
  rc.right = x + w;
  rc.bottom = y + h;
  g_win->clips[g_win->clip_depth++] = rc;
  HRGN rgn = CreateRectRgn(x, y, x + w, y + h);
  if (rgn != NULL) {
    SelectClipRgn(g_win->back_dc, rgn);
    DeleteObject(rgn);
  }
}

void l_gdi_pop_clip(void) {
  if (g_win == NULL || g_win->back_dc == NULL || g_win->clip_depth <= 0) return;
  g_win->clip_depth--;
  if (g_win->clip_depth == 0) {
    SelectClipRgn(g_win->back_dc, NULL);
    return;
  }
  RECT rc = g_win->clips[g_win->clip_depth - 1];
  HRGN rgn = CreateRectRgn(rc.left, rc.top, rc.right, rc.bottom);
  if (rgn != NULL) {
    SelectClipRgn(g_win->back_dc, rgn);
    DeleteObject(rgn);
  }
}

/* Returns a 1-based font handle (0 = failure) so callers can use 0 as "none". */
int32_t l_gdi_font(const char* face, int32_t size, int32_t bold) {
  if (g_win == NULL || g_win->font_count >= L_MAX_FONTS) return 0;
  if (size <= 0) size = 11;

  WCHAR wface[64];
  if (face == NULL || face[0] == 0 ||
      MultiByteToWideChar(CP_UTF8, 0, face, -1, wface, 64) <= 0) {
    wcscpy_s(wface, 64, L"Consolas");
  }

  int32_t px = -MulDiv(size, g_win->dpi, 72);
  HFONT f = CreateFontW(px, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, wface);
  if (f == NULL) return 0;
  g_win->fonts[g_win->font_count] = f;
  g_win->font_count++;
  return g_win->font_count;
}

void l_gdi_set_font(int32_t font) {
  if (g_win == NULL || g_win->back_dc == NULL) return;
  int32_t idx = font - 1;
  if (idx < 0 || idx >= g_win->font_count) return;
  SelectObject(g_win->back_dc, g_win->fonts[idx]);
  g_win->active_font = idx;
}

/* DrawText over a UTF-8 string converted to UTF-16 at the call boundary. */
void l_gdi_text(int32_t x, int32_t y, const char* utf8, int32_t r, int32_t g, int32_t b) {
  if (g_win == NULL || g_win->back_dc == NULL || utf8 == NULL || utf8[0] == 0) return;
  int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (n <= 0) return;
  WCHAR* wide = (WCHAR*)l_alloc((size_t)n * sizeof(WCHAR));
  if (wide == NULL) return;
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, n);
  SetTextColor(g_win->back_dc, to_color(r, g, b));
  TextOutW(g_win->back_dc, x, y, wide, n - 1);
  l_free(wide);
}

int32_t l_gdi_text_width(const char* utf8, int32_t font) {
  if (g_win == NULL || g_win->back_dc == NULL || utf8 == NULL || utf8[0] == 0) return 0;
  int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (n <= 0) return 0;
  WCHAR* wide = (WCHAR*)l_alloc((size_t)n * sizeof(WCHAR));
  if (wide == NULL) return 0;
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, n);

  HGDIOBJ prev = NULL;
  int32_t idx = font - 1;
  if (idx >= 0 && idx < g_win->font_count) {
    prev = SelectObject(g_win->back_dc, g_win->fonts[idx]);
  }
  SIZE sz;
  sz.cx = 0;
  sz.cy = 0;
  GetTextExtentPoint32W(g_win->back_dc, wide, n - 1, &sz);
  if (prev != NULL) SelectObject(g_win->back_dc, prev);
  l_free(wide);
  return (int32_t)sz.cx;
}

int32_t l_gdi_line_height(int32_t font) {
  if (g_win == NULL || g_win->back_dc == NULL) return 16;
  int32_t idx = font - 1;
  if (idx < 0 || idx >= g_win->font_count) return 16;
  HGDIOBJ prev = SelectObject(g_win->back_dc, g_win->fonts[idx]);
  TEXTMETRICW tm;
  memset(&tm, 0, sizeof(tm));
  GetTextMetricsW(g_win->back_dc, &tm);
  if (prev != NULL) SelectObject(g_win->back_dc, prev);
  return (int32_t)(tm.tmHeight + tm.tmExternalLeading);
}

#endif /* _WIN32 */
