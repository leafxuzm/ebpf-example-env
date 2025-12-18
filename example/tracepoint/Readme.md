# Tracepoint 原理与实践

## 核心原理
- 静态插桩：内核在关键路径通过 `TRACE_EVENT()` 等宏放置静态追踪点，编译期生成事件数据结构、打印模板和钩子入口。
- 回调链表：每个追踪点对应一个 `struct tracepoint`，维护已注册的回调数组（ftrace、perf、bpftrace、eBPF 等消费者各自注册）。
- 静态跳转标签（static key/jump label）：追踪点周围的分支由静态键控制。未启用时执行 `NOP`，启用时原地打补丁为跳转，关闭开销近似为零。
  - 实现细节：
    - `struct static_key` 维护引用计数；当有消费者启用事件（ftrace/perf/eBPF）时增加计数，全部关闭时降为 0。
    - 编译期在追踪点插入“可打补丁的分支位点”（jump label site），初始机器码为 `NOP`（不跳转）。
    - 启用路径：`static_key_enable()` 通过 jump label 框架在所有 CPU 上把该位点从 `NOP` 动态改写为 `JMP`（x86 典型为 5 字节跳转），指向回调分发逻辑；使用 `text_poke`/架构特定机制在内核只读代码段安全打补丁，并进行跨 CPU 同步。
    - 关闭路径：`static_key_disable()` 在引用计数降为 0 时把机器码从 `JMP` 恢复为 `NOP`，恢复零开销的直通路径。
    - 读路径：运行时检查通过 `static_branch_unlikely(&tp->key)` 等宏展开为极低成本的分支预测与内存读取，不涉及锁；仅在启用时才进入回调链表。
    - 多消费者：多个子系统同时启用同一追踪点时共享同一个 static key 的引用计数，保证位点保持 `JMP`，直到计数清零才回退。
- 快速路径：执行到追踪点时先检查启用状态；启用则迭代调用回调并把事件数据传递给各子系统。

## 事件数据与接口
- 事件格式：为每个事件生成 `format` 元数据（字段名、类型、偏移），位于 `tracefs` 路径 `/sys/kernel/debug/tracing/events/<subsys>/<event>/format`。
- 控制文件：
  - `enable`：开关事件
  - `filter`：内核态过滤（表达式匹配事件字段）
  - `trigger`：触发动作（如 `stacktrace`、`snapshot`、`traceon/off`）
- 上下文结构：不同子系统事件对应不同结构体；系统调用入口通常使用 `trace_event_raw_sys_enter`。

## eBPF 结合
- 程序类型：`BPF_PROG_TYPE_TRACEPOINT`，通过节区名 `tp/<subsys>/<event>` 挂载，如 `tp/syscalls/sys_enter_execve`。
- 类型信息：借助 BTF 与 `vmlinux.h`，BPF 程序可直接访问上下文字段。
- 数据输出：eBPF 回调可写入 `perf buffer`/`ring buffer`，由用户态读取并消费（打印、聚合、报警）。

## 性能与稳定性
- 未启用时几乎零开销（`NOP`）；启用后走回调链表，尽量少锁少拷贝。
- 稳定 ABI：tracepoint 字段定义相对稳定，比 kprobe 更不易受内核内部实现变化影响。
- 数据流隔离：不同消费者共享事件但通道独立，互不干扰。

## 与 kprobe 对比
- tracepoint：静态、稳定字段、开销可控，适合长期观测与聚合。
- kprobe：动态、灵活，几乎可钩任意函数，但上下文是寄存器组，需要手动取参，更易受版本影响。

## 查看与启用
- 列出事件：`bpftrace -l 'tracepoint:*'`
- 查看字段：`bpftrace -lv 'tracepoint:syscalls:sys_enter_execve'`
- Tracefs 控制示例：
  - `echo 1 > /sys/kernel/debug/tracing/events/syscalls/sys_enter_execve/enable`
  - `echo 'common_pid==1234' > /sys/kernel/debug/tracing/events/syscalls/sys_enter_execve/filter`

## 本仓示例指引
- eBPF Tracepoint 程序：`example/tracepoint/syscall.bpf.c`（节区 `tp/syscalls/sys_enter_execve`）
- 用户态加载与消费：`example/tracepoint/syscall.c`（libbpf 加载、ring buffer 传输、工作线程打印）
- 构建脚本：`example/tracepoint/Makefile`（生成 skeleton、链接 libbpf）
- bpftrace 脚本：
  - 文件打开监控：`example/tracepoint/bpftrace/open_files_by_pid.bt`

## 最小实践流程
- 编译运行 eBPF 示例：
  - `make -C example/tracepoint vmlinux && make -C example/tracepoint`
  - `sudo ./example/tracepoint/syscall`
- 使用 bpftrace 观察系统调用：
  - `sudo bpftrace -e 'tracepoint:syscalls:sys_enter_openat { printf("%s %s\\n", comm, str(args->filename)); }'`
