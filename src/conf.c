#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <arpa/inet.h>
#include "yaml_conf.h"
#include "conf.h"
#include "atomic.h"
#include "vpu.h"

vpu_param_t vpu_conf;

file_dirs_info_t file_dirs;
file_dirs_info_t r_file_dirs;

atomic32_t file_dir_no;

uint32_t vpu_id = 0;
uint8_t path_num = 0;

uint32_t storage_time = 90;
uint32_t disk_left = 5;

char cdr_ip[16];
uint16_t cdr_port;

char server_ip[16];
uint32_t server_port;

fax_info_t fax_infos[FAX_NUM_MAX];
uint32_t fax_num = 0;

fax_info_t r_fax_infos[FAX_NUM_MAX];
uint32_t r_fax_num = 0;

uint8_t svm_cdr_reload = 0;
uint8_t svm_fax_reload = 0;
uint8_t svm_vpu_reload = 0;

uint8_t dms_sguard_reload = 0;
uint8_t dms_ma_reload = 0;

uint8_t vpu_dev_reload = 0;
uint8_t vpu_dir_reload = 0;

int change_fax_status(uint32_t fax_id, uint32_t status)
{
    int i;

    if (fax_id == 0) {
        return -1;
    }

    for (i = 0; i < fax_num; i++) {
        if (fax_infos[i].id == fax_id) {
            fax_infos[i].flag = status;
            applog(APP_LOG_LEVEL_NOTICE, APP_VPU_LOG_MASK_BASE, "fax %u change status to %u", fax_id, status);
        }
    }

    return 0;
}


int get_cdr_ip_and_port(char *ip, uint32_t len, uint32_t *port)
{
    if (ip == NULL || len < 16 || port == NULL) {
        return -1;
    }

    strncpy(ip, cdr_ip, len);
    *port = cdr_port;

    return 0;
}



char *get_dev(void)
{
    char *p = NULL;

    p = vpu_conf.dev;

    return p;
}

int fill_file_dirs(char *dir, uint8_t disk)
{
    uint32_t num = file_dirs.num;
    struct stat dir_stat;

    if (dir == NULL || num > MAX_DIRS_NUM) {
        return -1;
    }

    if (stat(dir, &dir_stat) != 0 || (dir_stat.st_mode & S_IFMT) != S_IFDIR) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "%s is not exist or not a dir\n", dir);
        return -1;
    }

    if (strlen(dir) >= MAX_DIRS_LEN) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "%s len %d longer than %d\n", dir, (int)strlen(dir), MAX_DIRS_LEN);
        return -1;
    }
    strncpy(file_dirs.dirs[num].dir_name, dir, MAX_DIRS_LEN - 1);
    file_dirs.dirs[num].disk_num = disk;
    file_dirs.num = num + 1;
    file_dirs.dirs[num].write_flag = 1;

    return 0;
}


int reload_fill_file_dirs(char *dir, uint8_t disk)
{
    uint32_t num = r_file_dirs.num;
    struct stat dir_stat;

    if (dir == NULL || num > MAX_DIRS_NUM) {
        return -1;
    }

    if (stat(dir, &dir_stat) != 0 || (dir_stat.st_mode & S_IFMT) != S_IFDIR) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload %s is not exist or not a dir\n", dir);
        return -1;
    }

    if (strlen(dir) >= MAX_DIRS_LEN) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload %s len %d longer than %d\n", dir, (int)strlen(dir), MAX_DIRS_LEN);
        return -1;
    }
    strncpy(r_file_dirs.dirs[num].dir_name, dir, MAX_DIRS_LEN - 1);
    r_file_dirs.dirs[num].disk_num = disk;
    r_file_dirs.num = num + 1;
    r_file_dirs.dirs[num].write_flag = 1;

    return 0;
}





