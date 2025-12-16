# ebpf-example-env

ebpf 测试验证环境

## 项目目标
提供 eBPF 基础测试环境，用于快速搭建和验证 eBPF 程序功能。目标是让你在最短时间内拥有可用的 Linux eBPF 运行环境、常用工具链，以及若干示例与脚本，便于学习与实验。

## 功能特点
- 快速启动：基于容器或虚拟机在 macOS/Linux 主机上快速获得可用的 eBPF 环境
- 常用工具链：clang/llvm、bpftool、iproute2、libelf、gcc/make 等（bcc、libbpf 可按需扩展）
- 示例与脚本：提供基础示例（XDP/TC/kprobe/uprobe/tracepoint）的构建与运行命令（后续可补充脚本）
- 可扩展：建议目录结构便于逐步扩展场景和复用脚本
- 低门槛：一键安装常用工具，提供验证与排错的常用命令

## 环境要求
- 主机系统：macOS 或 Linux（建议较新的 Linux 发行版，内核支持 BPF）
- 推荐方式：
  - 使用 Docker（macOS 可用 Docker Desktop/Colima）在 Linux 容器内运行 eBPF 程序
  - 或使用虚拟机（Multipass/UTM/VirtualBox 等）启动 Linux（Ubuntu 22.04+）
- 权限与能力：需 root 或相应 CAP（容器建议使用 `--privileged`）
- 基础依赖：git、docker（如用容器方案）

## 快速开始（Docker 示例）
1. 启动 Ubuntu 容器（将当前目录挂载为工作区）
   ```bash
   docker run --privileged --rm -it \
     -v "$PWD":/workspace -w /workspace \
     --network host ubuntu:22.04
   ```

2. 在容器中安装工具链
   ```bash
   apt update
   apt install -y clang llvm make gcc libc6-dev libelf-dev pkg-config iproute2 bpftool git
   ```

3. 验证 bpftool 是否可用
   ```bash
   bpftool version
   ```

## 示例：最小 XDP 程序的编译与加载
以下示例演示最小工作流（假设在容器中，并创建了 `xdp_pass.c` 文件）：

- 代码（`xdp_pass.c`）：
  ```c
  #define KBUILD_MODNAME "xdp_pass"
  #include <linux/bpf.h>
  #include <bpf/bpf_helpers.h>

  SEC("xdp")
  int xdp_pass(struct xdp_md *ctx) {
      return XDP_PASS;
  }

  char LICENSE[] SEC("license") = "GPL";
  ```

- 编译为 BPF 对象：
  ```bash
  clang -target bpf -O2 -c xdp_pass.c -o xdp_pass.o
  ```

- 加载到网卡（示例以 eth0 为例，实际名称可能不同）：
  ```bash
  ip link set dev eth0 xdp obj xdp_pass.o sec xdp
  ```

- 查看已加载的 BPF 程序：
  ```bash
  bpftool prog
  ```

- 卸载 XDP 程序：
  ```bash
  ip link set dev eth0 xdp off
  ```

提示：在 Docker for macOS 场景中，直接对宿主网卡加载 XDP 通常不可行，建议在 Linux VM/容器网络中进行 XDP/TC 实验。

## 常用排查命令
- 查看所有 BPF 程序/映射：
  ```bash
  bpftool prog show
  bpftool map show
  ```
- 查看内核 BPF 功能：
  ```bash
  bpftool feature probe kernel
  ```
- 查看网卡 XDP 状态：
  ```bash
  ip -details link show eth0
  ```

## 建议的目录结构
为便于扩展与维护，建议采用如下结构（可根据需要调整）：
```
.
├── examples/              # 示例程序（XDP/TC/kprobe/uprobe/tracepoint 等）
│   ├── xdp/
│   ├── tc/
│   ├── kprobe/
│   ├── uprobe/
│   └── tracepoint/
├── scripts/               # 环境与构建脚本
│   ├── setup.sh           # 安装工具链、准备依赖
│   ├── build.sh           # 统一编译示例
│   ├── run.sh             # 统一运行/加载示例
│   └── clean.sh           # 清理/卸载程序
├── docs/                  # 教程/说明文档
└── README.md
```

## 后续计划
- 补充完整的示例代码与对应的构建/运行脚本
- 增加 libbpf（CO-RE）与 bcc 两种方式的示例，覆盖 XDP、TC、kprobe、tracepoint 等典型场景
- 提供常见错误的排查指南（权限、内核功能、网络设备命名、容器网络限制等）

## 免责声明
- eBPF 涉及内核功能与特权操作，务必在可控环境中进行测试
- 在生产环境使用前应充分评估风险与兼容性

