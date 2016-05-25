#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <endian.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <errno.h>
#include "vpu.h"
#include "conf.h"
#include "voice_parser.h"
#include "fax_queue.h"
#include "atomic.h"
#include "counter.h"
#include "fax_decode.h"
#include "send_cdr.h"


voice_file_info_t voice_file[SR155_NUM][E1_NUM][TS_NUM];

int init_voice_file(void)
{
    int i, j, k;

    memset(voice_file, 0, sizeof(voice_file));
    for (i = 0; i < SR155_NUM; i++) {
        for (j = 0; j < E1_NUM; j++) {
            for (k = 0; k < TS_NUM; k++) {
                pthread_mutex_init(&(voice_file[i][j][k].mutex), NULL);
            }
        }
    }
    return 0;
}


char *get_root_dir(voice_file_info_t *file)
{
    char *root_dir = NULL;
    uint32_t dir_no;
    uint32_t dir_no_cur;

    if (file == NULL) {
        return NULL;
    }

    dir_no = atomic32_add(&file_dir_no, 1);

    if (dir_no < 0 || dir_no >= file_dirs.num) {
        atomic32_set(&file_dir_no, 0);
        dir_no = atomic32_add(&file_dir_no, 1);
    }

    if(file_dirs.dirs[dir_no].write_flag == 1) {
        file->cdr.path = file_dirs.dirs[dir_no].disk_num;
        root_dir = file_dirs.dirs[dir_no].dir_name;
    } else {
        dir_no_cur = dir_no;
        while (1) {
            dir_no += 1;
            if (dir_no < 0 || dir_no >= file_dirs.num) {
                dir_no = 0;
            }
            if (dir_no == dir_no_cur) {
                break;
            }

            if(file_dirs.dirs[dir_no].write_flag == 1) {
                file->cdr.path = file_dirs.dirs[dir_no].disk_num;
                root_dir = file_dirs.dirs[dir_no].dir_name;
                break;
            }
        }
    }

    return root_dir;
}

inline int get_filename(voice_file_info_t *file)
{
    time_t time;
    struct tm *tms;
    struct tm tms_buf;
    uint32_t pos;
    uint8_t e1_no;
    uint8_t ts_no;
    char *root_dir;
    int i;
    char filename[256] = {0};
    struct stat file_stat;

    if (file == NULL || file->callid == 0) {
        return -1;
    }

    time = (file->callid >> 32) & 0xffffffff;
    pos = (file->callid >> 15) & 0x1ffff;
    e1_no = (pos >> 5) & 0x3f;
    ts_no = pos & 0x1f;

    tms = localtime_r(&time, &tms_buf);
    if (tms == NULL) {
        return -1;
    }

    root_dir = NULL;
    if (file->data_mark != 0x01) {
        for (i = 0; i < file_dirs.num; i++) {
            if (file->case_mark == 0) {
                snprintf(filename, 255, "%s/%s/%04d-%02d-%02d/%02d/%02d/%016llx_up", 
                        file_dirs.dirs[i].dir_name, NORMAL_DIR, 
                        tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday, 
                        e1_no, ts_no, (long long unsigned int)file->callid);
            } else {
                snprintf(filename, 255, "%s/%s/%04d-%02d-%02d/%02d/%02d/%016llx_up", 
                        file_dirs.dirs[i].dir_name, CASE_DIR, 
                        tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday, 
                        e1_no, ts_no, (long long unsigned int)file->callid);
            }

            if (stat(filename, &file_stat) == 0 && (file_stat.st_mode & S_IFMT) == S_IFREG) {
                file->cdr.path = file_dirs.dirs[i].disk_num;
                root_dir = file_dirs.dirs[i].dir_name;
                break;
            }
        }
    }
    if (root_dir == NULL) {
        root_dir = get_root_dir(file);
        if (root_dir == NULL) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "get root dir failed");
            return -2;
        }
    }

    if (file->case_mark == 0) {
        snprintf(file->filename_up, 255, "%s/%s/%04d-%02d-%02d/%02d/%02d/%016llx_up", 
                root_dir, NORMAL_DIR, tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday, 
                e1_no, ts_no, (long long unsigned int)file->callid);
        snprintf(file->filename_down, 255, "%s/%s/%04d-%02d-%02d/%02d/%02d/%016llx_down", 
                root_dir, NORMAL_DIR, tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday, 
                e1_no, ts_no, (long long unsigned int)file->callid);
    } else {
        snprintf(file->filename_up, 255, "%s/%s/%04d-%02d-%02d/%02d/%02d/%016llx_up", 
                root_dir, CASE_DIR, tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday, 
                e1_no, ts_no, (long long unsigned int)file->callid);
        snprintf(file->filename_down, 255, "%s/%s/%04d-%02d-%02d/%02d/%02d/%016llx_down", 
                root_dir, CASE_DIR, tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday, 
                e1_no, ts_no, (long long unsigned int)file->callid);
    }

    return 0;
}





