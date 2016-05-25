#ifndef  __FAX_PRE__
#define  __FAX_PRE__
void *fax_pre(void* arg);
void init_connect();
#endif
#if 0
int main(int argc, char **argv)
{
	¦   pthread_t id;

	¦   PTH_STRUCT  *queue = (PTH_STRUCT*)malloc(sizeof(PTH_STRUCT));
	¦   if(queue != NULL)
		¦   {
			¦   ¦   queue->eq = NULL;
			¦   ¦   queue->fq = NULL;
			¦   }

	¦   INIT_QUEUE(&(queue->eq), &(queue->fq), QUEUE_NUM);

	¦   pthread_create(&id, NULL, fax_pre, queue);
	¦   pthread_join(id, NULL);
	¦   return 0;
}
#endif
