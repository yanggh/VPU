#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "vpu.h"
#include "vpu_worker.h"
#include "voice_parser.h"
#include "conf.h"
#include "file_server.h"

#define SERVER_PORT         8080
#define REAL_TIME_PLAY      0x01
#define NON_REAL_TIME_PLAY  0x00

#define MAX_FILE_SIZE 0xffffffff


#define TYPE_VOICE	0
#define TYPE_FAX	1



extern int encrypt_voice(uint8_t *data, uint32_t data_len);
int send_result(uint16_t result, int sockfd)
{
    uint8_t buf[8];
    uint8_t *p;
    p = buf;

    *((uint16_t *)p) = htons(0x02);
    *((uint16_t *)(p + 2)) = htons(0x02);
    *((uint16_t *)(p + 4)) = htons(result);

    write(sockfd, buf, 6);

    return 0;
}


int send_voice_size(uint32_t size, int sockfd)
{
    uint8_t buf[8];
    uint8_t *p = buf;

    *((uint16_t *)p) = htons(0x03);
    *((uint16_t *)(p + 2)) = htons(0x04);
    *((uint32_t *)(p + 4)) = htonl(size);

    write(sockfd, buf, 8);

    return 0;
}


int send_faxpic_size(uint32_t size, int sockfd)
{
    uint8_t buf[8];
    uint8_t *p = buf;

    *((uint16_t *)p) = htons(0x04);
    *((uint16_t *)(p + 2)) = htons(0x04);
    *((uint32_t *)(p + 4)) = htonl(size);

    write(sockfd, buf, 8);

    return 0;
}




int cmd_parser(uint8_t *buf, int buf_len, uint64_t *callid, uint16_t *scase, uint16_t *stime, uint16_t *stype)
{
    uint16_t cmd_type;
    uint16_t cmd_len;
    uint8_t *data;
    int data_left;
    uint16_t data_type;
    uint16_t data_len;
    uint64_t *callid_p = NULL;
    uint16_t *scase_p = NULL;
    uint16_t *stime_p = NULL;
    uint16_t *stype_p = NULL;

    if (buf == NULL || buf_len <= 4 || callid == NULL 
            || scase == NULL || stime == NULL) {
        return -1;
    }

    cmd_type = ntohs(*(uint16_t *)buf);
    cmd_len = ntohs(*(uint16_t *)(buf + 2));
    if (cmd_type != 0x01 || cmd_len != (buf_len - 4)) {
        return -1;
    }

    data = buf + 4;
    data_left = buf_len - 4;

    while (data_left > 4) {
        data_type = ntohs(*(uint16_t *)data);
        data_len = ntohs(*(uint16_t *)(data + 2));
        switch(data_type) {
            case 0x01:
                memcpy(callid, data + 4, 8);
                callid_p = callid;
                break;
            case 0x02:
                *scase = ntohs(*(uint16_t *)(data + 4));
                scase_p = scase;
                break;
            case 0x03:
                *stime = ntohs(*(uint16_t *)(data + 4));
                stime_p = stime;
                break;
            case 0x04:
                *stype = ntohs(*(uint16_t *)(data + 4));
                stype_p = stype;
                break;
            default:
                return -1;
                break;
        }

        data = data + data_len + 4;
        data_left = data_left - data_len - 4;
    }

    if (callid_p == NULL || scase_p == NULL || stime_p == NULL || stype_p == NULL) {
        return -1;
    }

    return 0;
}


