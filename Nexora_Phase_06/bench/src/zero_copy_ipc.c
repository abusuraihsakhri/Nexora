#include "common.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static ssize_t write_all(int fd, const void *buf, size_t n) {
    const unsigned char *p = buf;
    size_t done = 0;
    while (done < n) {
        ssize_t r = write(fd, p + done, n - done);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        done += (size_t)r;
    }
    return (ssize_t)done;
}

static ssize_t read_all(int fd, void *buf, size_t n) {
    unsigned char *p = buf;
    size_t done = 0;
    while (done < n) {
        ssize_t r = read(fd, p + done, n - done);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        done += (size_t)r;
    }
    return (ssize_t)done;
}

static int create_memfd(void) {
#ifdef SYS_memfd_create
    return (int)syscall(SYS_memfd_create, "nexora-ipc", 0);
#else
    errno = ENOSYS;
    return -1;
#endif
}

int main(int argc, char **argv) {
    const char *variant = bench_arg_value(argc, argv, "--variant", "shared_mem");
    const uint64_t iterations = bench_parse_u64(bench_arg_value(argc, argv, "--iterations", "1000"), "iterations");
    const uint64_t warmup = bench_parse_u64(bench_arg_value(argc, argv, "--warmup", "100"), "warmup");
    const uint64_t size = bench_parse_u64(bench_arg_value(argc, argv, "--size", "262144"), "size");
    if (iterations == 0 || size == 0 || size > SIZE_MAX ||
        (strcmp(variant, "socket_copy") != 0 && strcmp(variant, "shared_mem") != 0)) {
        fprintf(stderr, "invalid arguments\n");
        return 2;
    }

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { perror("socketpair"); return 2; }
    unsigned char *payload = NULL;
    int memfd = -1;
    if (strcmp(variant, "shared_mem") == 0) {
        memfd = create_memfd();
        if (memfd < 0) { perror("memfd_create"); return 2; }
        if (ftruncate(memfd, (off_t)size) != 0) { perror("ftruncate"); return 2; }
        payload = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, memfd, 0);
        if (payload == MAP_FAILED) { perror("mmap"); return 2; }
    } else {
        payload = malloc((size_t)size);
        if (!payload) { perror("malloc"); return 2; }
    }
    memset(payload, 0x5a, (size_t)size);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 2; }
    if (pid == 0) {
        close(sv[0]);
        unsigned char *recvbuf = NULL;
        if (strcmp(variant, "socket_copy") == 0) {
            recvbuf = malloc((size_t)size);
            if (!recvbuf) _exit(3);
        }
        unsigned char token = 0;
        volatile unsigned char sink = 0;
        const uint64_t total = warmup + iterations;
        for (uint64_t i = 0; i < total; ++i) {
            if (strcmp(variant, "socket_copy") == 0) {
                if (read_all(sv[1], recvbuf, (size_t)size) < 0) _exit(4);
                sink ^= recvbuf[i % size];
            } else {
                if (read_all(sv[1], &token, 1) < 0) _exit(4);
                sink ^= payload[i % size];
            }
            if (write_all(sv[1], &token, 1) < 0) _exit(5);
        }
        free(recvbuf);
        close(sv[1]);
        (void)sink;
        _exit(0);
    }

    close(sv[1]);
    uint64_t *samples = calloc((size_t)iterations, sizeof(*samples));
    if (!samples) { perror("calloc"); kill(pid, SIGKILL); return 2; }
    unsigned char token = 1, ack = 0;
    const uint64_t total = warmup + iterations;
    for (uint64_t i = 0; i < total; ++i) {
        payload[i % size] ^= (unsigned char)i;
        uint64_t t0 = bench_now_ns();
        if (strcmp(variant, "socket_copy") == 0) {
            if (write_all(sv[0], payload, (size_t)size) < 0) { perror("write"); return 2; }
        } else {
            if (write_all(sv[0], &token, 1) < 0) { perror("write"); return 2; }
        }
        if (read_all(sv[0], &ack, 1) < 0) { perror("read"); return 2; }
        uint64_t t1 = bench_now_ns();
        if (i >= warmup) samples[i - warmup] = t1 - t0;
    }
    close(sv[0]);
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) { perror("waitpid"); return 2; }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "child failed (status=%d)\n", status);
        return 2;
    }

    double mean = bench_mean(samples, (size_t)iterations);
    uint64_t p50 = bench_percentile_u64(samples, (size_t)iterations, 0.50);
    uint64_t p95 = bench_percentile_u64(samples, (size_t)iterations, 0.95);
    uint64_t p99 = bench_percentile_u64(samples, (size_t)iterations, 0.99);
    uint64_t app_copy = strcmp(variant, "socket_copy") == 0 ? size : 0;

    bench_print_metrics_header("zero_copy_ipc", variant);
    printf("\"params\":{\"iterations\":%" PRIu64 ",\"warmup\":%" PRIu64 ",\"payload_bytes\":%" PRIu64 "},", iterations, warmup, size);
    printf("\"metrics\":{\"mean_handoff_ns\":%.3f,\"median_handoff_ns\":%" PRIu64 ",\"p95_handoff_ns\":%" PRIu64 ",\"p99_handoff_ns\":%" PRIu64 ",\"application_payload_copy_bytes_per_handoff\":%" PRIu64 "},", mean, p50, p95, p99, app_copy);
    printf("\"checksum\":%u}\n", (unsigned)(ack ^ payload[0]));

    free(samples);
    if (strcmp(variant, "shared_mem") == 0) {
        munmap(payload, (size_t)size);
        close(memfd);
    } else {
        free(payload);
    }
    return 0;
}
