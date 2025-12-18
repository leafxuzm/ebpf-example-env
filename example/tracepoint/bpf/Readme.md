# 如何找到内核追踪挂载点？
## Tracefs 静态追踪点文件系统
> Linux 内核通过一个特殊的虚拟文件系统（通常是 Tracefs 或 Debugfs）将所有的静态追踪点（Tracepoints）暴露给用户空间。 

静态追踪点挂载路径：
```
cat /proc/mounts | grep -E 'tracefs|debugfs'

debugfs /sys/kernel/debug debugfs rw,nosuid,nodev,noexec,relatime 0 0
tracefs /sys/kernel/debug/tracing tracefs rw,nosuid,nodev,noexec,relatime 0 0
```

静态追踪点位置和列表信息：
```aiignore
ls /sys/kernel/debug/tracing/events/
```

## 观察内核：BPF 程序 和 Tracefs 文件系统

### Ftrace接口 （途径1:Tracefs文件系统控制）

静态追踪点目录结构介绍：
```
ls -al /sys/kernel/debug/tracing/events/syscalls/sys_enter_execve
total 0
    drwxr-xr-x 1 root root 0 Dec 12 06:52 .
    drwxr-xr-x 1 root root 0 Dec 12 06:52 ..
    -rw-r----- 1 root root 0 Dec 12 08:57 enable
    -rw-r----- 1 root root 0 Dec 12 08:57 filter
    -r--r----- 1 root root 0 Dec 12 06:53 format
    -r--r----- 1 root root 0 Dec 12 08:57 id
    -rw-r----- 1 root root 0 Dec 12 08:57 trigger
````

以下是这些文件的详细作用解析：
1. id (身份证号)：该事件在当前运行内核中的唯一数字标识符。作用：供 perf、bpftrace 等工具使用。<br><br>
2. enable (开关)：控制是否启用该事件的追踪。 <br>内核会通过 "Static Jump Label" 技术，动态地将内核代码中的 nop 指令替换为跳转指令， 开始执行追踪回调函数。<br><br> 
3. format (说明书): 这是给工具软件（如 bpftrace, perf, trace-cmd）看的数据结构定义。<br><br>
4. filter (过滤器)：<br>
    内核态过滤，不符合条件的事件会被直接丢弃，不会唤醒用户态进程，性能极高。<br>
    用法示例： 追踪 PID 为 100 的进程：echo "common_pid == 100" > filter <br>
    清除过滤： echo 0 > filter  <br><br>
5. trigger (触发器)：设置**“当事件发生时，顺便干点别的动作”。常见动作：
    ```
        traceon/traceoff：触发时，开启或关闭整个追踪系统（用于捕捉“案发现场”）。
        stacktrace：      触发时，顺便记录当前的内核调用栈。
        snapshot：        触发时，对当前的 Trace Buffer 进行快照备份。
    
        用法示例：
        echo "stacktrace" > trigger：以后每次 execve 都会带上调用栈。
    ```

### BPF 程序 （途径2: 系统调用控制）
SEC 编译器指令定义
```
  #define SEC(NAME) __attribute__((section(NAME), used))
```

SEC 编译器指令的作用
```
  类型识别：内核根据节区名识别程序类型
  自动附加：工具（如 bpftool）可以自动附加程序
  组织管理：多个程序可以放在同一个 ELF 文件中
  元数据关联：与 maps、license 等关联

    maps：与 BPF 程序相关的全局变量（BPF maps）也需要放在 .maps 节区。
    struct {
        __uint(type, BPF_MAP_TYPE_HASH);
        __uint(max_entries, 1024);
        __type(key, u32);
        __type(value, u64);
    } exec_count SEC(".maps");  // 放到 .maps 节区


    char LICENSE[] SEC("license") = "GPL";
    int VERSION SEC("version") = LINUX_VERSION_CODE;
```

SEC 编译器指令运行时
```
  用户空间加载器（如 libbpf）会：
     1. 读取 ELF 文件
     2. 查找特定节区名的程序
     3. 根据节区名确定程序类型
     4. 加载到对应的内核钩子点
