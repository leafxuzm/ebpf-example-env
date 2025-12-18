# 话题：如何知道程序启动时搜索库的路径
- 使用动态链接器调试输出
  - `LD_DEBUG=libs ./syscall 2>&1 | head -50`
  - 保存日志到文件：`LD_DEBUG=libs LD_DEBUG_OUTPUT=ld.log ./syscall`
  - 显示加载过程、搜索路径、找到的库与加载顺序
- 检查二进制内嵌路径
  - `readelf -d ./syscall | grep -E 'RPATH|RUNPATH|NEEDED'`
  - `RUNPATH/RPATH` 会影响搜索顺序与范围
- 系统缓存与默认目录
  - `ldconfig -v` 查看由 `/etc/ld.so.conf` 与 `/etc/ld.so.conf.d/*.conf` 指定的目录生成的 `ld.so.cache`
  - 默认目录通常包含：`/lib64`、`/usr/lib64`、`/lib`、`/usr/lib`
- 环境变量影响
  - `LD_LIBRARY_PATH`：临时追加搜索目录（最高优先级）
  - `LD_PRELOAD`：强制预加载指定库
  - `LD_DEBUG`：打印链接器调试信息（如 `libs`, `files`, `all`）
- 运行期尝试的打开调用
  - `strace -f -e openat ./syscall 2>&1 | grep '\.so'`
  - 观察实际 `.so` 文件的打开顺序与路径

# 话题：如何监控进程打开的库文件（运行期）
- 基线快照（已加载库集合）
  - `awk '/\.so(\.|$)/ && $NF ~ /^\// {print $NF}' /proc/<pid>/maps | sort -u`
  - 结合 `/proc/<pid>/comm` 与 `/proc/<pid>/status` 获取进程名与 UID/用户名
- 动态变化监控
  - 使用 bpftrace 钩住 `libc:dlopen/uretprobe`，记录运行期通过 `dlopen` 打开的库
  - 周期性打印当前集合，反映增量变化

# 话题：linked_libs_by_pid.sh 的用法
- 路径：`example/tracepoint/bpftrace/linked_libs_by_pid.sh`
- 功能：
  - 输入 PID，输出基线：进程名、PID、UID、用户名以及当前已链接库（来自 `/proc/<pid>/maps`）
  - 若存在 `bpftrace` 且具备权限，调用 `linked_libs_by_pid.bt` 做实时监控（捕获 `dlopen`）
- 用法：
  - `./linked_libs_by_pid.sh <pid>`
  - 示例：`./linked_libs_by_pid.sh 29435`
- 权限与环境：
  - 需要 root 运行 bpftrace；脚本会在 root 下直接执行，非 root 且有 `sudo` 则走 `sudo`
  - 容器/精简系统可能没有 `sudo`，可在 root 下直接运行
- 输出示例：
  - 基线：`baseline for pid=29435 comm=syscall uid=0 user=root` 后跟多行 `lib: /lib64/xxx.so`
  - 动态：`syscall(29435 uid=0) dlopen: /lib64/libbpf.so.1 -> 0x...`

# 话题：linked_libs_by_pid.bt 的说明
- 入口探针：`uprobe:libc:dlopen` 在调用时读取库名（`arg0`），按线程暂存
- 返回探针：`uretprobe:libc:dlopen` 在返回时读取库名与返回值（句柄），打印并加入集合
- 周期输出：`interval:s:5` 每 5 秒打印当前记录的库集合（仅“打开”事件累积）
- 仅记录 `dlopen`，不删除集合项（若需“完整当前状态”，请结合基线快照）

# 话题：syscalls_latency_hist.bt 的用法
- 路径：`example/tracepoint/bpftrace/syscalls_latency_hist.bt`
- 作用：对所有 `tracepoint:syscalls:sys_*` 事件记录直方图（`hist(nsecs)`），每 5 秒打印并清空
- 用法：
  - 运行：`sudo bpftrace example/tracepoint/bpftrace/syscalls_latency_hist.bt`
  - 可选：按需在脚本内增加进程过滤（例如 `/pid==1234/`）
- 注意：该脚本按事件时间做直方图示例，如需精确时长统计，可在 enter/exit 记录时间差