int create_file_dirs(char *root, int year, int month, int day)
{
    int ret;
    int i, j;
    char date_dir[256] = {0};
    char e1_dir[256] = {0};
    char ts_dir[256] = {0};
    struct stat dir_stat;

    if (root == NULL) {
        return -1;
    }

    snprintf(date_dir, 256, "%s/%s/%04d-%02d-%02d", root, NORMAL_DIR, year, month, day);

    if (stat(date_dir, &dir_stat) == 0 && (dir_stat.st_mode & S_IFMT) == S_IFDIR) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "dir %s exist already\n", date_dir);
        return -2;
    }

    ret = mkdir(date_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (ret != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "mkdir %s error\n", date_dir);
        return -1;
    }
    for (i = 0; i < 64; i++) {
        snprintf(e1_dir, 256, "%s/%02d", date_dir, i);
        ret = mkdir(e1_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if (ret != 0) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "mkdir %s error\n", e1_dir);
            return -1;
        }
        for (j = 0; j < 32; j++) {
            snprintf(ts_dir, 256, "%s/%02d", e1_dir, j);
            ret = mkdir(ts_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
            if (ret != 0) {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "mkdir %s error\n", ts_dir);
                return -1;
            }
        }
    }


    memset(date_dir, 0, 256);
    memset(e1_dir, 0, 256);
    memset(ts_dir, 0, 256);
    snprintf(date_dir, 256, "%s/%s/%04d-%02d-%02d", root, CASE_DIR, year, month, day);
    ret = mkdir(date_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
    if (ret != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "mkdir %s error\n", date_dir);
        return -1;
    }
    for (i = 0; i < 64; i++) {
        snprintf(e1_dir, 256, "%s/%02d", date_dir, i);
        ret = mkdir(e1_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        if (ret != 0) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "mkdir %s error\n", e1_dir);
            return -1;
        }
        for (j = 0; j < 32; j++) {
            snprintf(ts_dir, 256, "%s/%02d", e1_dir, j);
            ret = mkdir(ts_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
            if (ret != 0) {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "mkdir %s error\n", ts_dir);
                return -1;
            }
        }
    }

    return 0;
}


void create_all_dirs(int year, int mon, int day)
{
    int i;

    for (i = 0; i < file_dirs.num; i++) {
        create_file_dirs(file_dirs.dirs[i].dir_name, year, mon, day);
    }

    return;
}



void init_file_dirs(void)
{
    time_t time_cur;
    struct tm today;
    time_t time_nextday;
    struct tm nextday;
    char file_dir[256] = {0};
    struct stat dir_stat;
    int ret;
    char *root_dir;
    uint32_t num = file_dirs.num;
    int i;

    if (num == 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "no available file dir, vpu exit");
        exit(-1);
    }

    for (i = 0; i < num; i++) {
        root_dir = file_dirs.dirs[i].dir_name;
        snprintf(file_dir, 256, "%s/%s", root_dir, NORMAL_DIR);

        if (stat(file_dir, &dir_stat) != 0 || (dir_stat.st_mode & S_IFMT) != S_IFDIR) {
            ret = mkdir(file_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
            if (ret != 0) {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "mkdir %s error\n", file_dir);
                exit(-1);
            }
        }

        snprintf(file_dir, 256, "%s/%s", root_dir, CASE_DIR);

        if (stat(file_dir, &dir_stat) != 0 || (dir_stat.st_mode & S_IFMT) != S_IFDIR) {
            ret = mkdir(file_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
            if (ret != 0) {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "mkdir %s error\n", file_dir);
                exit(-1);
            }
        }

        time_cur = time(NULL);
        localtime_r(&time_cur, &today);
        create_file_dirs(root_dir, today.tm_year + 1900, today.tm_mon + 1, today.tm_mday);

        time_nextday = time_cur + 24 * 60 * 60;
        localtime_r(&time_nextday, &nextday);
        create_file_dirs(root_dir, nextday.tm_year + 1900, nextday.tm_mon + 1, nextday.tm_mday);
    }

    return;
}