int open_voice_file(voice_file_info_t *file)
{
    int ret = 0;

    if (file == NULL || file->fp_up != NULL || file->fp_down != NULL) {
        return -1;
    }

    ret = get_filename(file);
    if (ret != 0) {
        return -1;
    }
    applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "get filename: %s  %s\n", file->filename_up, file->filename_down);

    file->fp_up = fopen(file->filename_up, "a+");
    if (file->fp_up == NULL) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "failed to open %s, errno: %d, %s", file->filename_up, errno, strerror(errno));
        ret = -1;
    }
    file->fp_down = fopen(file->filename_down, "a+");
    if (file->fp_down == NULL) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "failed to open %s, errno: %d, %s", file->filename_down, errno, strerror(errno));
        ret = -1;
    }

    return ret;
}


int write_voice_file(voice_file_info_t *file)
{
    int len;
    int data_len;
    int file_no;

    if (file == NULL) {
        return -1;
    }

    data_len = file->data_count_up > file->data_count_down ? file->data_count_up : file->data_count_down;
    data_len = data_len > VOICE_BUF_SIZE * MAX_BUF_NUM ? VOICE_BUF_SIZE * MAX_BUF_NUM : data_len;

    if (file->fp_up != NULL) {
        file_no = fileno(file->fp_up);
        if (file_no == -1) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_WORKER, "fwrite fp up: %p fileno is -1, %s", 
                    file->fp_up, file->filename_up);
        }
        len = fwrite(file->data_up, 1, data_len, file->fp_up);
        if (len != data_len) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "fwrite up file %u bytes but return %d", 
                    VOICE_BUF_SIZE * MAX_BUF_NUM, len);
        }
    }

    if (file->fp_down != NULL) {
        file_no = fileno(file->fp_down);
        if (file_no == -1) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_WORKER, "fwrite fp down: %p fileno is -1, %s", 
                    file->fp_down, file->filename_down);
        }
        len = fwrite(file->data_down, 1, data_len, file->fp_down);
        if (len != data_len) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "fwrite down file %u bytes but return %d", 
                    VOICE_BUF_SIZE * MAX_BUF_NUM, len);
        }
    }

    return 0;
}


int close_voice_file(voice_file_info_t *file)
{
    int file_no;
    if (file == NULL) {
        return -1;
    }

    if (file->fp_up != NULL) {
        file_no = fileno(file->fp_up);
        if (file_no == -1) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_WORKER, 
                    "fclose fp up: %p fileno is -1", file->fp_up);
        }
        fclose(file->fp_up);
        file->fp_up = NULL;
    } else {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, 
                "close fp up: %p, filename: %s", file->fp_up, file->filename_up);
    }

    if (file->fp_down != NULL) {
        file_no = fileno(file->fp_down);
        if (file_no == -1) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_WORKER, 
                    "fclose fp down: %p fileno is -1", file->fp_down);
        }
        fclose(file->fp_down);
        file->fp_down = NULL;
    } else {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, 
                "close fp down: %p, filename: %s", file->fp_down, file->filename_down);
    }

    return 0;
}

int send_voice_data(voice_file_info_t *file)
{
    uint8_t mix_buf[VOICE_BUF_SIZE * MAX_BUF_NUM * 2];
    int ret;
    int i;

    if (file == NULL || file->tx_fd <= 0) {
        return -1;
    }
    
    for (i = 0; i < VOICE_BUF_SIZE * MAX_BUF_NUM; i++) {
        mix_buf[i * 2] = file->data_up[i];
        mix_buf[i * 2 + 1] = file->data_down[i];
    }
    ret = write(file->tx_fd, mix_buf, VOICE_BUF_SIZE * MAX_BUF_NUM * 2);
    applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER,
            "send %d bytes to player", VOICE_BUF_SIZE * MAX_BUF_NUM * 2);
    if (ret != VOICE_BUF_SIZE * MAX_BUF_NUM * 2) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "send_voice_data write %d bytes but return %d", 
                VOICE_BUF_SIZE * MAX_BUF_NUM * 2, ret);
        close(file->tx_fd);
        file->tx_fd = 0;
    }

    return 0;
}