int check_voice_file(uint64_t callid, uint16_t scase, char *filename_up, char *filename_down, uint32_t *file_size)
{
    int ret;
    int num;
    int i;
    char *mid_dir;
    char *dir;

    time_t call_time;
    uint32_t pos;
    uint8_t e1_no;
    uint8_t ts_no;

    struct tm *tms;
    struct tm tm_buf;

    struct stat statbuff;

    uint32_t up_size = 0;
    uint32_t down_size = 0;

    uint8_t up_flag = 0;
    uint8_t down_flag = 0;

    if (filename_up == NULL || filename_down == NULL) {
        return -1;
    }

    call_time = (callid >> 32) & 0xffffffff;
    pos = (callid >> 15) & 0x1ffff;
    e1_no = (pos >> 5) & 0x3f;
    ts_no = pos & 0x1f;

    tms = localtime_r(&call_time, &tm_buf);
    if (tms == NULL) {
        return -2;
    }

    mid_dir = CASE_DIR;
    ret = -1;
    num = file_dirs.num;
    for (i = 0; i < num; i++) {
        dir = file_dirs.dirs[i].dir_name;
        snprintf(filename_up, MAX_DIRS_LEN, "%s/%s/%04d-%02d-%02d/%02d/%02d/%016lx_up", 
                dir, mid_dir, tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday, 
                e1_no, ts_no, callid);
        snprintf(filename_down, MAX_DIRS_LEN, "%s/%s/%04d-%02d-%02d/%02d/%02d/%016lx_down", 
                dir, mid_dir, tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday, 
                e1_no, ts_no, callid);

        up_flag = 0;
        down_flag = 0;
        memset(&statbuff, 0, sizeof(struct stat));
        if (stat(filename_up, &statbuff) == 0) {
            if (S_ISREG(statbuff.st_mode)) {
                up_flag = 1;
                up_size = statbuff.st_size;
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "up file %s exist, size: %u", filename_up, up_size);
            } else {
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "up file %s is not a regular file", filename_up);
            }
        } else {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "up file %s is not exist", filename_up);
        }
        if (stat(filename_down, &statbuff) == 0) {
            if (S_ISREG(statbuff.st_mode)) {
                down_flag = 1;
                down_size = statbuff.st_size;
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "down file %s exist, size: %u", filename_down, down_size);
            } else {
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "down file %s is not a regular file", filename_down);
            }
        } else {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "down file %s is not exist", filename_down);
        }

        if (up_flag == 1 && down_flag == 1) {
            ret = 0;
            *file_size = 2 * (up_size < down_size ? up_size : down_size);
            break;
        }
    }

	if (scase == 1 || ret == 0) {
        return ret;
    }

    mid_dir = NORMAL_DIR;
    ret = -1;
    num = file_dirs.num;
    for (i = 0; i < num; i++) {
        dir = file_dirs.dirs[i].dir_name;
        snprintf(filename_up, MAX_DIRS_LEN, "%s/%s/%04d-%02d-%02d/%02d/%02d/%016llx_up", 
                dir, mid_dir, tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday, 
                e1_no, ts_no, (long long unsigned int)callid);
        snprintf(filename_down, MAX_DIRS_LEN, "%s/%s/%04d-%02d-%02d/%02d/%02d/%016llx_down", 
                dir, mid_dir, tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday, 
                e1_no, ts_no, (long long unsigned int)callid);

        up_flag = 0;
        down_flag = 0;
        memset(&statbuff, 0, sizeof(struct stat));
        if (stat(filename_up, &statbuff) == 0) {
            if (S_ISREG(statbuff.st_mode)) {
                up_flag = 1;
                up_size = statbuff.st_size;
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "up file %s exist, size: %u", filename_up, up_size);
            } else {
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "up file %s is not a regular file", filename_up);
            }
        } else {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "up file %s is not exist", filename_up);
        }
        if (stat(filename_down, &statbuff) == 0) {
            if (S_ISREG(statbuff.st_mode)) {
                down_flag = 1;
                down_size = statbuff.st_size;
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "down file %s exist, size: %u", filename_down, down_size);
            } else {
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "down file %s is not a regular file", filename_down);
            }
        } else {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "down file %s is not exist", filename_down);
        }

        if (up_flag == 1 && down_flag == 1) {
            ret = 0;
            *file_size = 2 * (up_size < down_size ? up_size : down_size);
            break;
        }
    }

    return ret;
}