void init_reload_file_dirs(void)
{
    struct timeval tval;
    struct tm local_tm;
    struct tm *tms;
    char file_dir[256] = {0};
    struct stat dir_stat;
    int ret;
    uint32_t num = r_file_dirs.num;
    char *root_dir;
    int i;

    if (num == 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload no available file dir");
        return;
    }

    vpu_dir_reload = 1;
    for (i = 0; i < num; i++) {
        root_dir = r_file_dirs.dirs[i].dir_name;
        snprintf(file_dir, 256, "%s/%s", root_dir, NORMAL_DIR);

        if (stat(file_dir, &dir_stat) != 0 || (dir_stat.st_mode & S_IFMT) != S_IFDIR) {
            ret = mkdir(file_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
            if (ret != 0) {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "mkdir %s error\n", file_dir);
                exit(-1);
            }
        }

        snprintf(file_dir, 256, "%s/%s", root_dir, CASE_DIR);

        if (stat(file_dir, &dir_stat) != 0 || (dir_stat.st_mode & S_IFMT) != S_IFDIR) {
            ret = mkdir(file_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
            if (ret != 0) {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "mkdir %s error\n", file_dir);
                exit(-1);
            }
        }

        memset(&tval, 0, sizeof(struct timeval));
        gettimeofday(&tval, NULL);
        tms = (struct tm *)localtime_r(&tval.tv_sec, &local_tm);

        create_file_dirs(root_dir, tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday);
        create_file_dirs(root_dir, tms->tm_year + 1900, tms->tm_mon + 1, tms->tm_mday + 1);
    }

    return;
}




static int get_local_mac(vpu_param_t *vpu_conf)
{
    struct ifreq ifreq;
    int sock = 0;

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
    {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "Get mac: Create socket error!");
        return 1;
    }

    strcpy(ifreq.ifr_name, vpu_conf->dev);
    if(ioctl(sock,SIOCGIFHWADDR,&ifreq) < 0)
    {
        return 2;
    }
    memcpy(vpu_conf->dev_mac, ifreq.ifr_hwaddr.sa_data, 6);
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "dev mac: %02x:%02x:%02x:%02x:%02x:%02x", 
            vpu_conf->dev_mac[0], vpu_conf->dev_mac[1], vpu_conf->dev_mac[2], 
            vpu_conf->dev_mac[3], vpu_conf->dev_mac[4], vpu_conf->dev_mac[5]);
    close(sock);

    return 0;
}



int get_vpu_id(void)
{
    char *value = NULL;

    if (ConfGet("id", &value) == 1) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "vpu id: %s\n", value);
        vpu_id = atoi(value);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "no vpu id in vpu config file!\n");
        exit(1);
    }

    return 0;
}


int reload_vpu_id(void)
{
    char *value = NULL;

    if (ConfGet("id", &value) == 1) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "reload vpu id: %s\n", value);
        vpu_id = atoi(value);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload no vpu id in vpu config file!\n");
    }

    return 0;
}



int dev_parser(void)
{
    char *value = NULL;

    if (ConfGet("dev", &value) == 1) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "dev: %s\n", value);
        if (strlen(value) >= 64) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "device name %s too long\n", value);
            exit(1);
        }
        strcpy(vpu_conf.dev, value);
        if (get_local_mac(&vpu_conf)) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "device %s is not exist!\n", vpu_conf.dev);
            exit(1);
        }
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "no device name in vpu config file!\n");
        exit(1);
    }

    return 0;
}



int reload_dev_parser(void)
{
    char *value = NULL;
    char dev[64] = {0};

    if (ConfGet("dev", &value) == 1) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "reload dev: %s", value);
        if (strlen(value) >= 64) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload device name %s too long", value);
            return -1;
        }
        strcpy(dev, value);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload no device name in vpu config file!\n");
        return -1;
    }

    if (strcmp(dev, vpu_conf.dev) != 0) {
        vpu_dev_reload = 1;
    }

    return 0;
}






int file_dirs_parser(void)
{
    ConfNode *base;
    ConfNode *child;
    char *dir;
    uint8_t disknum;
    uint8_t flag;

    base = ConfGetNode("filedirs");
    if (base == NULL) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "vpu not get file dirs from config file");
        exit(-1);
    }

    memset(&file_dirs, 0, sizeof(file_dirs_info_t));
    atomic32_init(&file_dir_no);
    TAILQ_FOREACH(child, &base->head, next) {
        if (!strcmp(child->val, "dir")) {
            ConfNode *subchild;
            dir = NULL;
            disknum = 0;
            flag = 0;
            TAILQ_FOREACH(subchild, &child->head, next) {
                if ((!strcmp(subchild->name, "dir"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "file dir: %s", subchild->val);
                    dir = subchild->val;
                }
                if ((!strcmp(subchild->name, "disk"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "disk num: %s", subchild->val);
                    disknum = atoi(subchild->val);
                }
            }

            if (0 != fill_file_dirs(dir, disknum)) {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "file dir: %s, disk num: %d", dir, disknum);
            }
        }
    }

    init_file_dirs();

    return 0;
}



