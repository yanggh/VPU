#include "vpu.h"
#include "atomic.h"
#include "counter.h"


atomic64_t all_pkts;
atomic64_t lost_pkts;
atomic64_t voice_pkts;
atomic64_t other_pkts;

atomic64_t call_start;
atomic64_t call_end;
atomic64_t call_online;
atomic64_t call_timeout;

atomic64_t file_ok;
atomic64_t file_fail;

atomic64_t fax_lost;
atomic64_t fax_enqueue;
atomic64_t fax_dequeue;
atomic64_t fax_send;
atomic64_t fax_ack;
atomic64_t fax_yes;
atomic64_t fax_no;

void init_atomic_counter(void)
{
    atomic64_init(&all_pkts);
    atomic64_init(&lost_pkts);
    atomic64_init(&voice_pkts);
    atomic64_init(&other_pkts);
    atomic64_init(&call_start);
    atomic64_init(&call_end);
    atomic64_init(&call_online);
    atomic64_init(&call_timeout);
    atomic64_init(&file_ok);
    atomic64_init(&file_fail);
    atomic64_init(&fax_lost);
    atomic64_init(&fax_enqueue);
    atomic64_init(&fax_dequeue);
    atomic64_init(&fax_yes);
    atomic64_init(&fax_no);
}