int get_fax_pic_info(char *filename_up, char *filename_down, fax_pic_info_t *fax_pic, uint32_t *data_size)
{
    char fname_tmp[MAX_DIRS_LEN] = {0};
    struct stat statbuff;
    int pic_num = 0;

    if (fax_pic == NULL) {
        return -1;
    }
    *data_size = 0;

    if (filename_up != NULL) {
        snprintf(fname_tmp, MAX_DIRS_LEN, "%s.tif", filename_up);
        memset(&statbuff, 0, sizeof(struct stat));
        if (stat(fname_tmp, &statbuff) == 0 && S_ISREG(statbuff.st_mode)) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "fax picture %s exist", fname_tmp);
            snprintf(fax_pic[pic_num].name, MAX_DIRS_LEN, "%s", fname_tmp);
            fax_pic[pic_num].size = statbuff.st_size;
            *data_size += (fax_pic[pic_num].size + 4);
            pic_num += 1;
        } else {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "fax picture %s not exist", fname_tmp);
        }
    }
    if (filename_down != NULL) {
        snprintf(fname_tmp, MAX_DIRS_LEN, "%s.tif", filename_down);

        memset(&statbuff, 0, sizeof(struct stat));
        if (stat(fname_tmp, &statbuff) == 0 && S_ISREG(statbuff.st_mode)) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "fax picture %s exist", fname_tmp);
            snprintf(fax_pic[pic_num].name, MAX_DIRS_LEN, "%s", fname_tmp);
            fax_pic[pic_num].size = statbuff.st_size;
            *data_size += (fax_pic[pic_num].size + 4);
            pic_num += 1;
        } else {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "fax picture %s not exist", fname_tmp);
        }
    }

    return pic_num;
}

void *file_tx_thread(void *arg)
{
    int connfd;
    uint8_t buf[2048];
    int data_len;
    uint64_t callid;
    uint16_t scase;
    uint16_t stime;
    uint16_t stype;
    int fileup_fd;
    int filedown_fd;
    char filename_up[MAX_DIRS_LEN] = {0};
    char filename_down[MAX_DIRS_LEN] = {0};
    uint8_t fileup_buf[1024];
    uint8_t filedown_buf[1024];
    uint8_t filemix_buf[2048];
    uint32_t up_len;
    uint32_t down_len;
    voice_file_info_t *filep;
    int i;
    int ret;
    uint32_t file_size;
    fax_pic_info_t fax_pic[2];
    int faxpic_fd;
    uint8_t faxpic_buf[1024];
    uint32_t faxpic_len;

    connfd = (int)(uint64_t)arg;

    data_len = read(connfd, buf, 2048);
    ret = cmd_parser(buf, data_len, &callid, &scase, &stime, &stype);
    if (ret != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FILESRV, "data from streamweb parser error\n");
        send_result(0x00, connfd);
        close(connfd);
        pthread_exit(NULL);
    }
    applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "callid: %016lx, scase: %d, stime: %d", callid, scase, stime);

    if (stype == TYPE_VOICE && stime == NON_REAL_TIME_PLAY) {
        
        if (0 != check_voice_file(callid, scase, filename_up, filename_down, &file_size)) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_FILESRV, "file_tx_thread check_voice_file return NULL");
            send_result(0x00, connfd);
            close(connfd);
            pthread_exit(NULL);
        }

        fileup_fd = open(filename_up, O_RDONLY);
        filedown_fd = open(filename_down, O_RDONLY);
        if (fileup_fd == -1 || filedown_fd == -1) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "failed to open file %s or %s\n", filename_up, filename_down);
            send_result(0x00, connfd);
            close(fileup_fd);
            close(filedown_fd);
            close(connfd);
            pthread_exit(NULL);
        }
        send_result(0x01, connfd);
        send_voice_size(file_size, connfd);
        while (1) {
            up_len = read(fileup_fd, fileup_buf, 1024);
            down_len = read(filedown_fd, filedown_buf, 1024);
            if (up_len != down_len || up_len <= 0 || down_len <= 0) {
                break;
            }

            for (i = 0; i < up_len; i++) {
                filemix_buf[i * 2] = fileup_buf[i];
                filemix_buf[i * 2 + 1] = filedown_buf[i];
            }

            write(connfd, filemix_buf, up_len + down_len);
        }

        close(connfd);
        close(fileup_fd);
        close(filedown_fd);
    } else if (stime == REAL_TIME_PLAY) {
        filep = get_file_struct(callid);
        if (filep == NULL) {
            //send fail info to client
            send_result(0x00, connfd);
            close(connfd);
        } else {
            if (filep->tx_fd > 0) {
                applog(APP_LOG_LEVEL_NOTICE, APP_VPU_LOG_MASK_FILESRV, "someone is listening this call\n");
                send_result(0x00, connfd);
                close(connfd);
            } else {
                send_result(0x01, connfd);
                send_voice_size(MAX_FILE_SIZE, connfd);
                filep->tx_fd = connfd;
            }
        }
    } else if (stype == TYPE_FAX) {
        if (0 != check_voice_file(callid, scase, filename_up, filename_down, &file_size)) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "file_tx_thread check_voice_file return NULL");
            send_result(0x00, connfd);
            close(connfd);
            pthread_exit(NULL);
        }

        memset(fax_pic, 0, sizeof(fax_pic));
        ret = get_fax_pic_info(filename_up, filename_down, fax_pic, &file_size);
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "fax picture num: %d", ret);
        if (ret <= 0) {
            send_result(0x00, connfd);
            close(connfd);
            pthread_exit(NULL);
        }
        send_result(0x01, connfd);
        send_faxpic_size(file_size, connfd);
        for (i = 0; i < ret; i++) {
            faxpic_fd = open(fax_pic[i].name, O_RDONLY);
            if (faxpic_fd != -1) {
            	applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "open %s success", fax_pic[i].name);
                *((uint32_t *)(faxpic_buf + 4)) = htonl(fax_pic[i].size);
                encrypt_voice(faxpic_buf + 4, 4);
                write(connfd, faxpic_buf + 4, 4);
                while ((faxpic_len = read(faxpic_fd, faxpic_buf, 1024)) > 0) {
                    write(connfd, faxpic_buf, faxpic_len);
                }
                close(faxpic_fd);
            } else {
            	applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "open %s failed", fax_pic[i].name);
            }
        }
        close(connfd);
    }

    pthread_exit(NULL);
}

