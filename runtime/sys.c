/* Layer 1 C runtime: process argv, environment, and filesystem helpers. */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

/* From runtime/memory.c — format_time results are heap strings. */
void* l_alloc(size_t size);

static int32_t g_argc = 0;
static char** g_argv = NULL;

void l_init_args(int32_t argc, char** argv) {
  g_argc = argc < 0 ? 0 : argc;
  g_argv = argv;
}

int32_t l_get_arg_count(void) {
  return g_argc;
}

char* l_get_arg(int32_t index) {
  if (g_argv == NULL || index < 0 || index >= g_argc) {
    return "";
  }
  if (g_argv[index] == NULL) {
    return "";
  }
  return g_argv[index];
}

/* Returns getenv(name), or NULL when unset. Caller must not free. */
char* l_get_env(const char* name) {
  if (name == NULL) {
    return NULL;
  }
  return getenv(name);
}

/* 1 if path exists (file or directory), 0 otherwise. */
int32_t l_file_exists(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
#ifdef _WIN32
  DWORD attrs = GetFileAttributesA(path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return 0;
  }
  return 1;
#else
  struct stat st;
  if (stat(path, &st) != 0) {
    return 0;
  }
  return 1;
#endif
}

/* 0 on success, non-zero on failure. */
int32_t l_create_dir(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return -1;
  }
#ifdef _WIN32
  if (CreateDirectoryA(path, NULL)) {
    return 0;
  }
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    return -2;
  }
  return -1;
#else
  if (mkdir(path, 0755) == 0) {
    return 0;
  }
  if (errno == EEXIST) {
    return -2;
  }
  return -1;
#endif
}

/* 0 on success, non-zero on failure. */
int32_t l_remove_file(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return -1;
  }
#ifdef _WIN32
  if (DeleteFileA(path)) {
    return 0;
  }
  return -1;
#else
  if (unlink(path) == 0) {
    return 0;
  }
  return -1;
#endif
}

/* 0 on success, non-zero on failure. Empty directory only. */
int32_t l_remove_dir(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return -1;
  }
#ifdef _WIN32
  if (RemoveDirectoryA(path)) {
    return 0;
  }
  return -1;
#else
  if (rmdir(path) == 0) {
    return 0;
  }
  return -1;
#endif
}

/* Unix epoch milliseconds (UTC). */
int64_t l_time_now_ms(void) {
#ifdef _WIN32
  /* FILETIME: 100-ns ticks since 1601-01-01. Unix epoch = 11644473600s later. */
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  const uint64_t kEpochDiff100ns = 116444736000000000ULL;
  if (uli.QuadPart < kEpochDiff100ns) {
    return 0;
  }
  return (int64_t)((uli.QuadPart - kEpochDiff100ns) / 10000ULL);
#else
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  return (int64_t)ts.tv_sec * 1000LL + (int64_t)(ts.tv_nsec / 1000000L);
#endif
}

/* Unix epoch seconds (UTC). */
int64_t l_time_now_sec(void) {
  return l_time_now_ms() / 1000;
}

/* Format Unix seconds with strftime into an l_alloc'd C string. */
char* l_format_time(int64_t timestamp, const char* format_str) {
  if (format_str == NULL || format_str[0] == '\0') {
    char* empty = (char*)l_alloc(1);
    if (empty != NULL) {
      empty[0] = '\0';
    }
    return empty;
  }

  time_t t = (time_t)timestamp;
  struct tm tm_buf;
  struct tm* tm_ptr = NULL;
#ifdef _WIN32
  if (localtime_s(&tm_buf, &t) == 0) {
    tm_ptr = &tm_buf;
  }
#else
  tm_ptr = localtime_r(&t, &tm_buf);
#endif
  if (tm_ptr == NULL) {
    char* empty = (char*)l_alloc(1);
    if (empty != NULL) {
      empty[0] = '\0';
    }
    return empty;
  }

  char stack_buf[256];
  size_t n = strftime(stack_buf, sizeof(stack_buf), format_str, tm_ptr);
  if (n == 0) {
    /* Ambiguous: empty format result or buffer too small — grow once. */
    char big[1024];
    n = strftime(big, sizeof(big), format_str, tm_ptr);
    if (n == 0) {
      char* empty = (char*)l_alloc(1);
      if (empty != NULL) {
        empty[0] = '\0';
      }
      return empty;
    }
    char* out = (char*)l_alloc(n + 1);
    if (out == NULL) {
      return NULL;
    }
    memcpy(out, big, n + 1);
    return out;
  }

  char* out = (char*)l_alloc(n + 1);
  if (out == NULL) {
    return NULL;
  }
  memcpy(out, stack_buf, n + 1);
  return out;
}

