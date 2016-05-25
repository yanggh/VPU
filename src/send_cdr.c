#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include "vpu.h"
#include "conf.h"
#include "fax_queue.h"
#include "send_cdr.h"


STRUCT_CDR_LIST g_cdr_buff;

int connect2cdr(void)
{
    int sockfd;
    struct sockaddr_in servaddr;
    struct timeval timeo = {3, 0};
    socklen_t len = sizeof(timeo);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_CDR, "create socket to cdr fail");
        return -1;
    }

    //set the timeout period
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeo, len);

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(cdr_port);
    if(inet_pton(AF_INET, cdr_ip, &servaddr.sin_addr) != 1){  
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_CDR, "inet_pton error for %s", cdr_ip);  
        close(sockfd);
        return -1; 
    }

    if(connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0){
        if (errno == EINPROGRESS) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_CDR, "connect to cdr timeout");
        }
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_CDR, "connect to cdr error");
        close(sockfd);
        return -1;
    }
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_CDR, "connect to cdr(ip: %s, port: %d) success", 
            cdr_ip, cdr_port);

    return sockfd;
}


void cdr_init(void)
{
    if (pthread_mutex_init(&g_cdr_buff.lock, NULL) != 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_CDR, "error initializing cdr mutex");
        exit(-1);
    }
    
    g_cdr_buff.ack = 0;
    g_cdr_buff.send = 1;
    g_cdr_buff.wait = 1;
    memset(g_cdr_buff.cdr_buf, 0, sizeof(STRUCT_CDR)*65536);
}


void cdr_insert(STRUCT_CDR *signal)
{
	pthread_mutex_lock(&g_cdr_buff.lock);
	if (g_cdr_buff.wait == g_cdr_buff.ack)
	{
		g_cdr_buff.ack++;
		if (g_cdr_buff.ack == g_cdr_buff.send)
		{
			g_cdr_buff.send++;
		}
	}
	g_cdr_buff.cdr_buf[g_cdr_buff.wait].callid = signal->callid;
	g_cdr_buff.cdr_buf[g_cdr_buff.wait].time = signal->time;
	g_cdr_buff.cdr_buf[g_cdr_buff.wait].path = signal->path;
	g_cdr_buff.cdr_buf[g_cdr_buff.wait].callflag = signal->callflag;
	g_cdr_buff.cdr_buf[g_cdr_buff.wait].ruleflag = signal->ruleflag; 
	g_cdr_buff.cdr_buf[g_cdr_buff.wait].recv3 = signal->recv3; 
	g_cdr_buff.wait++;

    //applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_CDR, "cdr insert %lu %lu %u %u", signal->callid, signal->time, signal->path, signal->callflag);

	pthread_mutex_unlock(&g_cdr_buff.lock);
}

int sock_send_buf(int fd, unsigned char *buf, int size)
{
    int len = 0;
    int ret;

    while (len < size)
    {   
        ret = send(fd, &buf[len], size-len, 0); 
        if (ret<0)
            return -1; 
        len+=ret;
    }   
    return 0;
}


int sock_recv_buf(int fd, unsigned char *buf, int size)
{
    int ret;
    int len = 0;

    while (len < size)
    {   
        ret = recv(fd, &buf[len], (size-len), 0); 
        if (ret <= 0)
            return -1; 
        len += ret;
    }   
    return 0;
}

