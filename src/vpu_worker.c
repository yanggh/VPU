#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sched.h>
#include <pcap/pcap.h>
#include "vpu.h"
#include "pkt_fifo.h"
#include "voice_parser.h"
#include "conf.h"
#include "counter.h"
#include "vpu_worker.h"



void *vpu_io_thread(void *arg)
{
    pcap_t *pcap_dev;
    const u_char *pkt;
    struct pcap_pkthdr pkt_hdr;
    pkt_node_t node;
    uint64_t callid;
    uint32_t worker;
    pthread_t tid;
    cpu_set_t mask;
    char *dev = NULL;
    int num;
    char errbuf[1024];
    uint32_t pos;
    uint8_t sr155_no;
    uint8_t e1_no;
    uint8_t ts_no;

    num = sysconf(_SC_NPROCESSORS_CONF);
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_IO, "system has %d processor(s)", num);
    tid = pthread_self();
    
    CPU_ZERO(&mask);
    CPU_SET(0, &mask);
    pthread_setaffinity_np(tid, sizeof(mask), &mask);

    dev = get_dev();

    pcap_dev = pcap_open_live(dev, MAX_ETH_PACKET_LEN, PCAP_ETH_PROMISC, PCAP_TIMEOUT, errbuf);
    if (pcap_dev == NULL) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_IO, "%s", errbuf);
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_IO, "open dev %s failed\n", dev);
		exit(-1);
    }
    
    while (1) {
        if (unlikely(svm_signal_flags & SVM_DONE)) {
            break;
        }

        if (unlikely(vpu_dev_reload == 1)) {
            pcap_close(pcap_dev);
            
            dev = get_dev();
            pcap_dev = pcap_open_live(dev, MAX_ETH_PACKET_LEN, PCAP_ETH_PROMISC, PCAP_TIMEOUT, errbuf);
            if (pcap_dev == NULL) {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_IO, "%s", errbuf);
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_IO, "open dev %s failed\n", dev);
                exit(-1);
            }

            vpu_dev_reload = 0;
        }
        pkt = pcap_next(pcap_dev, &pkt_hdr);
        if (pkt == NULL)
            continue;

        atomic64_add(&all_pkts, 1);
        callid = *(uint64_t *)(pkt + 20);
        
        pos = (callid >> 15) & 0x1ffff;
        sr155_no = pos >> 11;
        e1_no = (pos >> 5) & 0x3f;
        ts_no = pos & 0x1f;

        worker = ts_no % WORKER_NUM;

        node.pkt = (uint8_t *)pkt;
        node.pkt_len = pkt_hdr.len;
        if (0 != pkt_fifo_put(&pkt_fifos[worker], &node)) {
            atomic64_add(&lost_pkts, 1);
        }
    }

    pcap_close(pcap_dev);
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_IO, "pthread vpu_io_thread exit");

    pthread_exit(NULL);
}




void *vpu_worker_thread(void *arg)
{
    uint32_t worker_id;
    pkt_fifo_t *fifo;
    pkt_node_t node;
    struct timespec req;

    worker_id = (uint64_t)arg;
    fifo = &pkt_fifos[worker_id];

    req.tv_sec = 0;
    req.tv_nsec = 100;
    while (1) {
        if (unlikely(svm_signal_flags & SVM_DONE)) {
            break;
        }

        if (unlikely(vpu_dir_reload == 1)) {
            if (worker_id == 0) {
                memcpy(&file_dirs, &r_file_dirs, sizeof(file_dirs_info_t));
                vpu_dir_reload = 0;
            } else {
                nanosleep(&req, NULL);
                continue;
            }
        }

        if (0 != pkt_fifo_get(fifo, &node)) {
            nanosleep(&req, NULL);
            continue;
        }
        if (unlikely(node.pkt_len < 36) || node.pkt[12] != 0x80 || 
                node.pkt[13] != 0x52 || node.pkt[16] != 0x03) {
            atomic64_add(&other_pkts, 1);
        } else {
            atomic64_add(&voice_pkts, 1);
            voice_parser(node.pkt, node.pkt_len);
        }
    }

    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_WORKER, "pthread vpu_worker_thread %u exit", worker_id);

    pthread_exit(NULL);
}


