#ifndef __VOICE_PARSER_H__
#define __VOICE_PARSER_H__

#include <stdint.h>
#include <pthread.h>
#include "atomic.h"
#include "counter.h"
#include "fax_queue.h"

#define FILE_DIR            "/data"
#define FILE_MAX_NUM        2048
#define VOICE_BUF_SIZE      1024
//#define MAX_BUF_NUM         256
#define MAX_BUF_NUM         8
#define MAX_FILENAME_SIZE   256

#define SR155_NUM       64
#define E1_NUM          64
#define TS_NUM          32

typedef struct voice_file_info_ {
    uint64_t callid;
    char filename_up[MAX_FILENAME_SIZE];
    char filename_down[MAX_FILENAME_SIZE];
    uint8_t data_up[VOICE_BUF_SIZE * MAX_BUF_NUM];
    uint8_t data_down[VOICE_BUF_SIZE * MAX_BUF_NUM];
    uint32_t data_count_up;
    uint32_t data_count_down;
    FILE *fp_up;
    FILE *fp_down;
    uint8_t case_mark;
    uint8_t data_mark;
    uint8_t up_no;
    uint8_t down_no; 
    uint32_t up_lost;
    uint32_t down_lost; 
    int tx_fd;
    STRUCT_CDR_PATH *cdr_node;
    STRUCT_CDR cdr;
    uint32_t fax_data_len_up;
    uint32_t fax_data_len_down;
    uint32_t dir_no;
    time_t update_time;
    uint8_t up_end_flag;
    uint8_t down_end_flag;
    pthread_mutex_t mutex;
} voice_file_info_t;



extern voice_file_info_t voice_file[SR155_NUM][E1_NUM][TS_NUM];


int init_voice_file(void);
inline int voice_parser(uint8_t *pkt, uint32_t pkt_len);
voice_file_info_t *get_file_struct(uint64_t callid);
int cleanup_file_struct(voice_file_info_t *file);

#endif
