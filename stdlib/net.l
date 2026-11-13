// Layer 1 C runtime TCP sockets (runtime/net.c).
extern module c_net {
    fun l_socket_create() -> i32
    fun l_socket_bind_and_listen(i32 fd, i32 port) -> i32
    fun l_socket_accept(i32 fd) -> i32
    fun l_socket_connect(i32 fd, ptr ip, i32 port) -> i32
    fun l_socket_connect_host(i32 fd, ptr host, i32 port) -> i32
    fun l_resolve_ipv4(ptr host) -> ptr
    fun l_socket_send(i32 fd, ptr data, i32 len) -> i32
    fun l_socket_recv(i32 fd, ptr buffer, i32 len) -> i32
    fun l_socket_close(i32 fd) -> void
    fun l_https_get(ptr host, i32 port, ptr path) -> ptr
}