/* Run a shell command via system(). Returns process exit code, or -1 on launch failure. */
int32_t l_run_command(const char* cmd) {
  if (cmd == NULL) {
    return -1;
  }
  int rc = system(cmd);
#ifdef _WIN32
  return (int32_t)rc;
#else
  if (rc == -1) {
    return -1;
  }
  if (WIFEXITED(rc)) {
    return (int32_t)WEXITSTATUS(rc);
  }
  return -1;
#endif
}

/* 0 on success. */
int32_t l_rename_path(const char* from, const char* to) {
  if (from == NULL || to == NULL || from[0] == '\0' || to[0] == '\0') {
    return -1;
  }
#ifdef _WIN32
  if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) {
    return 0;
  }
  return -1;
#else
  return rename(from, to) == 0 ? 0 : -1;
#endif
}

/* Copy file bytes. 0 on success. */
int32_t l_copy_file(const char* from, const char* to) {
  if (from == NULL || to == NULL || from[0] == '\0' || to[0] == '\0') {
    return -1;
  }
#ifdef _WIN32
  if (CopyFileA(from, to, FALSE)) {
    return 0;
  }
  return -1;
#else
  FILE* in = fopen(from, "rb");
  if (in == NULL) {
    return -1;
  }
  FILE* out = fopen(to, "wb");
  if (out == NULL) {
    fclose(in);
    return -1;
  }
  char buf[8192];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      fclose(in);
      fclose(out);
      return -1;
    }
  }
  fclose(in);
  fclose(out);
  return 0;
#endif
}

/* 1 if path is a directory, 0 otherwise. */
int32_t l_is_dir(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return 0;
  }
#ifdef _WIN32
  DWORD attrs = GetFileAttributesA(path);
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return 0;
  }
  return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
  struct stat st;
  if (stat(path, &st) != 0) {
    return 0;
  }
  return S_ISDIR(st.st_mode) ? 1 : 0;
#endif
}

/* Newline-joined directory entry names (no "." / ".."), malloc/l_alloc'd. NULL on failure. */
char* l_read_dir(const char* path) {
  if (path == NULL || path[0] == '\0') {
    return NULL;
  }
  size_t cap = 256;
  size_t len = 0;
  char* out = (char*)l_alloc(cap);
  if (out == NULL) {
    return NULL;
  }
  out[0] = '\0';

#ifdef _WIN32
  char pattern[MAX_PATH];
  size_t plen = strlen(path);
  if (plen + 3 >= sizeof(pattern)) {
    return out;
  }
  memcpy(pattern, path, plen);
  if (plen > 0 && path[plen - 1] != '\\' && path[plen - 1] != '/') {
    pattern[plen++] = '\\';
  }
  pattern[plen++] = '*';
  pattern[plen] = '\0';

  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) {
    return out;
  }
  do {
    if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
      continue;
    }
    size_t nlen = strlen(fd.cFileName);
    size_t need = len + nlen + 2;
    if (need > cap) {
      size_t ncap = cap * 2;
      while (ncap < need) {
        ncap *= 2;
      }
      char* nbuf = (char*)l_alloc(ncap);
      if (nbuf == NULL) {
        FindClose(h);
        return out;
      }
      memcpy(nbuf, out, len + 1);
      out = nbuf;
      cap = ncap;
    }
    if (len > 0) {
      out[len++] = '\n';
    }
    memcpy(out + len, fd.cFileName, nlen);
    len += nlen;
    out[len] = '\0';
  } while (FindNextFileA(h, &fd));
  FindClose(h);
