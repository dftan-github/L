/* HTTPS GET via Windows Schannel (Phase 1). Non-Windows: returns NULL.
 * Returned buffer is l_alloc'd (free with l_free). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* l_alloc(size_t size);
void l_free(void* ptr);

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#define SECURITY_WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <schannel.h>
#include <security.h>
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "ws2_32.lib")

#ifndef SP_NAME_A
#define SP_NAME_A "Microsoft Unified Security Protocol Provider"
#endif

extern int32_t l_socket_create(void);
extern int32_t l_socket_connect_host(int32_t fd, const char* host, int32_t port);
extern void l_socket_close(int32_t fd);
extern int32_t l_socket_send(int32_t fd, const void* data, int32_t len);
extern int32_t l_socket_recv(int32_t fd, void* buffer, int32_t len);

static int send_all(SOCKET s, const char* data, int len) {
  int sent = 0;
  while (sent < len) {
    int n = send(s, data + sent, len - sent, 0);
    if (n == SOCKET_ERROR || n == 0) {
      return -1;
    }
    sent += n;
  }
  return 0;
}

/* Very small HTTPS GET: handshake + one request, accumulate decrypted plaintext. */
char* l_https_get(const char* host, int32_t port, const char* path) {
  if (host == NULL || path == NULL || port <= 0) {
    return NULL;
  }

  int32_t fd = l_socket_create();
  if (fd < 0) {
    return NULL;
  }
  if (l_socket_connect_host(fd, host, port) != 0) {
    l_socket_close(fd);
    return NULL;
  }
  SOCKET sock = (SOCKET)(UINT_PTR)(uint32_t)fd;

  SCHANNEL_CRED cred;
  ZeroMemory(&cred, sizeof(cred));
  cred.dwVersion = SCHANNEL_CRED_VERSION;
  cred.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
  /* Phase 1: skip cert chain validation so local/dev hosts still work. */
  cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION |
                 SCH_CRED_NO_SERVERNAME_CHECK;

  CredHandle cred_handle;
  TimeStamp lifetime;
  SECURITY_STATUS st =
      AcquireCredentialsHandleA(NULL, (SEC_CHAR*)UNISP_NAME_A, SECPKG_CRED_OUTBOUND, NULL, &cred,
                                NULL, NULL, &cred_handle, &lifetime);
  if (st != SEC_E_OK) {
    l_socket_close(fd);
    return NULL;
  }

  CtxtHandle ctx;
  ZeroMemory(&ctx, sizeof(ctx));
  ULONG ctx_attr = 0;
  SecBufferDesc out_desc;
  SecBuffer out_buf;
  out_buf.pvBuffer = NULL;
  out_buf.BufferType = SECBUFFER_TOKEN;
  out_buf.cbBuffer = 0;
  out_desc.ulVersion = SECBUFFER_VERSION;
  out_desc.cBuffers = 1;
  out_desc.pBuffers = &out_buf;

  DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY |
                ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;

  st = InitializeSecurityContextA(&cred_handle, NULL, (SEC_CHAR*)host, flags, 0, 0, NULL, 0,
                                  &ctx, &out_desc, &ctx_attr, &lifetime);
  if (st != SEC_I_CONTINUE_NEEDED && st != SEC_E_OK) {
    FreeCredentialsHandle(&cred_handle);
    l_socket_close(fd);
    return NULL;
  }
  if (out_buf.cbBuffer > 0 && out_buf.pvBuffer != NULL) {
    if (send_all(sock, (const char*)out_buf.pvBuffer, (int)out_buf.cbBuffer) != 0) {
      FreeContextBuffer(out_buf.pvBuffer);
      DeleteSecurityContext(&ctx);
      FreeCredentialsHandle(&cred_handle);
      l_socket_close(fd);
      return NULL;
    }
    FreeContextBuffer(out_buf.pvBuffer);
  }

  char recv_buf[16384];
  size_t recv_len = 0;
  int handshake_done = 0;
  while (!handshake_done) {
    int n = recv(sock, recv_buf + recv_len, (int)(sizeof(recv_buf) - recv_len), 0);
    if (n <= 0) {
      DeleteSecurityContext(&ctx);
      FreeCredentialsHandle(&cred_handle);
      l_socket_close(fd);
      return NULL;
    }
    recv_len += (size_t)n;

    SecBuffer in_bufs[2];
    in_bufs[0].pvBuffer = recv_buf;
    in_bufs[0].cbBuffer = (ULONG)recv_len;
    in_bufs[0].BufferType = SECBUFFER_TOKEN;
    in_bufs[1].pvBuffer = NULL;
    in_bufs[1].cbBuffer = 0;
    in_bufs[1].BufferType = SECBUFFER_EMPTY;
    SecBufferDesc in_desc;
    in_desc.ulVersion = SECBUFFER_VERSION;
    in_desc.cBuffers = 2;
    in_desc.pBuffers = in_bufs;

    out_buf.pvBuffer = NULL;
    out_buf.cbBuffer = 0;
    out_buf.BufferType = SECBUFFER_TOKEN;
    out_desc.ulVersion = SECBUFFER_VERSION;
    out_desc.cBuffers = 1;
    out_desc.pBuffers = &out_buf;

    st = InitializeSecurityContextA(&cred_handle, &ctx, (SEC_CHAR*)host, flags, 0, 0, &in_desc, 0,
                                    NULL, &out_desc, &ctx_attr, &lifetime);
    if (out_buf.cbBuffer > 0 && out_buf.pvBuffer != NULL) {
      send_all(sock, (const char*)out_buf.pvBuffer, (int)out_buf.cbBuffer);
      FreeContextBuffer(out_buf.pvBuffer);
    }
    if (st == SEC_E_OK) {
      handshake_done = 1;
      if (in_bufs[1].BufferType == SECBUFFER_EXTRA && in_bufs[1].cbBuffer > 0) {
        size_t extra = in_bufs[1].cbBuffer;
        memmove(recv_buf, recv_buf + (recv_len - extra), extra);
        recv_len = extra;
      } else {
        recv_len = 0;
      }
    } else if (st == SEC_I_CONTINUE_NEEDED) {
      if (in_bufs[1].BufferType == SECBUFFER_EXTRA && in_bufs[1].cbBuffer > 0) {
        size_t extra = in_bufs[1].cbBuffer;
        memmove(recv_buf, recv_buf + (recv_len - extra), extra);
        recv_len = extra;
      } else {
        recv_len = 0;
      }
    } else {
      DeleteSecurityContext(&ctx);
      FreeCredentialsHandle(&cred_handle);
      l_socket_close(fd);
      return NULL;
    }
  }

  char req[1024];
  int req_len = snprintf(req, sizeof(req),
                         "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
  if (req_len <= 0 || req_len >= (int)sizeof(req)) {
    DeleteSecurityContext(&ctx);
    FreeCredentialsHandle(&cred_handle);
    l_socket_close(fd);
    return NULL;
  }

  SecBufferDesc enc_desc;
  SecBuffer enc_bufs[4];
  SecPkgContext_StreamSizes sizes;
  st = QueryContextAttributes(&ctx, SECPKG_ATTR_STREAM_SIZES, &sizes);
  if (st != SEC_E_OK) {
    DeleteSecurityContext(&ctx);
    FreeCredentialsHandle(&cred_handle);
    l_socket_close(fd);
    return NULL;
  }
  size_t msg_size = sizes.cbHeader + (ULONG)req_len + sizes.cbTrailer;
  char* msg = (char*)malloc(msg_size);
  if (msg == NULL) {
    DeleteSecurityContext(&ctx);
    FreeCredentialsHandle(&cred_handle);
    l_socket_close(fd);
    return NULL;
  }
  memcpy(msg + sizes.cbHeader, req, (size_t)req_len);
  enc_bufs[0].pvBuffer = msg;
  enc_bufs[0].cbBuffer = sizes.cbHeader;
  enc_bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
  enc_bufs[1].pvBuffer = msg + sizes.cbHeader;
  enc_bufs[1].cbBuffer = (ULONG)req_len;
  enc_bufs[1].BufferType = SECBUFFER_DATA;
  enc_bufs[2].pvBuffer = msg + sizes.cbHeader + req_len;
  enc_bufs[2].cbBuffer = sizes.cbTrailer;
  enc_bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
  enc_bufs[3].pvBuffer = NULL;
  enc_bufs[3].cbBuffer = 0;
  enc_bufs[3].BufferType = SECBUFFER_EMPTY;
  enc_desc.ulVersion = SECBUFFER_VERSION;
  enc_desc.cBuffers = 4;
  enc_desc.pBuffers = enc_bufs;
  st = EncryptMessage(&ctx, 0, &enc_desc, 0);
  if (st != SEC_E_OK) {
    free(msg);
    DeleteSecurityContext(&ctx);
    FreeCredentialsHandle(&cred_handle);
    l_socket_close(fd);
    return NULL;
  }
  int wire = (int)(enc_bufs[0].cbBuffer + enc_bufs[1].cbBuffer + enc_bufs[2].cbBuffer);
  if (send_all(sock, msg, wire) != 0) {
    free(msg);
    DeleteSecurityContext(&ctx);
    FreeCredentialsHandle(&cred_handle);
    l_socket_close(fd);
    return NULL;
  }
  free(msg);

  size_t plain_cap = 8192;
  size_t plain_len = 0;
  char* plain = (char*)malloc(plain_cap);
  if (plain == NULL) {
    DeleteSecurityContext(&ctx);
    FreeCredentialsHandle(&cred_handle);
    l_socket_close(fd);
    return NULL;
  }
  plain[0] = '\0';

  recv_len = 0;
  for (;;) {
    if (recv_len + 2048 > sizeof(recv_buf)) {
      break;
    }
    int n = recv(sock, recv_buf + recv_len, (int)(sizeof(recv_buf) - recv_len), 0);
    if (n <= 0) {
      break;
    }
    recv_len += (size_t)n;

    SecBuffer bufs[4];
    bufs[0].pvBuffer = recv_buf;
    bufs[0].cbBuffer = (ULONG)recv_len;
    bufs[0].BufferType = SECBUFFER_DATA;
    bufs[1].BufferType = SECBUFFER_EMPTY;
    bufs[2].BufferType = SECBUFFER_EMPTY;
    bufs[3].BufferType = SECBUFFER_EMPTY;
    SecBufferDesc desc;
    desc.ulVersion = SECBUFFER_VERSION;
    desc.cBuffers = 4;
    desc.pBuffers = bufs;
    st = DecryptMessage(&ctx, &desc, 0, NULL);
    if (st == SEC_E_INCOMPLETE_MESSAGE) {
      continue;
    }
    if (st != SEC_E_OK && st != SEC_I_RENEGOTIATE) {
      break;
    }
    for (int i = 0; i < 4; ++i) {
      if (bufs[i].BufferType == SECBUFFER_DATA && bufs[i].pvBuffer && bufs[i].cbBuffer > 0) {
        size_t need = plain_len + bufs[i].cbBuffer + 1;
        if (need > plain_cap) {
          size_t ncap = need * 2;
          char* np = (char*)realloc(plain, ncap);
          if (np == NULL) {
            free(plain);
            DeleteSecurityContext(&ctx);
            FreeCredentialsHandle(&cred_handle);
            l_socket_close(fd);
            return NULL;
          }
          plain = np;
          plain_cap = ncap;
        }
        memcpy(plain + plain_len, bufs[i].pvBuffer, bufs[i].cbBuffer);
        plain_len += bufs[i].cbBuffer;
        plain[plain_len] = '\0';
      }
    }
    if (bufs[1].BufferType == SECBUFFER_EXTRA && bufs[1].cbBuffer > 0) {
      size_t extra = bufs[1].cbBuffer;
      memmove(recv_buf, (char*)bufs[1].pvBuffer, extra);
      recv_len = extra;
    } else {
      recv_len = 0;
    }
  }

  DeleteSecurityContext(&ctx);
  FreeCredentialsHandle(&cred_handle);
  l_socket_close(fd);

  /* Return via l_alloc so L can l_free safely. */
  {
    extern void* l_alloc(size_t size);
    char* out = (char*)l_alloc(plain_len + 1);
    if (out == NULL) {
      free(plain);
      return NULL;
    }
    memcpy(out, plain, plain_len + 1);
    free(plain);
    return out;
  }
}

#else
char* l_https_get(const char* host, int32_t port, const char* path) {
  (void)host;
  (void)port;
  (void)path;
  return NULL;
}
#endif
