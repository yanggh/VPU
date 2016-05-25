#ifndef __COUNTER_H__
#define __COUNTER_H__

#include "atomic.h"

typedef struct worker_count_ {
    uint64_t voice_pkts;
    uint64_t other_pkts;
    uint64_t call_start;
    uint64_t call_end;
    uint64_t call_timeout;
    uint64_t file_ok;
    uint64_t file_fail;
    uint64_t fax_lost;
    uint64_t fax_enqueue;
} worker_count_t;


typedef struct all_count_ {
    uint64_t pkts;
    uint64_t lost_pkts;
    uint64_t voice_pkts;
    uint64_t other_pkts;
    uint64_t call_start;
    uint64_t call_end;
    uint64_t call_timeout;
    uint64_t file_ok;
    uint64_t file_fail;
    uint64_t fax_lost;
    uint64_t fax_enqueue;
    uint64_t fax_dequeue;
    uint64_t fax_yes;
    uint64_t fax_no;
} all_count_t;


extern atomic64_t all_pkts;
extern atomic64_t lost_pkts;
extern atomic64_t voice_pkts;
extern atomic64_t other_pkts;

extern atomic64_t call_start;
extern atomic64_t call_end;
extern atomic64_t call_online;
extern atomic64_t call_timeout;

extern atomic64_t file_ok;
extern atomic64_t file_fail;

extern atomic64_t fax_lost;
extern atomic64_t fax_enqueue;
extern atomic64_t fax_dequeue;
extern atomic64_t fax_yes;
extern atomic64_t fax_no;




inline worker_count_t *get_counter_buf(uint32_t num);
inline uint16_t get_counter_flag(uint32_t num);
inline void enable_counter(uint32_t num);
inline void disable_counter(uint32_t num);
void init_atomic_counter(void);

#endif