```

SEC 编译器指令例子
```
    SEC("tp/syscalls/sys_enter_execve")

    sec()：通知 clang 把代码放到 eBPF 程序的节区（section）
    tp/：表示这是跟踪点（tracepoint）类型的程序
    syscalls/：跟踪点类别是系统调用
    sys_enter_execve：具体跟踪系统调用 execve() 的进入点
```

### 两种方式的关系
监听同一个事件，但它们的数据流是完全隔离的<br><br>
当你在内核里“开启”一个 Tracepoint 时，内核实际上是在维护一个 回调函数链表 (Callback List)。
1. 当你 echo 1 > enable 时，Ftrace 向这个链表注册了一个回调函数，它的工作是“把数据格式化成文本，写到 /sys/.../trace 文件里”。<br>
2. 当你运行 bpftrace 时，BPF 向同一个链表注册了另一个回调函数，它的工作是“运行你的 BPF 字节码，更新 Map 或打印输出”。

# 如何找到静态追踪点参数结构体定义
## BPF 程序函数名和结构体定义
### BPF 程序结构体定义
> 结构体定义来源 : bpftool btf dump file /sys/kernel/btf/vmlinux format c > vmlinux.h

不同的 BPF 程序类型（Probe 类型）对应着完全不同的上下文结构体。

#### Tracepoint (SEC "tp/..."): 
1. 上下文类型： struct trace_event_raw_<事件名>
2. syscalls 比较特殊。所有系统调用的入口通常都复用一个通用的结构体 struct trace_event_raw_sys_enter.

#### Kprobe / Kretprobe (SEC "kprobe/...")
1. 上下文类型： struct pt_regs *ctx
2. Kprobe 基于断点机制，它拿到的上下文永远是CPU 寄存器组。 需要通过 PT_REGS_PARM1(ctx) 等宏（定义在 bpf_tracing.h）来获取函数参数。

#### Fentry / Fexit (SEC "fentry/..." 或 "fexit/...")
1. 上下文类型： u64 *ctx (原始) 或 直接写函数签名 (推荐)
2. 较新的 BPF 挂载方式（需要内核 5.5+）。它允许你直接把 BPF 程序写得像内核函数一样。

```
    // 不需要自己找 ctx，宏帮你搞定
    SEC("fentry/do_unlinkat")
    int BPF_PROG(do_unlinkat_entry, int dfd, struct filename *name) {
        // 直接用 name，不需要从 ctx 里提取
        return 0;
    }
```

#### 网络相关 (XDP / TC)
1. SEC("xdp") 上下文类型: struct xdp_md *ctx 
2. SEC("tc") (或 classifier) 上下文类型: struct __sk_buff *ctx

### BPF 程序函数名
1. 没有特殊要求。

# 如何运行例子
## 环境准备
1. 创建镜像: docker build -t ebpf:centos9 -f dockerfile .
2. 创建容器
    ```aiignore
    docker run -it --name ebpf-centos9-dev \
    --privileged --pid=host \
    -v /usr/src/kernels:/usr/src/kernels:rw \
    -v /lib/modules:/lib/modules:ro \
    -v /sys/kernel/debug:/sys/kernel/debug:rw \
    -v /sys/kernel/btf/vmlinux:/sys/kernel/btf/vmlinux:ro \
    quay.io/centos/centos:stream9 /bin/bash
    
    ```
3. 进入容器：docker exec -it 容器ID bash。
4. 把 tracepoint 代码拷贝进容器  /workspace/projects 目录下。docker cp tracepoint/ 容器ID:/workspace/projects

## 程序编译
1. 在容器中：cd /workspace/projects
2. 执行：make vmlinux && make 
3. 运行：syscall

## 其他待优化
1. tracepoint只能跟踪，不能拦截系统调用
2. handle_event 是用户态代码，你可以做任何 Linux 进程能做的事。
3. 千万不要在 handle_event 里做耗时操作（发邮件、大量打印、重型计算）。
4. 使用内存队列 + 工作线程。主线程 handle_event 负责“收货”，工作线程负责“发货”。
5. bash 中 cd 命令只是 Shell 进程内部的一个动作，并没有“执行一个新的程序”。所以追踪不到