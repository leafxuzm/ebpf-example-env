## 系统调用跟踪实际需求案例

- 文件访问审计：监控敏感路径读写，识别异常进程
- 进程启动画像：统计 execve 的命令、用户、失败原因
- 慢系统调用定位：对 sys_exit_* 时长做直方图，找长尾
- 高频短生命周期进程：检测频繁 fork/exec/exit 的服务

```
示例：sudo bpftrace -e 'tracepoint:syscalls:sys_enter_openat { @cnt[str(args->filename)]++; } interval:s:5 { print(@cnt); clear(@cnt); }'
```

## 调度与CPU

- 线程饥饿与调度延迟：测 sched_wakeup 到 sched_switch 的等待时间
- 抖动与跨核迁移：统计 sched:sched_migrate_task 次数与来源-目标 CPU
- 热点线程：找被频繁切出/切入的进程，定位过度抢占

```
示例：sudo bpftrace -e 'tracepoint:sched:sched_switch { @sw[str(args->prev_comm),str(args->next_comm)]++; } interval:s:5 { print(@sw); clear(@sw); }'
```

## 磁盘与块层

- I/O 延迟：block_rq_issue/complete 计算读写耗时与直方图
- 队列深度与吞吐：按设备聚合流量与并发
- 进程热点：统计进程触发的块层请求分布

```
示例：sudo bpftrace -e 'tracepoint:block:block_rq_issue { @ts[args->cookie]=nsecs; } tracepoint:block:block_rq_complete /@ts[args->cookie]/ { @lat[args->rwbs]=hist((nsecs-@ts[args->cookie])/1e6); delete(@ts[args->cookie]); }'
```

## 文件系统

- 元数据热点：ext4_unlink_enter/rename 统计路径热点
- 写回风暴：writeback:* 观察后台写回节奏与长尾
- 同步压力：fsync/fdatasync 直方图分析

```
示例：sudo bpftrace -e 'tracepoint:ext4:ext4_rename_enter { @rn[str(args->oldname)]++; } interval:s:5 { print(@rn); clear(@rn); }'
```

## 网络与TCP

- 重传与拥塞：tcp_retransmit_skb 统计按进程/端口
- 建连异常：tcp_connect/tcp_reset 记录失败原因与远端
- 零窗口/背压：tcp_probe 类事件识别窗口更新问题（视内核启用）

```
示例：sudo bpftrace -e 'tracepoint:tcp:tcp_retransmit_skb { @rt[str(comm)]++; } interval:s:5 { print(@rt); clear(@rt); }'
```

## 内存与OOM

- OOM 诊断：oom:oom_kill_process 记录被杀进程与触发者
- 回收与抖动：vmscan:mm_vmscan_* 观察回收频率与停顿
- 大页/分配失败：mm:compaction_*、kmem:mm_page_alloc 分析

```
示例：sudo bpftrace -e 'tracepoint:oom:oom_kill_process { printf("oom kill: %s pid=%d\n", str(args->comm), args->pid); }'
```

## 中断/定时器/工作队列

- 中断风暴：irq:* 统计设备与频率，定位异常硬件/驱动
- 软中断占用：softirq:* 发现网络/块层软中断过载
- 定时器过期延迟：timer:* 估算延迟与调度压力
- 工作队列长尾：workqueue_execute_start/exec_* 识别执行时间过长任务

```
示例：sudo bpftrace -e 'tracepoint:workqueue:workqueue_execute_start { @wq[str(args->function)]++; } interval:s:5 { print(@wq); clear(@wq); }'
```

## 实践建议

- 先用 sudo bpftrace -lv 'tracepoint:...': 查看字段再编写过滤
- 高频事件务必做聚合或取样，避免大量打印影响性能
- 与 open_files_by_pid.bt、linked_libs_by_pid.bt 搭配，用 bpftrace 快速验证，再沉淀到 eBPF C 程序生产化
- 容器内无 sudo 时用 root 运行；字段名与可用 tracepoint 受内核版本影响，必要时用 bpftrace -l 'tracepoint:*' 列出并适配
