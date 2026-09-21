#ifndef NEXORA_BENCH_COMMON_H
#define NEXORA_BENCH_COMMON_H

#include <stddef.h>
#include <stdint.h>

uint64_t bench_now_ns(void);
uint64_t bench_parse_u64(const char *s, const char *name);
const char *bench_arg_value(int argc, char **argv, const char *name, const char *fallback);
int bench_has_flag(int argc, char **argv, const char *name);
uint64_t bench_xorshift64(uint64_t *state);
double bench_mean(const uint64_t *values, size_t n);
uint64_t bench_percentile_u64(uint64_t *values, size_t n, double q);
void bench_print_metrics_header(const char *benchmark, const char *variant);

#endif