int create_tcp_server(char *ip, uint16_t port)
{
    int listenfd;
    struct sockaddr_in servaddr;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd == -1) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "create_tcp_server create socket fail");
        return -1;
    }

    if (port == 0) {
        port = SERVER_PORT;
    }
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    if (ip == NULL) {
        servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
        servaddr.sin_addr.s_addr = inet_addr(ip);
    }
    servaddr.sin_port = htons(port);

    if(bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "create_tcp_server bind socket fail\n");
        close(listenfd);
        return -1;
    }

    if(listen(listenfd, 2048) == -1){
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, "create_tcp_server listen socket fail\n");
        close(listenfd);
        return -1;
    }

    return listenfd;
}

void *file_server_thread(__attribute__((unused))void *arg)
{
    int listenfd;
    int connfd;
    int nfds;
    fd_set readfds;
    int ret;
    pthread_t pid_tx;
    pthread_attr_t attr;
    struct timeval tv;
    uint32_t conn_times;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    listenfd = -1;
    conn_times = 0;
    while (1) {
#if 1
        if (unlikely(svm_signal_flags & SVM_DONE)) {
            break;
        }

        if (unlikely(svm_vpu_reload == 1)) {
            close(listenfd);
            listenfd = -1;
            svm_vpu_reload = 0;
        }

        if (listenfd == -1) {
            listenfd = create_tcp_server(server_ip, server_port);
            if (listenfd == -1) {
                if (++conn_times >= 60) {
                    conn_times = 0;
                    applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FILESRV, "create file server failed");
                }
                sleep(1);
                continue;
            }
        }

        tv.tv_sec = 1;
        tv.tv_usec = 0;
        FD_ZERO(&readfds);
        FD_SET(listenfd, &readfds);
        nfds = listenfd + 1;
        ret = select(nfds, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            continue;
        }
        if (FD_ISSET(listenfd, &readfds)) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FILESRV, 
                    "file_server_thread someone connect to me\n");
            if((connfd = accept(listenfd, (struct sockaddr*)NULL, NULL)) == -1){
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FILESRV, 
                        "accept socket error: %s(errno: %d)",strerror(errno),errno);
                continue;
            }

            if (pthread_create(&pid_tx, &attr, (void *)file_tx_thread, (void *)(uint64_t)connfd) != 0) {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FILESRV, 
                        "thread file_tx_thread create failed\n");
            }
        }
#else
        if((connfd = accept(listenfd, (struct sockaddr*)NULL, NULL)) == -1){
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FILESRV, 
                    "file_server_thread accept socket error: %s(errno: %d)",strerror(errno),errno);
            continue;
        }
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_FILESRV, "file_server_thread someone connect to me\n");

        if (pthread_create(&pid_tx, &attr, (void *)file_tx_thread, (void *)(uint64_t)connfd) != 0) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FILESRV, "thread file_tx_thread create failed\n");
        }

#endif

    }
    close(listenfd);

    pthread_attr_destroy(&attr);

    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_FILESRV, "pthread file_server_thread exit");
    pthread_exit(NULL);
}

