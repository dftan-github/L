// 边界 5: sync 内部 `?` 提前返回, 锁必须被释放 (否则死锁 + 计数失衡).
// 验证方式不靠"没卡住", 而是 lock_count == unlock_count.

fun risky(i32 n) -> Result<i32, String> {
    if n == 0 {
        return Result<i32, String>(false, 0, "boom")
    }
    return Result<i32, String>(true, n, "")
}

// `?` inside sync: Err path must flush held sync locks before returning.
fun worker_sync_try(i32 n) -> Result<i32, String> {
    sync {
        v := risky(n)?
        return Result<i32, String>(true, v + 1, "")
    }
}

fun main() -> i32 {
    l_reset_lock_counts()

    // 单线程路径: 成功与失败各走一遍.
    ok := worker_sync_try(5)
    print(ok.unwrap())          // 6
    bad := worker_sync_try(0)
    print(bad.unwrap_err())     // boom

    a := l_get_lock_count()
    b := l_get_unlock_count()
    print(a)
    print(b)

    // 多线程: 4 个线程, 每个在 sync 内触发 ? 提前返回.
    h0 := spawn { worker_sync_try(0) }
    h1 := spawn { worker_sync_try(0) }
    h2 := spawn { worker_sync_try(0) }
    h3 := spawn { worker_sync_try(0) }
    l_thread_join(h0)
    l_thread_join(h1)
    l_thread_join(h2)
    l_thread_join(h3)

    // 锁若未释放, 这里会永久阻塞; 能打印出来即证明没有死锁.
    print(l_get_lock_count())
    print(l_get_unlock_count())

    sync {
        print("recovered")
    }

    return 0
}
