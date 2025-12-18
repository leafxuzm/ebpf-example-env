# 目录脚本使用文档

## 概览
- 目录：`/Users/xu/Work/Leafxu/ebpf-example-env/example/script`
- 当前脚本：
  - `copy-to-container.sh`：将宿主机文件或目录拷贝到指定容器目录；若容器目录不存在则自动创建

## 环境要求
- 已安装并可用的 `docker`
- 目标容器处于运行或可执行 `docker exec` 的状态

## copy-to-container.sh
### 用途
- 复制宿主机上的文件或目录到容器中的指定目录
- 如果容器目录不存在，先在容器中创建，再完成复制

### 用法
```
./copy-to-container.sh <src_path> <container_id> [dest_path=/workspace/projects/]
```

### 参数说明
- `src_path`：宿主机上的源路径（文件或目录）
- `container_id`：目标容器 ID 或名称（可用 `docker ps` 查看）
- `dest_path`：容器内目标目录，默认 `"/workspace/projects/"`；可自定义子目录

### 示例
- 复制目录到默认路径：
```
./copy-to-container.sh ./example/tracepoint ebpf-centos9-dev
```
- 复制文件到自定义路径：
```
./copy-to-container.sh ./README.md ebpf-centos9-dev /workspace/projects/trace/
```

### 输出
- 成功时输出：
```
Copied <src> to <container>:<dest><basename>
```

### 常见问题
- `docker not found`：宿主机未安装或未配置 `docker`
- `Container not found: <id>`：容器 ID/名称不正确或容器不可达
- `Source not found: <path>`：源路径不存在或不可访问

### 权限
- 首次使用时请确保脚本有执行权限：
```
chmod +x ./copy-to-container.sh
```