voice_file_info_t *get_file_struct(uint64_t callid)
{
    uint32_t pos;
    uint8_t sr155_no;
    uint8_t e1_no;
    uint8_t ts_no;
    voice_file_info_t *filep;

    pos = (callid >> 15) & 0x1ffff;

    sr155_no = pos >> 11;
    e1_no = (pos >> 5) & 0x3f;
    ts_no = pos & 0x1f;

    if (sr155_no >= SR155_NUM || e1_no >= E1_NUM || ts_no >= TS_NUM) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "callid: %lx, sr155 no: %d, e1 no: %d, ts no: %d\n", callid, sr155_no, e1_no, ts_no);
        return NULL;
    }

    filep = &voice_file[sr155_no][e1_no][ts_no];

    return filep;
}


int cleanup_file_struct(voice_file_info_t *file)
{
    if (file == NULL) {
        return 0;
    }
    if (file->callid != 0) {
        atomic64_add(&call_timeout, 1);
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_CYCLE, "clean up file %p, callid: %lu, fp_up: %p, fp_down: %p, cdr: %p, up: %s, down: %s", 
                file, file->callid, file->fp_up, file->fp_down, 
                file->cdr_node, file->filename_up, file->filename_down);
        atomic64_add(&call_end, 1);
        atomic64_sub(&call_online, 1);
        file->callid = 0;
    }

    if (file->fp_up != NULL || file->fp_down != NULL) {
        write_voice_file(file);
        close_voice_file(file);
    }

    if (file->tx_fd > 0) {
        close(file->tx_fd);
        file->tx_fd = 0;
    }

    file->up_no = 0;
    file->down_no = 0;

#if 1
    if (file->cdr_node != NULL) {
        if (file->cdr_node->next != NULL) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_CYCLE, "timeout voice parser enqueue cdr next is NOT NULL, len: %u", file->fax_data_len_up);
        }
        memcpy(&(file->cdr_node->cdr), &(file->cdr), sizeof(STRUCT_CDR));
        file->cdr_node->next = NULL;
        EN_QUEUE(queue.fq, file->cdr_node);
        atomic64_add(&fax_enqueue, 1);
        file->cdr_node = NULL;
    }
#endif

    return 0;
}


int encrypt_voice(uint8_t *data, uint32_t data_len)
{
    uint32_t i;

    for (i = 0; i < data_len; i++) {
        data[i ] = data[i] ^ 0x53;
    }

    return 0;
}



inline int voice_parser(uint8_t *pkt, uint32_t pkt_len)
{
    uint64_t callid;
    uint8_t calldir;
    uint8_t ccase;
    uint8_t data_mark;
    uint8_t data_no;
    uint16_t data_len;
    voice_file_info_t *file;
    uint32_t data_buf_left;
    uint32_t fax_buf_left_up;
    uint32_t fax_buf_left_down;
    time_t c_time;

    if (pkt_len < 36 || pkt[16] != 0x03) {
        return -1;
    }

    memcpy(&callid, pkt + 20, 8);
    calldir = pkt[28];
    ccase = pkt[29];
    data_mark = pkt[33];
    data_no = pkt[34];
    data_len = *(uint16_t *)(pkt + 35);
    data_len = ntohs(data_len);
    if (data_len > 1024) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "error! voice data len: %d", data_len);
        return -2;
    }

    file = get_file_struct(callid);
    if (file == NULL) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "get_file_struct return NULL");
        return -1;
    }

    pthread_mutex_lock(&(file->mutex));
    file->update_time = time(NULL);

    c_time = (callid >> 32) & 0xffffffff;
    if (unlikely(file->update_time - c_time >= 18000)) {
        if (file->callid != 0) {
            file->up_end_flag = 1;
            file->down_end_flag = 1;
            applog(APP_LOG_LEVEL_NOTICE, APP_VPU_LOG_MASK_WORKER, "call %lx is too long, finish it!", callid);
        } else {
            pthread_mutex_unlock(&(file->mutex));
            return 0;
        }
    }

    if (file->callid != callid) {
        cleanup_file_struct(file);
        memset(file, 0, sizeof(voice_file_info_t));
#if 0
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, 
                "================this is a start packet================\n");
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, 
                "callid:     %016llx\n", (long long unsigned int)callid);
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "calldir:    %d\n", calldir);
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "ccase:      %d\n", ccase);
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "data mark:  %d\n", data_mark);
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "data len:   %d\n", data_len);
#endif

        atomic64_add(&call_start, 1);
        atomic64_add(&call_online, 1);
        file->callid = callid;
        file->case_mark = ccase;
        file->data_mark = data_mark;
        file->up_lost = 0;
        file->down_lost = 0;
        file->up_no = 1;
        file->down_no = 1;
        file->cdr.callid = callid;
        file->cdr.time = time(NULL);
        file->cdr.path = 0;
        file->cdr.ruleflag = ccase;
        file->cdr.callflag = 0;

        if (open_voice_file(file) == 0) {
            atomic64_add(&file_ok, 1);

            file->cdr_node = FETCH_PHEAD(queue.eq);
            file->fax_data_len_up = 0;
            file->fax_data_len_down = 0;
            if (file->cdr_node != NULL) {
                memset(file->cdr_node, 0, sizeof(STRUCT_CDR_PATH));
            } else {
                atomic64_add(&fax_lost, 1);
                cdr_insert(&(file->cdr));
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, "FETCH_PHEAD return NULL");
            }
        } else {
            atomic64_add(&file_fail, 1);
        }
    }

    if (calldir == 1) {
        if (file->up_no != data_no) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, 
                    "callid %016lx up packet num should be %u, but current packet num is %u", callid, file->up_no, data_no);
            file->up_lost++;
        }
        file->up_no = data_no + 1;
        if (file->data_count_up >= VOICE_BUF_SIZE * MAX_BUF_NUM) {
            if (unlikely(file->tx_fd > 0)) {
                send_voice_data(file);
            }
            write_voice_file(file);
            file->data_count_up = 0;
            file->data_count_down = 0;
        }
        data_buf_left = (VOICE_BUF_SIZE * MAX_BUF_NUM) - file->data_count_up;
        data_len = data_buf_left < data_len ? data_buf_left : data_len;

        memcpy(file->data_up + file->data_count_up, pkt + 37, data_len);
        encrypt_voice(file->data_up + file->data_count_up, data_len);
        file->data_count_up += data_len;
        if (data_mark == 0x02) {
            file->up_end_flag = 1;
        }

