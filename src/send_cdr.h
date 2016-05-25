#ifndef __SEND_CDR__
#define __SEND_CDR__


typedef struct STRUCT_CDR_LIST_
{
	STRUCT_CDR cdr_buf[65536];
	pthread_mutex_t lock;
	unsigned short  ack;
	unsigned short  send;
	unsigned short  wait;
}STRUCT_CDR_LIST;



void cdr_insert(STRUCT_CDR *signal);
void *conn_cdr_thread(void *arg);

#endif