int reload_file_dirs(void)
{
    ConfNode *base;
    ConfNode *child;
    char *dir;
    uint8_t disknum;
    uint8_t flag;

    base = ConfGetNode("filedirs");
    if (base == NULL) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "vpu not get file dirs from config file");
        return -1;
    }

    memset(&r_file_dirs, 0, sizeof(file_dirs_info_t));
    TAILQ_FOREACH(child, &base->head, next) {
        if (!strcmp(child->val, "dir")) {
            ConfNode *subchild;
            dir = NULL;
            disknum = 0;
            flag = 0;
            TAILQ_FOREACH(subchild, &child->head, next) {
                if ((!strcmp(subchild->name, "dir"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "file dir: %s", subchild->val);
                    dir = subchild->val;
                }
                if ((!strcmp(subchild->name, "disk"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "disk num: %s", subchild->val);
                    disknum = atoi(subchild->val);
                }
            }

            if (0 != reload_fill_file_dirs(dir, disknum)) {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "file dir: %s, disk num: %d", dir, disknum);
            }
        }
    }

    if (memcmp(&file_dirs, &r_file_dirs, sizeof(file_dirs_info_t)) != 0) {
        init_reload_file_dirs();
    }

    return 0;
}


int reload_fax_ip_port(void)
{
    ConfNode *base;
    ConfNode *child;
    int i;

    memset(r_fax_infos, 0, sizeof(r_fax_infos));
    r_fax_num = 0;

    base = ConfGetNode("fax");
    if (base == NULL) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload vpu not get file dirs from config file");
        return -1;
    }

    TAILQ_FOREACH(child, &base->head, next) {
        if (!strcmp(child->val, "id")) {
            ConfNode *subchild;
            TAILQ_FOREACH(subchild, &child->head, next) {
                if ((!strcmp(subchild->name, "id"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "fax id: %s", subchild->val);
                    r_fax_infos[r_fax_num].id = atoi(subchild->val);
                    r_fax_infos[r_fax_num].flag = 0;
                    r_fax_infos[r_fax_num].fd = -1;
                } else if ((!strcmp(subchild->name, "ip"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "fax ip: %s", subchild->val);

                    if (strlen(subchild->val) >= 16) {
                        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "fax ip %s error\n", subchild->val);
                    } else {
                        strcpy(r_fax_infos[r_fax_num].ip, subchild->val);
                    }
                } else if ((!strcmp(subchild->name, "port"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "fax port: %s", subchild->val);
                    r_fax_infos[r_fax_num].port = atoi(subchild->val);
                }
            }
        }
        r_fax_num += 1;
    }

    if (fax_num != r_fax_num) {
        svm_fax_reload = 1;
        return 0;
    }

    for (i = 0; i < fax_num; i++) {
        if (strcmp(r_fax_infos[i].ip, fax_infos[i].ip) != 0 
                || r_fax_infos[i].port != fax_infos[i].port) {
            svm_fax_reload = 1;
            break;
        }
    }

    return 0;
}




int get_fax_ip_port(void)
{
    ConfNode *base;
    ConfNode *child;

    memset(fax_infos, 0, sizeof(fax_infos));
    fax_num = 0;

    base = ConfGetNode("fax");
    if (base == NULL) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "vpu not get file dirs from config file");
        return -1;
    }

    TAILQ_FOREACH(child, &base->head, next) {
        if (!strcmp(child->val, "id")) {
            ConfNode *subchild;
            TAILQ_FOREACH(subchild, &child->head, next) {
                if ((!strcmp(subchild->name, "id"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "fax id: %s", subchild->val);
                    fax_infos[fax_num].id = atoi(subchild->val);
                    fax_infos[fax_num].flag = 1;
                    fax_infos[fax_num].fd = -1;
                } else if ((!strcmp(subchild->name, "ip"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "fax ip: %s", subchild->val);

                    if (strlen(subchild->val) >= 16) {
                        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "fax ip %s error\n", subchild->val);
                    } else {
                        strcpy(fax_infos[fax_num].ip, subchild->val);
                    }
                } else if ((!strcmp(subchild->name, "port"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "fax port: %s", subchild->val);
                    fax_infos[fax_num].port = atoi(subchild->val);
                }
            }
        }
        fax_num += 1;
    }
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "fax num: %u", fax_num);

    return 0;
}


