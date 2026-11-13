/* Layer 1 C runtime: TCP sockets (Winsock2 / BSD sockets). */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef _WIN32
static INIT_ONCE g_wsa_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK init_winsock(PINIT_ONCE once, PVOID param, PVOID* ctx) {
  (void)once;
  (void)param;
  (void)ctx;
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    return FALSE;
  }
  return TRUE;
}

static int ensure_net(void) {
  PVOID ctx = NULL;
  if (InitOnceExecuteOnce(&g_wsa_once, init_winsock, NULL, &ctx) == FALSE) {
    return -1;
  }
  return 0;
}

static int sock_err(void) {
  return WSAGetLastError();
}

static void sock_close(SOCKET s) {
  closesocket(s);
}
#else
static int ensure_net(void) {
  return 0;
}

static int sock_err(void) {
  return errno;
}

static void sock_close(int s) {
  close(s);
}
#endif

int32_t l_socket_create(void) {
  if (ensure_net() != 0) {
    return -1;
  }
#ifdef _WIN32
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) {
    return -1;
  }
  return (int32_t)s;
#else
  int s = (int)socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    return -1;
  }
  return (int32_t)s;
#endif
}

int32_t l_socket_bind_and_listen(int32_t fd, int32_t port) {
  if (fd < 0 || port <= 0 || port > 65535) {
    return -1;
  }
  if (ensure_net() != 0) {
    return -1;
  }

  int yes = 1;
#ifdef _WIN32
  SOCKET s = (SOCKET)(UINT_PTR)(uint32_t)fd;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#else
  int s = (int)fd;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)port);

#ifdef _WIN32
  if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
    (void)sock_err();
    return -1;
  }
  if (listen(s, 8) == SOCKET_ERROR) {
    (void)sock_err();
    return -1;
  }
#else
  if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    (void)sock_err();
    return -1;
  }
  if (listen(s, 8) != 0) {
    (void)sock_err();
    return -1;
  }
#endif
  return 0;
}

int32_t l_socket_accept(int32_t fd) {
  if (fd < 0) {
    return -1;
  }
  if (ensure_net() != 0) {
    return -1;
  }
#ifdef _WIN32
  SOCKET s = (SOCKET)(UINT_PTR)(uint32_t)fd;
  SOCKET c = accept(s, NULL, NULL);
  if (c == INVALID_SOCKET) {
    return -1;
  }
  return (int32_t)c;
#else
  int c = accept((int)fd, NULL, NULL);
  if (c < 0) {
    return -1;
  }
  return (int32_t)c;
#endif
}

int32_t l_socket_connect(int32_t fd, const char* ip, int32_t port) {
  if (fd < 0 || ip == NULL || port <= 0 || port > 65535) {
    return -1;
  }
  if (ensure_net() != 0) {
    return -1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
#ifdef _WIN32
  if (InetPtonA(AF_INET, ip, &addr.sin_addr) != 1) {
    return -1;
  }
  SOCKET s = (SOCKET)(UINT_PTR)(uint32_t)fd;
  if (connect(s, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
    (void)sock_err();
    return -1;
  }
#else
  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
    return -1;
  }
  if (connect((int)fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
    (void)sock_err();
    return -1;
  }
#endif
  return 0;
}

int32_t l_socket_send(int32_t fd, const void* data, int32_t len) {
  if (fd < 0 || data == NULL || len < 0) {
    return -1;
  }
  if (len == 0) {
    return 0;
  }
#ifdef _WIN32
  SOCKET s = (SOCKET)(UINT_PTR)(uint32_t)fd;
  int n = send(s, (const char*)data, (int)len, 0);
  if (n == SOCKET_ERROR) {
    return -1;
  }
  return (int32_t)n;
#else
  ssize_t n = send((int)fd, data, (size_t)len, 0);
  if (n < 0) {
    return -1;
  }
  return (int32_t)n;
#endif
}

/* Recv into buffer[0..len). Always NUL-terminates when len > 0 by leaving one
 * byte for '\0' (reads at most len-1 payload bytes). Returns payload bytes. */
int32_t l_socket_recv(int32_t fd, void* buffer, int32_t len) {
  if (fd < 0 || buffer == NULL || len <= 0) {
    return -1;
  }
  char* buf = (char*)buffer;
  memset(buf, 0, (size_t)len);
  if (len == 1) {
    return 0;
  }
  const int32_t max_payload = len - 1;
#ifdef _WIN32
  SOCKET s = (SOCKET)(UINT_PTR)(uint32_t)fd;
  int n = recv(s, buf, (int)max_payload, 0);
  if (n == SOCKET_ERROR) {
    buf[0] = '\0';
    return -1;
  }
  buf[n] = '\0';
  return (int32_t)n;
#else
  ssize_t n = recv((int)fd, buf, (size_t)max_payload, 0);
  if (n < 0) {
    buf[0] = '\0';
    return -1;
  }
  buf[n] = '\0';
  return (int32_t)n;
#endif
}

void l_socket_close(int32_t fd) {
  if (fd < 0) {
    return;
  }
#ifdef _WIN32
  sock_close((SOCKET)(UINT_PTR)(uint32_t)fd);
#else
  sock_close((int)fd);
#endif
}

/* Resolve hostname to IPv4 dotted string (malloc'd). NULL on failure. */
char* l_resolve_ipv4(const char* host) {
  if (host == NULL || host[0] == '\0') {
    return NULL;
  }
  if (ensure_net() != 0) {
    return NULL;
  }
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = NULL;
  if (getaddrinfo(host, NULL, &hints, &res) != 0 || res == NULL) {
    return NULL;
  }
  char buf[64];
  buf[0] = '\0';
#ifdef _WIN32
  struct sockaddr_in* sa = (struct sockaddr_in*)res->ai_addr;
  if (InetNtopA(AF_INET, &sa->sin_addr, buf, (ULONG)sizeof(buf)) == NULL) {
    freeaddrinfo(res);
    return NULL;
  }
#else
  struct sockaddr_in* sa = (struct sockaddr_in*)res->ai_addr;
  if (inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf)) == NULL) {
    freeaddrinfo(res);
    return NULL;
  }
#endif
  freeaddrinfo(res);
  size_t n = strlen(buf);
  char* out = (char*)malloc(n + 1);
  if (out == NULL) {
    return NULL;
  }
  memcpy(out, buf, n + 1);
  return out;
}

/* Connect using hostname or dotted IPv4. */
int32_t l_socket_connect_host(int32_t fd, const char* host, int32_t port) {
  if (fd < 0 || host == NULL || port <= 0 || port > 65535) {
    return -1;
  }
  if (ensure_net() != 0) {
    return -1;
  }
  char port_str[16];
  snprintf(port_str, sizeof(port_str), "%d", (int)port);
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* res = NULL;
  if (getaddrinfo(host, port_str, &hints, &res) != 0 || res == NULL) {
    return -1;
  }
  int rc = -1;
#ifdef _WIN32
  SOCKET s = (SOCKET)(UINT_PTR)(uint32_t)fd;
  if (connect(s, res->ai_addr, (int)res->ai_addrlen) != SOCKET_ERROR) {
    rc = 0;
  }
#else
  if (connect((int)fd, res->ai_addr, (socklen_t)res->ai_addrlen) == 0) {
    rc = 0;
  }
#endif
  freeaddrinfo(res);
  return rc;
}
