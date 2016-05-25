#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <assert.h>
#include "conf.h"
#include "fax_queue.h"
#include "vpu.h"

PTH_STRUCT queue;


void *INIT_QUEUE(QUEUE**  eq, QUEUE** fq, int num)
{
    STRUCT_CDR_PATH  *p = NULL;
    STRUCT_CDR_PATH  *q = NULL;

    *eq = malloc(sizeof(QUEUE));
    if((*eq) == NULL)
    {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "eq malloc is error!");
        return NULL;
    }
    else
    {
    	(*eq)->phead = NULL;
        (*eq)->len = 0;
        pthread_mutex_init(&((*eq)->mutex), NULL);
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "eq malloc is %p", *eq);
    }

    (*fq) = malloc(sizeof(QUEUE));
    if((*fq) == NULL)
    {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "fq malloc is error!\n");
        return NULL;
    }
    else
    {
		(*fq)->phead = NULL;
        (*fq)->len = 0;
        pthread_mutex_init(&((*fq)->mutex), NULL);
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "fq malloc is %p", *fq);
    }

	do
	{
		q = malloc(sizeof(STRUCT_CDR_PATH));
		if(q != NULL)
		{
			q->next = NULL;
			if((*eq)->phead == NULL)
			{
				(*eq)->phead = q;
				p = (*eq)->phead;
			}
			else
			{
				p->next = q;
				p = q;
			}
			(*eq)->len ++;
		}
	}while((*eq)->len < num);
	return NULL;
}

int EN_QUEUE(QUEUE* queue, STRUCT_CDR_PATH* tmp)
{
	assert(tmp != NULL);
	assert(queue != NULL);

    int i = 0;
    STRUCT_CDR_PATH *p = NULL;
    pthread_mutex_lock(&(queue->mutex));

#if 0
    if (tmp->next != NULL) {
        applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_FAX, "EN_QUEUE queue %p tmp next is NULL", queue);
    }
    if (queue->phead == NULL) {
        queue->phead = tmp;
    } else {
        i = 1;
        for (p = queue->phead; p->next != NULL; p = p->next) {
            i++;
        }
        p->next = tmp;
        if (queue->len != i) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "===========error===========queue: %p, len: %d, i: %d", queue, queue->len, i);
        } else {
            //applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "+++++++++++right++++++++++queue: %p, len: %d, i: %d", queue, queue->len, i);
        }
    }
    queue->len += 1;
#else

    p = queue->phead;
    for(i = 1; i < queue->len; i++)
    {
        if (p == NULL) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_FAX, "err! EN_QUEUE p is NULL, queue: %p, len: %d, i: %d", queue, queue->len, i);
        }
        p = p->next;
    }

	if(p == NULL)
	{
		queue->phead = tmp;
	}
	else
	{
		p->next = tmp;
	}
    queue->len ++;
#endif

    pthread_mutex_unlock(&(queue->mutex));
    return 0;
}

void *DESTROY_QUEUE(QUEUE* queue)
{
	assert(queue != NULL);

    int i = 0;
    STRUCT_CDR_PATH *q = NULL;
    STRUCT_CDR_PATH *p = queue->phead;
	
    for(i = 0; i < queue->len; i++)
    {
        q = p->next;
        free(p);
        p = q;
    }

    pthread_mutex_destroy(&queue->mutex);
    queue->len = 0;
    free(queue);
	queue = NULL;
    return  NULL;
}

STRUCT_CDR_PATH  *FETCH_PHEAD(QUEUE* queue)
{
	assert(queue != NULL);
	
    STRUCT_CDR_PATH *p = NULL;
    pthread_mutex_lock(&(queue->mutex));
#if 0
    p = queue->phead;
    if (queue->phead != NULL) {
        queue->phead = p->next;
        p->next = NULL;
        queue->len -= 1;
    }
    pthread_mutex_unlock(&(queue->mutex));
#else
    p = queue->phead;
    if(p == NULL)
    {
        pthread_mutex_unlock(&(queue->mutex));
        queue->len = 0;
        return NULL;
    }
    else
    {
        queue->phead = p->next;
        p->next = NULL;
        queue->len --;
        pthread_mutex_unlock(&(queue->mutex));
    }
#endif

    return p;
}