#if 1
        if (file->cdr_node != NULL && file->fax_data_len_up < FAX_BUF_SIZE) {
            fax_buf_left_up = FAX_BUF_SIZE - file->fax_data_len_up;
            data_len = data_len < fax_buf_left_up ? data_len : fax_buf_left_up;
            memcpy(file->cdr_node->buf_up + file->fax_data_len_up, pkt + 37, data_len);
            file->fax_data_len_up += data_len;
        }
#endif

    } else {
        if (file->down_no != data_no) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, 
                    "callid %016lx down packet num should be %u, but current packet num is %u", callid, file->down_no, data_no);
            file->down_lost++;
        }
        file->down_no = data_no + 1;
        if (file->data_count_down >= VOICE_BUF_SIZE * MAX_BUF_NUM) {
            if (unlikely(file->tx_fd > 0)) {
                send_voice_data(file);
            }
            write_voice_file(file);
            file->data_count_up = 0;
            file->data_count_down = 0;
        }
        data_buf_left = (VOICE_BUF_SIZE * MAX_BUF_NUM) - file->data_count_down;
        data_len = data_buf_left < data_len ? data_buf_left : data_len;

        memcpy(file->data_down + file->data_count_down, pkt + 37, data_len);
        encrypt_voice(file->data_down + file->data_count_down, data_len);
        file->data_count_down += data_len;
        if (data_mark == 0x02) {
            file->down_end_flag = 1;
        }

#if 1
        if (file->cdr_node != NULL && file->fax_data_len_down < FAX_BUF_SIZE) {
            fax_buf_left_down = FAX_BUF_SIZE - file->fax_data_len_down;
            data_len = data_len < fax_buf_left_down ? data_len : fax_buf_left_down;
            memcpy(file->cdr_node->buf_down + file->fax_data_len_down, pkt + 37, data_len);
            file->fax_data_len_down += data_len;
        }
#endif
    }

    if (file->up_end_flag == 1 && file->down_end_flag == 1) {
#if 0
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, 
                "call  %016lx  end\n", callid);
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, 
                "================this is a end packet================\n");
#endif

        file->update_time = 0;
        file->callid = 0;
        file->up_end_flag = 0;
        file->down_end_flag = 0;
        file->up_no = 0;
        file->down_no = 0;
        if (file->up_lost != 0 || file->down_lost != 0) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_WORKER, 
                    "voice packet lost, callid: %016lx up_lost: %u down_lost: %u", callid, file->up_lost, file->down_lost);
        }

        if (unlikely(file->tx_fd > 0)) {
            send_voice_data(file);
        }
        write_voice_file(file);
        close_voice_file(file);

        atomic64_add(&call_end, 1);
        atomic64_sub(&call_online, 1);
        
        if (file->tx_fd > 0) {
            close(file->tx_fd);
            file->tx_fd = 0;
        }
#if 1
        if (file->cdr_node != NULL) {
            memcpy(&(file->cdr_node->cdr), &(file->cdr), sizeof(STRUCT_CDR));
            file->cdr_node->next = NULL;
            EN_QUEUE(queue.fq, file->cdr_node);
            file->cdr_node = NULL;
            atomic64_add(&fax_enqueue, 1);
        }
#endif

    }

    pthread_mutex_unlock(&(file->mutex));

    return 0;
}


