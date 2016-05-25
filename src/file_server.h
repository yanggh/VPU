#ifndef __FILE_SERVER_H__
#define __FILE_SERVER_H__


typedef struct fax_pic_info_ {
    char name[MAX_DIRS_LEN];
    uint32_t size;
} fax_pic_info_t;


void *file_tx_thread(void *arg);
void *file_server_thread(void *arg);

#endif