int get_cdr_ip_port(void)
{
    char *value = NULL;

    memset(cdr_ip, 0, sizeof(cdr_ip));
    cdr_port = 0;
    if (ConfGet("cdr.ip", &value) == 1) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "cdr ip: %s\n", value);
        if (strlen(value) >= 16) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "cdr ip error\n");
        } else {
            strcpy(cdr_ip, value);
        }
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "no cdr ip in vpu config file!\n");
    }

    if (ConfGet("cdr.vpuport", &value) == 1) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "cdr port: %s\n", value);
        cdr_port = atoi(value);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "no cdr port in svm config file!\n");
    }

    return 0;
}



int reload_cdr_ip_port(void)
{
    char *value = NULL;
    char r_cdr_ip[16];
    uint32_t r_cdr_port;

    memset(r_cdr_ip, 0, sizeof(r_cdr_ip));
    r_cdr_port = 0;
    if (ConfGet("cdr.ip", &value) == 1) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "reload cdr ip: %s\n", value);
        if (strlen(value) >= 16) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload cdr ip error\n");
        } else {
            strcpy(r_cdr_ip, value);
        }
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload no cdr ip in vpu config file!\n");
    }

    if (ConfGet("cdr.vpuport", &value) == 1) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "cdr port: %s\n", value);
        r_cdr_port = atoi(value);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "no cdr port in svm config file!\n");
    }

    if (strcmp(r_cdr_ip, cdr_ip) != 0 || r_cdr_port != cdr_port) {
        strcpy(cdr_ip, r_cdr_ip);
        cdr_port = r_cdr_port;
        svm_cdr_reload = 1;
    }

    return 0;
}






int get_storage_time(void)
{
    char *value = NULL;

    if (ConfGet("storage-time", &value) == 1) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "storage time: %s", value);
        storage_time = atoi(value);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "no storage time in vpu config file!");
    }
    if (storage_time <= 0 || storage_time > 1024) {
        storage_time = 90;
    }

    return 0;
}


int reload_storage_time(void)
{
    char *value = NULL;

    if (ConfGet("storage-time", &value) == 1) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "storage time: %s\n", value);
        storage_time = atoi(value);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "no storage time in vpu config file!\n");
    }
    if (storage_time <= 0 || storage_time > 1024) {
        storage_time = 90;
    }

    return 0;
}


int get_disk_left(void)
{
    char *value = NULL;

    if (ConfGet("disk-left", &value) == 1) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "disk left: %s\n", value);
        disk_left = atoi(value);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "no disk left in vpu config file!\n");
    }
    if (disk_left <= 0 || disk_left > 1024) {
        disk_left = 5;
    }

    return 0;
}


int reload_disk_left(void)
{
    char *value = NULL;

    if (ConfGet("disk-left", &value) == 1) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "disk left: %s\n", value);
        disk_left = atoi(value);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "no disk left in vpu config file!\n");
    }
    if (disk_left <= 0 || disk_left > 1024) {
        disk_left = 5;
    }

    return 0;
}




