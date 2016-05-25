#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include "vpu.h"
#include "atomic.h"
#include "pkt_fifo.h"
#include "vpu_worker.h"
#include "conf.h"
#include "conn_serv.h"
#include "file_server.h"
#include "cycle_operation.h"
#include "daemon.h"
#include "fax_queue.h"
#include "fax_decode.h"
#include "voice_parser.h"
#include "counter.h"
#include "send_cdr.h"

static int daemon_flag = 1;
uint8_t svm_signal_flags = 0;

static void signal_handler_sigint(__attribute__((unused))int sig)
{
    //applog(APP_LOG_LEVEL_NOTICE, APP_VPU_LOG_MASK_BASE, "receive a SIGINT");
    svm_signal_flags |= SVM_STOP;
}



static void signal_catch(int sig, void (*handler)(int))
{
    struct sigaction action;
    memset(&action, 0x00, sizeof(struct sigaction));

    action.sa_handler = handler;
    sigemptyset(&(action.sa_mask));
    sigaddset(&(action.sa_mask),sig);
    action.sa_flags = 0;
    sigaction(sig, &action, 0);

    return;
}








int main(int argc, char *argv[])
{
    pthread_t tid_io;
    pthread_t tid_worker[WORKER_NUM];
    pthread_t tid_sguard;
    pthread_t tid_ma;
    pthread_t tid_cdr;
    pthread_t tid_server;
    pthread_t tid_cycle;
    pthread_t tid_faxpre[WORKER_NUM];
    int i;

    open_applog("vpu", LOG_NDELAY, DEF_APP_SYSLOG_FACILITY);
    applog_set_debug_mask(0);

    if (daemon_flag == 1) {
        daemonize();
    }

    signal(SIGPIPE, SIG_IGN);
    signal_catch(SIGINT, signal_handler_sigint);

    param_parser(argc, argv);
    get_vpu_conf();
    get_svm_conf();
    get_dms_conf();

    init_atomic_counter();
    init_pkt_fifo();
    INIT_QUEUE(&(queue.eq), &(queue.fq), 1024);
    init_voice_file();
    init_connect();

    if (pthread_create(&tid_sguard, NULL, (void *)conn_sguard, NULL) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "thread conn_sguard create failed");
    }
    if (pthread_create(&tid_ma, NULL, (void *)conn_ma, NULL) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "thread conn_ma create failed");
    }

    if (pthread_create(&tid_cdr, NULL, (void *)conn_cdr_thread, NULL) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "thread conn_cdr_thread create failed");
    }
    for (i = 0; i < WORKER_NUM; i++) {
        if (pthread_create(&tid_worker[i], NULL, &vpu_worker_thread, (void *)(uint64_t)i) != 0) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "%dth worker thread create failed", i);
        }
    }
    if (pthread_create(&tid_io, NULL, &vpu_io_thread, NULL) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "vpu io thread create failed");
    }
    if (pthread_create(&tid_server, NULL, (void *)file_server_thread, NULL) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "file server thread create failed");
    }
    if (pthread_create(&tid_cycle, NULL, (void *)cycle_thread, NULL) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "cycle thread create failed");
    }
    for(i = 0; i < 4; i++) {
        queue.flag = i;
        if (pthread_create(&tid_faxpre[i], NULL, (void *)fax_pre, &queue) != 0) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "fax_pre thread create failed");
        }
        usleep(5000);
    }

    pthread_join(tid_sguard, NULL);
    pthread_join(tid_ma, NULL);
    pthread_join(tid_io, NULL);
    pthread_join(tid_server, NULL);
    pthread_join(tid_cycle, NULL);
    for (i = 0; i < WORKER_NUM; i++) {
        pthread_join(tid_worker[i], NULL);
    }
    for (i = 0; i < 4; i++) {
        pthread_join(tid_faxpre[i], NULL);
    }

    applog(APP_LOG_LEVEL_NOTICE, APP_VPU_LOG_MASK_BASE, "VPU DONE!");
    close_applog();

    return 0;
}
