#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

// 1. 定义一个 Ring Buffer Map，名字叫 "rb"
struct {
    __uint(type, BPF_MAP_TYPE_RINGBUF);
    __uint(max_entries, 256 * 1024); // 大小必须是 4KB 的倍数，这里是 256KB
} rb SEC(".maps");

// 2. 定义你想传给用户态的数据结构
struct event {
    int pid;
    __u32 uid;
    char comm[16];
    char filename[128];
};

SEC("tp/syscalls/sys_enter_execve")
int tracepoint__syscalls__sys_enter_execve(struct trace_event_raw_sys_enter *ctx)
{
    struct event *e;
    
    // 3. 在 Ring Buffer 中预留空间
    e = bpf_ringbuf_reserve(&rb, sizeof(*e), 0);
    if (!e) {
        return 0; // 没空间了，丢弃
    }

    // 4. 填充数据
    e->pid = bpf_get_current_pid_tgid() >> 32;
    e->uid = (__u32)(bpf_get_current_uid_gid() & 0xffffffff);
    bpf_get_current_comm(&e->comm, sizeof(e->comm));
    bpf_probe_read_user_str(&e->filename, sizeof(e->filename), (const char *)ctx->args[0]);

    // 5. 提交数据到 Ring Buffer
    bpf_ringbuf_submit(e, 0);

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