static int cdr_send(int fd)
{
	unsigned short num;
	unsigned char buf[6];
	STRUCT_CDR cdr_arr[16];
	int size;
	int ret;
	unsigned short send_len;

	pthread_mutex_lock(&g_cdr_buff.lock);
	if (g_cdr_buff.send == g_cdr_buff.wait)
	{
		pthread_mutex_unlock(&g_cdr_buff.lock);
		return 0;
	}
	else if (g_cdr_buff.send > g_cdr_buff.wait)
	{
		num = 65536-g_cdr_buff.send;
	}
	else
	{
		num = g_cdr_buff.wait - g_cdr_buff.send;
	}

	num = num>16?16:num;
	size = num*sizeof(STRUCT_CDR);
	send_len = g_cdr_buff.send;
	memcpy(cdr_arr, &g_cdr_buff.cdr_buf[g_cdr_buff.send], size);
	g_cdr_buff.send += num;
	pthread_mutex_unlock(&g_cdr_buff.lock);

	buf[0] = 0;
	buf[1] = 4;
	buf[2] = send_len>>8;
	buf[3] = send_len&0xff;
	buf[4] = 0;
	buf[5] = num;

    ret = sock_send_buf(fd, buf, 6);
	if (-1 == ret) {
		return -1;
	}

    ret = sock_send_buf(fd, (unsigned char *)cdr_arr, size);
	if (-1 == ret) {
		return -1;
	}

	return 0;
}

static void cdr_update_ack(unsigned short ack)
{
	unsigned short send_num, ack_num;
	pthread_mutex_lock(&g_cdr_buff.lock);
	send_num = g_cdr_buff.send - 1 - g_cdr_buff.ack;
	ack_num = ack - g_cdr_buff.ack;
	if (ack_num <= send_num)
		g_cdr_buff.ack = ack;
	pthread_mutex_unlock(&g_cdr_buff.lock);
}

static inline void cdr_reset_send()
{
	pthread_mutex_lock(&g_cdr_buff.lock);
	g_cdr_buff.send = g_cdr_buff.ack + 1;
	pthread_mutex_unlock(&g_cdr_buff.lock);
}





void *conn_cdr_thread(__attribute__((unused))void *arg)
{
    int sockfd;
    int ret;
	uint8_t cmd;
	uint8_t para[2];
    uint32_t conn_times;
    struct timeval tv;
    fd_set readfds;
    int nfds;
    uint32_t remain_len;
    uint32_t recv_len;
    uint8_t recv_data[4];

    cdr_init();

    sockfd = -1;
    conn_times = 0;

    remain_len = 4;
    recv_len = 0;
    while (1) {
        if (unlikely(svm_signal_flags & SVM_DONE)) {
            break;
        }

        if (unlikely(svm_cdr_reload == 1)) {
            close(sockfd);
            sockfd = -1;
            svm_cdr_reload = 0;
        }
        if (sockfd == -1) {
            if ((sockfd = connect2cdr()) == -1) {
                if (++conn_times >= 60) {
                    conn_times = 0;
                    applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_CDR, "connect to cdr failed");
                }
                sleep(1);
                continue;
            }
            cdr_reset_send();
        }

        tv.tv_sec = 0;
        tv.tv_usec = 1000;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        nfds = sockfd + 1;
        ret = select(nfds, &readfds, NULL, NULL, &tv);
        if (ret > 0) {
            if (remain_len == 0) {
                cmd = recv_data[1];
                if(cmd == 5) {
                    para[0] = recv_data[2];
                    para[1] = recv_data[3];
                    cdr_update_ack((para[0]<<8)+para[1]);
                }
                remain_len = 4;
                recv_len = 0;
            } else {
                ret = recv(sockfd, &recv_data[recv_len], remain_len, 0);
                if (ret <= 0) {
                    close(sockfd);
                    sockfd = -1;
                } else {
                    remain_len -= ret;
                    recv_len += ret;
                    if (remain_len == 0) {
                        cmd = recv_data[1];
                        if(cmd == 5) {
                            para[0] = recv_data[2];
                            para[1] = recv_data[3];
                            cdr_update_ack((para[0]<<8)+para[1]);
                        }
                        remain_len = 4;
                        recv_len = 0;
                    }
                }
            }
        } else if (ret < 0) {
            usleep(500);
        }

        if (sockfd != -1) {
            ret = cdr_send(sockfd);
            if (ret != 0) {
                close(sockfd);
                sockfd = -1;
            }
        }
    }
    close(sockfd);

    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_CDR, "pthread conn_cdr_thread exit");
    pthread_exit(NULL);
}
