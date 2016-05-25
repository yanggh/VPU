#ifndef __CONF_H__
#define __CONF_H__
#include "atomic.h"

//#define VPU_FILE    "/usr/local/etc/vpu/vpu.yaml"
//#define SVM_FILE    "/usr/local/etc/svm.yaml"
//#define DMS_FILE    "/usr/local/etc/dms.yaml"
#define VPU_NAME    "vpu"
#define VPU_FILE    "/usr/local/etc/vpu/"
#define SVM_FILE    "/usr/local/etc/"
#define DMS_FILE    "/usr/local/etc/"

#define SERV_IP         "127.0.0.1"

#define MAX_DIRS_NUM        16
#define MAX_DIRS_LEN        256


#define NORMAL_DIR  "normal"
#define CASE_DIR    "case"

#define FAX_NUM_MAX         32

typedef struct file_dirs_node_ {
    char dir_name[MAX_DIRS_LEN];
    uint8_t disk_num;
    uint8_t write_flag;
} file_dirs_node_t;

typedef struct file_dirs_info_ {
    file_dirs_node_t dirs[MAX_DIRS_NUM];
    //char dirs[MAX_DIRS_NUM][MAX_DIRS_LEN];
    uint32_t num;
} file_dirs_info_t;

typedef struct vpu_param_ {
    uint16_t sn;
    uint16_t port_sguard;
    uint16_t port_ma;
    char ip_sguard[16]; 
    char ip_ma[16];
    char dev[64];
    uint8_t dev_mac[6];
    char app_name[64];
    char conf_file[128];
    char svm_conf_file[128];
    char dms_conf_file[128];
} vpu_param_t;

typedef struct fax_info_ {
    uint32_t id;
    char ip[16];
    uint32_t port;
    int fd;
    uint8_t flag;
} fax_info_t;



extern vpu_param_t vpu_conf;
extern file_dirs_info_t file_dirs;
extern file_dirs_info_t r_file_dirs;

extern atomic32_t file_dir_no;

extern char cdr_ip[16];
extern uint16_t cdr_port;

extern char server_ip[16];
extern uint32_t server_port;

extern uint8_t path_num;

extern uint32_t vpu_id;

extern uint32_t storage_time;
extern uint32_t disk_left;



extern fax_info_t fax_infos[FAX_NUM_MAX];
extern uint32_t fax_num;

extern fax_info_t r_fax_infos[FAX_NUM_MAX];
extern uint32_t r_fax_num;


extern uint8_t svm_cdr_reload;
extern uint8_t svm_fax_reload;
extern uint8_t svm_vpu_reload;

extern uint8_t dms_sguard_reload;
extern uint8_t dms_ma_reload;

extern uint8_t vpu_dev_reload;
extern uint8_t vpu_dir_reload;




int param_parser(int argc, char *argv[]);
int get_vpu_conf(void);
int get_svm_conf(void);
int get_dms_conf(void);
char *get_dev(void);
void create_all_dirs(int year, int mon, int day);

int get_cdr_ip_and_port(char *ip, uint32_t len, uint32_t *port);

int change_fax_status(uint32_t fax_id, uint32_t status);


int reload_svm_conf(char *cfg_file);
int reload_dms_conf(char *cfg_file);
int reload_vpu_conf(char *cfg_file);

#endif
