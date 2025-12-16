#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <pthread.h> // 引入多线程库
#include <bpf/libbpf.h>
#include "syscall.skel.h"

// ==========================================
// 1. 数据结构与全局变量
// ==========================================

// 事件结构体 (与 BPF 程序保持一致)
struct event {
    int pid;
    char comm[16];
    char filename[128];
};

static volatile bool exiting = false;

// 信号处理
static void sig_handler(int sig)
{
    exiting = true;
}

// ==========================================
// 2. 线程安全队列 (Thread-Safe Queue) 实现
// ==========================================

// 队列节点
typedef struct QueueNode {
    struct event *data;
    struct QueueNode *next;
} QueueNode;

// 队列管理器
typedef struct {
    QueueNode *head;
    QueueNode *tail;
    pthread_mutex_t mutex; // 互斥锁
    pthread_cond_t cond;   // 条件变量 (用于通知消费者)
    int size;
} SafeQueue;

SafeQueue *queue_init() {
    SafeQueue *q = malloc(sizeof(SafeQueue));
    q->head = q->tail = NULL;
    q->size = 0;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
    return q;
}

// 入队 (Producer 调用)
void queue_push(SafeQueue *q, struct event *data) {
    QueueNode *node = malloc(sizeof(QueueNode));
    node->data = data;
    node->next = NULL;

    pthread_mutex_lock(&q->mutex);
    
    if (q->tail == NULL) {
        q->head = node;
        q->tail = node;
    } else {
        q->tail->next = node;
        q->tail = node;
    }
    q->size++;
    
    // 唤醒等待的消费者
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mutex);
}

// 出队 (Consumer 调用)
// 如果队列为空，wait_ms > 0 时会阻塞等待，或者直到 exiting 为 true
struct event *queue_pop(SafeQueue *q) {
    struct event *data = NULL;

    pthread_mutex_lock(&q->mutex);

    // 如果队列为空，且程序未退出，则等待信号
    while (q->head == NULL && !exiting) {
        pthread_cond_wait(&q->cond, &q->mutex);
    }

    // 再次检查队列是否为空 (因为可能是因为 exiting 退出的等待)
    if (q->head != NULL) {
        QueueNode *tmp = q->head;
        data = tmp->data;
        q->head = tmp->next;
        if (q->head == NULL) {
            q->tail = NULL;
        }
        free(tmp); // 释放节点内存
        q->size--;
    }

    pthread_mutex_unlock(&q->mutex);
    return data;
}

void queue_free(SafeQueue *q) {
    // 实际生产中这里应该清理剩余节点，防止内存泄露
    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
    free(q);
}

// 全局队列实例
SafeQueue *g_queue = NULL;

// ==========================================
// 3. 工作线程 (消费者 Consumer)
// ==========================================

void *worker_thread_func(void *arg) {
    printf("[Worker] Thread started. Waiting for events...\n");
    
    while (true) {
        // 从队列取数据 (如果队列空会阻塞)
        struct event *e = queue_pop(g_queue);

        // 如果取到了数据，处理它
        if (e) {
            // --- 这里是耗时操作 (打印、写文件、发邮件) ---
            printf("[Worker] Process PID: %d, Comm: %s, Exec: %s\n", 
                   e->pid, e->comm, e->filename);
            
            // 处理完必须释放内存！因为是 handle_event 里 malloc 的
            free(e); 
        }

        // 如果收到退出信号 且 队列已经空了，就跳出循环
        if (exiting && e == NULL) {
            break;
        }
    }
    
    printf("[Worker] Thread exiting...\n");
    return NULL;
}

// ==========================================
// 4. BPF 回调 (生产者 Producer)
// ==========================================

// 注意：这个函数必须极快！不能做耗时 IO！
static int handle_event(void *ctx, void *data, size_t data_sz)
{
    const struct event *raw_e = data;

    // 1. 分配堆内存 (Heap)
    // 必须拷贝一份，因为 data 指针指向的 Ring Buffer 内存稍后会被覆盖
    struct event *e_copy = malloc(sizeof(struct event));
    if (!e_copy) {
        fprintf(stderr, "malloc failed\n");
        return 0; // 丢弃事件
    }

    // 2. 拷贝数据
    memcpy(e_copy, raw_e, sizeof(struct event));

    // 3. 推入队列
    queue_push(g_queue, e_copy);

    return 0;
}

// ==========================================
// 5. 主函数
// ==========================================

int main(int argc, char **argv)
{
    struct syscall_bpf *obj;
    struct ring_buffer *rb = NULL;
    int err;
    pthread_t worker_tid; // 线程 ID

    // 初始化队列
    g_queue = queue_init();

    // 启动工作线程
    if (pthread_create(&worker_tid, NULL, worker_thread_func, NULL) != 0) {
        fprintf(stderr, "Failed to create worker thread\n");
        return 1;
    }

    struct rlimit rlim = { RLIM_INFINITY, RLIM_INFINITY };
    setrlimit(RLIMIT_MEMLOCK, &rlim);

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    obj = syscall_bpf__open();
    if (!obj) {
        fprintf(stderr, "failed to open BPF object\n");
        goto cleanup;
    }

    err = syscall_bpf__load(obj);
    if (err) {
        fprintf(stderr, "failed to load BPF object: %d\n", err);
        goto cleanup;
    }

    err = syscall_bpf__attach(obj);
    if (err) {
        fprintf(stderr, "failed to attach BPF programs\n");
        goto cleanup;
    }

    rb = ring_buffer__new(bpf_map__fd(obj->maps.rb), handle_event, NULL, NULL);
    if (!rb) {
        fprintf(stderr, "failed to create ring buffer\n");
        goto cleanup;
    }

    printf("Successfully started! Hit Ctrl+C to stop.\n");

    // 主循环：只负责从 Ring Buffer 搬运数据到 Queue
    while (!exiting) {

		// 内核态 (Ring Buffer) 中的数据，搬运到 用户态 并触发你写好的回调函数 (handle_event). ring_buffer__poll调用了epoll.
        err = ring_buffer__poll(rb, 100);
        if (err == -EINTR) {
            err = 0;
            break;
        }
        if (err < 0) {
            fprintf(stderr, "Error polling ring buffer: %d\n", err);
            break;
        }
    }

cleanup:
    printf("Stopping...\n");
    exiting = true; 
    
    // 关键：通知工作线程 wake up，这样它才能检测到 exiting 标志并处理完剩余数据后退出
    pthread_mutex_lock(&g_queue->mutex);
    pthread_cond_signal(&g_queue->cond);
    pthread_mutex_unlock(&g_queue->mutex);

    // 等待工作线程安全退出
    pthread_join(worker_tid, NULL);

    // 清理资源
    if (rb) ring_buffer__free(rb);
    if (obj) syscall_bpf__destroy(obj);
    queue_free(g_queue);
    
    return err < 0 ? -err : 0;
}
