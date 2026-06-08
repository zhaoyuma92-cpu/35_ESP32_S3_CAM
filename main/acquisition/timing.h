#ifndef TIMING_H
#define TIMING_H

#include <stdint.h>

/* Lightweight min/avg/max accumulator for microsecond measurements. */
typedef struct {
    uint64_t sum;
    uint32_t min;
    uint32_t max;
    uint32_t count;
} timing_stat_t;

static inline void timing_add(timing_stat_t *s, uint32_t value)
{
    if (s->count == 0 || value < s->min) s->min = value;
    if (value > s->max)                  s->max = value;
    s->sum += value;
    s->count++;
}

static inline uint32_t timing_min(const timing_stat_t *s)
{
    return s->count > 0 ? s->min : 0;
}

static inline uint32_t timing_avg(const timing_stat_t *s)
{
    return s->count > 0 ? (uint32_t)(s->sum / s->count) : 0;
}

#endif /* TIMING_H */