int get_server_ip_port(void)
{
    ConfNode *base;
    ConfNode *child;
    int id;
    uint32_t mac[6] = {0};
    uint8_t id_flag;

    base = ConfGetNode("vpu");
    if (base == NULL) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "vpu not get file dirs from config file");
        return -1;
    }

    memset(server_ip, 0, sizeof(server_ip));
    server_port = 0;
    id_flag = 0;
    TAILQ_FOREACH(child, &base->head, next) {
        if (!strcmp(child->val, "id")) {
            ConfNode *subchild;
            TAILQ_FOREACH(subchild, &child->head, next) {
                if ((!strcmp(subchild->name, "id"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "vpu id: %s", subchild->val);
                    id = atoi(subchild->val);
                    if (id == 0 || id != vpu_id) {
                        break;
                    }
                    id_flag = 1;
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "id match");
                } else if ((!strcmp(subchild->name, "mac"))) {
                    sscanf(subchild->val, "%02x-%02x-%02x-%02x-%02x-%02x", 
                            &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "vpu local mac: %02x-%02x-%02x-%02x-%02x-%02x", vpu_conf.dev_mac[0], vpu_conf.dev_mac[1], vpu_conf.dev_mac[2], vpu_conf.dev_mac[3], vpu_conf.dev_mac[4], vpu_conf.dev_mac[5]);
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "get mac: %02x-%02x-%02x-%02x-%02x-%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                    if (mac[0] != vpu_conf.dev_mac[0] || mac[1] != vpu_conf.dev_mac[1] 
                            || mac[2] != vpu_conf.dev_mac[2] || mac[3] != vpu_conf.dev_mac[3] 
                            || mac[4] != vpu_conf.dev_mac[4] || mac[5] != vpu_conf.dev_mac[5]) {
                        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "vpu mac not matching, vpu exit");
                        exit(-1);
                    }
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "mac match");

                } else if ((!strcmp(subchild->name, "ip"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "vpu ip: %s", subchild->val);

                    if (strlen(subchild->val) >= 16) {
                        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "vpu ip %s error\n", subchild->val);
                    } else {
                        strcpy(server_ip, subchild->val);
                    }
                } else if ((!strcmp(subchild->name, "port"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "vpu port: %s", subchild->val);
                    server_port = atoi(subchild->val);
                }
            }
        }
    }

    if (id_flag != 1) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "not vpu id match, vpu exit");
        exit(-1);
    }

    return 0;
}



int reload_server_ip_port(void)
{
    ConfNode *base;
    ConfNode *child;
    int id;
    uint32_t mac[6] = {0};
    uint8_t id_flag;

    char r_server_ip[16];
    uint32_t r_server_port;

    base = ConfGetNode("vpu");
    if (base == NULL) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "vpu not get file dirs from config file");
        return -1;
    }

    memset(r_server_ip, 0, sizeof(r_server_ip));
    r_server_port = 0;
    id_flag = 0;
    TAILQ_FOREACH(child, &base->head, next) {
        if (!strcmp(child->val, "id")) {
            ConfNode *subchild;
            TAILQ_FOREACH(subchild, &child->head, next) {
                if ((!strcmp(subchild->name, "id"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "vpu id: %s", subchild->val);
                    id = atoi(subchild->val);
                    if (id == 0 || id != vpu_id) {
                        break;
                    }
                    id_flag = 1;
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "id match");
                } else if ((!strcmp(subchild->name, "mac"))) {
                    sscanf(subchild->val, "%02x-%02x-%02x-%02x-%02x-%02x", 
                            &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "vpu local mac: %02x-%02x-%02x-%02x-%02x-%02x", vpu_conf.dev_mac[0], vpu_conf.dev_mac[1], vpu_conf.dev_mac[2], vpu_conf.dev_mac[3], vpu_conf.dev_mac[4], vpu_conf.dev_mac[5]);
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "get mac: %02x-%02x-%02x-%02x-%02x-%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                    if (mac[0] != vpu_conf.dev_mac[0] || mac[1] != vpu_conf.dev_mac[1] 
                            || mac[2] != vpu_conf.dev_mac[2] || mac[3] != vpu_conf.dev_mac[3] 
                            || mac[4] != vpu_conf.dev_mac[4] || mac[5] != vpu_conf.dev_mac[5]) {
                        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload svm.yaml mac not matching");
                        break;
                    }
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "mac match");

                } else if ((!strcmp(subchild->name, "ip"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "vpu ip: %s", subchild->val);

                    if (strlen(subchild->val) >= 16) {
                        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "vpu ip %s error\n", subchild->val);
                    } else {
                        strcpy(r_server_ip, subchild->val);
                    }
                } else if ((!strcmp(subchild->name, "port"))) {
                    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "vpu port: %s", subchild->val);
                    r_server_port = atoi(subchild->val);
                }
            }
        }
    }

    if (id_flag != 1) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload svm.yaml not vpu id match");
        return -1;
    }

    if (strcmp(r_server_ip, server_ip) != 0 || r_server_port != server_port) {
        strcpy(server_ip, r_server_ip);
        server_port = r_server_port;
        svm_vpu_reload = 1;
    }

    return 0;
}





