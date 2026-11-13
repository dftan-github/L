// TCP loopback: server + client OS threads.
// Port 38473 (8080 is often taken by other local services).

fun client_connect() -> i32 {
    i! := 0
    while i < 40 {
        fd := l_socket_create()
        if l_socket_connect(fd, "127.0.0.1", 38473) == 0 {
            return fd
        }
        l_socket_close(fd)
        l_sleep_ms(50)
        i! = i + 1
    }
    return 0 - 1
}

fun run_server(ptr out_buf) -> i32 {
    sfd := l_socket_create()
    if sfd < 0 {
        return 0 - 1
    }
    if l_socket_bind_and_listen(sfd, 38473) != 0 {
        l_socket_close(sfd)
        return 0 - 2
    }
    cfd := l_socket_accept(sfd)
    if cfd >= 0 {
        l_socket_recv(cfd, out_buf, 64)
        ptr reply = "Hello Client"
        l_socket_send(cfd, reply, l_str_len(reply))
        l_socket_close(cfd)
    }
    l_socket_close(sfd)
    return 0
}

fun run_client(ptr out_buf) -> i32 {
    cfd := client_connect()
    if cfd < 0 {
        return 0 - 1
    }
    ptr hello = "Hello Server"
    l_socket_send(cfd, hello, l_str_len(hello))
    l_socket_recv(cfd, out_buf, 64)
    l_socket_close(cfd)
    return 0
}

fun main() -> i32 {
    ptr server_buf = l_alloc(64)
    ptr client_buf = l_alloc(64)

    hs := spawn {
        run_server(server_buf)
    }

    l_sleep_ms(200)

    hc := spawn {
        run_client(client_buf)
    }

    l_thread_join(hs)
    l_thread_join(hc)

    print(String(server_buf))
    print(String(client_buf))

    l_free(server_buf)
    l_free(client_buf)
    return 0
}