#else
  DIR* d = opendir(path);
  if (d == NULL) {
    return out;
  }
  struct dirent* ent;
  while ((ent = readdir(d)) != NULL) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
      continue;
    }
    size_t nlen = strlen(ent->d_name);
    size_t need = len + nlen + 2;
    if (need > cap) {
      size_t ncap = cap * 2;
      while (ncap < need) {
        ncap *= 2;
      }
      char* nbuf = (char*)l_alloc(ncap);
      if (nbuf == NULL) {
        closedir(d);
        return out;
      }
      memcpy(nbuf, out, len + 1);
      out = nbuf;
      cap = ncap;
    }
    if (len > 0) {
      out[len++] = '\n';
    }
    memcpy(out + len, ent->d_name, nlen);
    len += nlen;
    out[len] = '\0';
  }
  closedir(d);
#endif
  return out;
}

typedef struct LProcessResult {
  int32_t exit_code;
  char* out_stdout;
  char* out_stderr;
} LProcessResult;

static char* read_stream_all(FILE* f) {
  size_t cap = 256;
  size_t len = 0;
  char* buf = (char*)malloc(cap);
  if (buf == NULL) {
    return NULL;
  }
  buf[0] = '\0';
  for (;;) {
    char tmp[1024];
    size_t n = fread(tmp, 1, sizeof(tmp), f);
    if (n == 0) {
      break;
    }
    if (len + n + 1 > cap) {
      size_t ncap = (len + n + 1) * 2;
      char* nbuf = (char*)realloc(buf, ncap);
      if (nbuf == NULL) {
        free(buf);
        return NULL;
      }
      buf = nbuf;
      cap = ncap;
    }
    memcpy(buf + len, tmp, n);
    len += n;
    buf[len] = '\0';
  }
  return buf;
}

/* Run via shell; capture stdout (stderr empty in Phase 1). */
void* l_process_run_capture(const char* cmd) {
  LProcessResult* r = (LProcessResult*)malloc(sizeof(LProcessResult));
  if (r == NULL) {
    return NULL;
  }
  r->exit_code = -1;
  r->out_stdout = (char*)malloc(1);
  r->out_stderr = (char*)malloc(1);
  if (r->out_stdout) {
    r->out_stdout[0] = '\0';
  }
  if (r->out_stderr) {
    r->out_stderr[0] = '\0';
  }
  if (cmd == NULL) {
    return r;
  }

#ifdef _WIN32
  FILE* pipe = _popen(cmd, "r");
#else
  FILE* pipe = popen(cmd, "r");
#endif
  if (pipe == NULL) {
    return r;
  }
  char* so = read_stream_all(pipe);
#ifdef _WIN32
  int code = _pclose(pipe);
#else
  int code = pclose(pipe);
  if (code != -1 && WIFEXITED(code)) {
    code = WEXITSTATUS(code);
  }
#endif
  if (so) {
    free(r->out_stdout);
    r->out_stdout = so;
  }
  r->exit_code = (int32_t)code;
  return r;
}

int32_t l_process_result_code(void* result) {
  if (result == NULL) {
    return -1;
  }
  return ((LProcessResult*)result)->exit_code;
}

char* l_process_result_stdout(void* result) {
  if (result == NULL) {
    return "";
  }
  char* s = ((LProcessResult*)result)->out_stdout;
  return s ? s : "";
}

char* l_process_result_stderr(void* result) {
  if (result == NULL) {
    return "";
  }
  char* s = ((LProcessResult*)result)->out_stderr;
  return s ? s : "";
}

void l_process_result_free(void* result) {
  if (result == NULL) {
    return;
  }
  LProcessResult* r = (LProcessResult*)result;
  free(r->out_stdout);
  free(r->out_stderr);
  free(r);
}

/* Graceful-shutdown flag (Ctrl+C / SIGINT). Default running=1. */
static volatile int32_t g_l_running = 1;

#ifdef _WIN32
static BOOL WINAPI l_console_handler(DWORD type) {
  if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
    g_l_running = 0;
    return TRUE;
  }
  return FALSE;
}
#else
static void l_sigint_handler(int sig) {
  (void)sig;
  g_l_running = 0;
}
#endif

void l_install_shutdown_handler(void) {
#ifdef _WIN32
  SetConsoleCtrlHandler(l_console_handler, TRUE);
#else
  signal(SIGINT, l_sigint_handler);
  signal(SIGTERM, l_sigint_handler);
#endif
}

int32_t l_is_running(void) {
  return g_l_running;
}

void l_request_shutdown(void) {
  g_l_running = 0;
}