int get_vpu_conf(void)
{
    char dump_config = 0;
    char filename[256] = {0};

    ConfInit();

    snprintf(filename, 255, "%s%s", vpu_conf.conf_file, "vpu.yaml");
    if (ConfYamlLoadFile(filename) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "load vpu config file %s error\n", vpu_conf.conf_file);
        /* Error already displayed. */
        exit(EXIT_FAILURE);
    }

    if (dump_config) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_BASE, "Dump all config variable:\n");
        ConfDump();
    }

    get_vpu_id();
    dev_parser();
    get_storage_time();
    get_disk_left();
    file_dirs_parser();
    get_log_para();

    ConfDeInit();

    return 0;
}



int reload_vpu_conf(char *cfg_file)
{
    char dump_config = 0;
    char filename[256] = {0};

    if (cfg_file == NULL) {
        return -1;
    }

    ConfInit();

    snprintf(filename, 255, "%s%s", vpu_conf.conf_file, cfg_file);
    if (ConfYamlLoadFile(filename) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload vpu config file %s error\n", cfg_file);
        return -1;
    }

    if (dump_config) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_BASE, "Dump all config variable:\n");
        ConfDump();
    }

    reload_vpu_id();
    reload_dev_parser();
    reload_storage_time();
    reload_disk_left();
    reload_file_dirs();
    get_log_para();

    ConfDeInit();

    return 0;
}




int get_svm_conf(void)
{
    char dump_config = 0;
    char filename[256] = {0};

    ConfInit();

    snprintf(filename, 255, "%s%s", vpu_conf.svm_conf_file, "svm.yaml");
    if (ConfYamlLoadFile(filename) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "load svm config file %s error\n", vpu_conf.svm_conf_file);
        /* Error already displayed. */
        exit(EXIT_FAILURE);
    }

    if (dump_config) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_BASE, "Dump all config variable:\n");
        ConfDump();
    }

    get_cdr_ip_port();
    get_fax_ip_port();
    get_server_ip_port();

    ConfDeInit();

    return 0;
}



int reload_svm_conf(char *cfg_file)
{
    char dump_config = 0;
    char filename[256] = {0};

    if (cfg_file == NULL) {
        return -1;
    }

    ConfInit();

    snprintf(filename, 255, "%s%s", vpu_conf.svm_conf_file, cfg_file);
    if (ConfYamlLoadFile(filename) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload svm config file %s error\n", cfg_file);
        /* Error already displayed. */
        return -1;
    }

    if (dump_config) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_BASE, "Dump all config variable:\n");
        ConfDump();
    }

    reload_cdr_ip_port();
    reload_fax_ip_port();
    reload_server_ip_port();

    ConfDeInit();

    return 0;
}



int get_sguard_port(void)
{
    char *value;

    vpu_conf.port_sguard = 2000;
    if (ConfGet("ma.sguardport", &value) == 1) {
        vpu_conf.port_sguard = atoi(value);
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "sguard server port: %d\n", vpu_conf.port_sguard);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "get sguard server port failed\n");
    }

    strncpy(vpu_conf.ip_sguard, SERV_IP, 15);

    return 0;
}


int reload_sguard_port(void)
{
    char *value;
    uint16_t port = 0;

    if (ConfGet("ma.sguardport", &value) == 1) {
        port = atoi(value);
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "reload sguard server port: %d\n", port);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload sguard server port failed\n");
        return -1;
    }

    if (port != 0 && port != vpu_conf.port_sguard) {
        dms_sguard_reload = 1;
    }
    return 0;
}




