#ifndef __FAX_QUEUE_H__
#define __FAX_QUEUE_H__

#include <stdint.h>
#include <time.h>
#include <pthread.h>



#define  QUEUE_NUM     100000
#define FAX_BUF_SIZE    (512 * 1024)

typedef struct  STRUCT_CDR_
{
    uint64_t callid;
    time_t   time;
    unsigned short path;
    unsigned char callflag;
    unsigned char ruleflag;
    unsigned int  recv3;
}STRUCT_CDR;

typedef struct  STRUCT_CDR_PATH_
{
    STRUCT_CDR  cdr;
    char  buf_up[FAX_BUF_SIZE];
    char  buf_down[FAX_BUF_SIZE];
    struct STRUCT_CDR_PATH_ *next;
}STRUCT_CDR_PATH;

typedef struct QUEUE_
{
    STRUCT_CDR_PATH  *phead;
    pthread_mutex_t  mutex;
    int              len;
}QUEUE;

typedef struct PTH_STRUCT_
{
    QUEUE  *eq;
    QUEUE  *fq;
    int    flag;
}PTH_STRUCT;



extern PTH_STRUCT queue;


void *INIT_QUEUE(QUEUE**  eq, QUEUE** fq, int num);
int EN_QUEUE(QUEUE* queue, STRUCT_CDR_PATH* tmp);
void *DESTROY_QUEUE(QUEUE* queue);
STRUCT_CDR_PATH  *FETCH_PHEAD(QUEUE* queue);

int callid2filedir(STRUCT_CDR *cdr, char *buf, uint32_t buf_size);

#endif