int get_ma_port(void)
{
    char *value;

    vpu_conf.port_ma = 2001;
    if (ConfGet("ma.maport", &value) == 1) {
        vpu_conf.port_ma = atoi(value);
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "ma server port: %d\n", vpu_conf.port_ma);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "get ma server port failed\n");
    }

    strncpy(vpu_conf.ip_ma, SERV_IP, 15);

    return 0;
}


int reload_ma_port(void)
{
    char *value;
    uint16_t port = 0;

    if (ConfGet("ma.maport", &value) == 1) {
        port = atoi(value);
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "reload ma server port: %d\n", port);
    } else {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload ma server port failed\n");
        return -1;
    }

    if (port != 0 && port != vpu_conf.port_ma) {
        dms_ma_reload = 1;
    }
    return 0;
}






int get_dms_conf(void)
{
    char dump_config = 0;
    char filename[256] = {0};

    ConfInit();

    snprintf(filename, 255, "%s%s", vpu_conf.dms_conf_file, "dms.yaml");
    if (ConfYamlLoadFile(filename) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "load dms config file %s error\n", vpu_conf.dms_conf_file);
        /* Error already displayed. */
        exit(EXIT_FAILURE);
    }

    if (dump_config) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_BASE, "Dump all config variable:\n");
        ConfDump();
    }

    get_sguard_port();
    get_ma_port();

    ConfDeInit();

    return 0;
}



int reload_dms_conf(char *cfg_file)
{
    char dump_config = 0;
    char filename[256] = {0};

    if (cfg_file == NULL) {
        return -1;
    }

    ConfInit();

    snprintf(filename, 255, "%s%s", vpu_conf.dms_conf_file, cfg_file);
    if (ConfYamlLoadFile(filename) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "reload dms config file %s error\n", cfg_file);
        return -1;
    }

    if (dump_config) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_BASE, "Dump all config variable:\n");
        ConfDump();
    }


    reload_sguard_port();
    reload_ma_port();

    ConfDeInit();

    return 0;
}




int sn_parser(int argc, char *argv[])
{
    int len;
    int i;
    char *sn;

    if (argc < 1) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "no sn value\n");
        return 0;
    }

    sn = argv[0];
    len = strlen(sn);
    for (i = 0; i < len; i++) {
        if (0 == isdigit(sn[i])) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "error sn value\n");
            vpu_conf.sn = 0;
            return -1;
        }
    }

    vpu_conf.sn = atoi(sn);
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "app sn is %d\n", vpu_conf.sn);

    return 1;
}



int config_parser(int argc, char *argv[])
{
    int len;

    if (argc < 1) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "no config value\n");
        return 0;
    }

    len = strlen(argv[0]);
    if (len >= 128) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "config filename %s to long\n", argv[0]);
        return 0;
    }

    strcpy(vpu_conf.conf_file, argv[0]);

    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "conf file is %s\n", vpu_conf.conf_file);

    return 1;
}






int param_parser(int argc, char *argv[])
{
    int n;
    int ret;

    memset(&vpu_conf, 0, sizeof(vpu_param_t));
    strcpy(vpu_conf.app_name, VPU_NAME);
    strcpy(vpu_conf.conf_file, VPU_FILE);
    strcpy(vpu_conf.svm_conf_file, SVM_FILE);
    strcpy(vpu_conf.dms_conf_file, DMS_FILE);
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_BASE, "app name: %s\n", argv[0]);

    if (strlen(argv[0]) >= 64 || argc < 3) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "argc: %d, app name: %s\n", argc, argv[0]);
        return 0;
    }

    n = 1;
    while (n < argc) {
        if (strcmp(argv[n], "--sn") == 0) {
            n++;
            ret = sn_parser(argc - n, argv + n);
            if (ret <= 0) {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "sn_parser fail\n");
                return -1;
            }
            n += ret;
        } else if (strcmp(argv[n], "--config") == 0) {
            n++;
            n += config_parser(argc - n, argv + n);
        } else {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_BASE, "%dth param %s error\n", n, argv[n]);
            n++;
        }
    }

    return 0;
}




