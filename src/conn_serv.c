#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "conf.h"
#include "conn_serv.h"
#include "vpu.h"
#include "counter.h"

#include "../../common/ma_pkt.h"
#include "../../common/file_func.h"



#define VPU_BUFSIZE     2048

static uint16_t sguard_num = 0;
//static uint16_t ma_num = 0;


int ma_sockfd;

uint16_t vpu_fill_info(uint8_t *pkt, uint8_t *info, uint16_t len, uint16_t type)
{
    uint16_t n_len;
    uint16_t n_type;

    if (pkt == NULL || info == NULL) {
        return 0;
    }

    n_type = htons(type);
    memcpy(pkt, &n_type, 2);
    n_len = htons(len);
    memcpy(pkt + 2, &n_len, 2);
    memcpy(pkt + 4, info, len);

    return len + 4;
}


uint32_t vpu_fill_regsguard_packet(uint8_t *pkt, uint16_t num, char *name)
{
    struct pkt_header *header;
    uint8_t *payload;
    uint16_t len;
    uint16_t payload_len;
    pid_t pid;
    uint32_t n_pid;
    uint32_t n_sn;
    uint32_t n_vpu_id;

    if (pkt == NULL || name == NULL) {
        return 0;
    }

    header = (struct pkt_header *)pkt;
    header->reply = 0;
    header->ver = 1;
    header->cmd = htons(16);
    header->num = htons(num);

    payload = pkt + sizeof(struct pkt_header) + 4;

    pid = getpid();
    n_pid = htonl(pid);
    len = 4;
    payload_len = vpu_fill_info(payload, (uint8_t *)&n_pid, len, 2);

    len = 4;
    n_sn = htonl(vpu_conf.sn);
    payload_len += vpu_fill_info(payload + payload_len, (uint8_t *)&n_sn, len, 3);

    n_vpu_id = htonl(vpu_id);
    len = 4;
    payload_len += vpu_fill_info(payload + payload_len, (uint8_t *)&n_vpu_id, len, 4);

    len = strlen(server_ip);
    payload_len += vpu_fill_info(payload + payload_len, (uint8_t *)server_ip, len, 5);
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_MA, "reg sguard ip %s", server_ip);

    payload = pkt + sizeof(struct pkt_header);
    *((uint16_t *)payload) = htons(1);
    *((uint16_t *)(payload + 2)) = htons(payload_len);

    header->len = htons(payload_len + 4 + sizeof(struct pkt_header));

    return payload_len + 4 + sizeof(struct pkt_header);
}


uint8_t get_status(void)
{
    uint8_t status;

    status = 1;

    return status;
}


uint32_t vpu_fill_alive_packet(uint8_t *pkt, uint16_t num)
{
    struct pkt_header *header;
    uint8_t *payload;
    uint16_t len;
    uint16_t payload_len;
    uint16_t status;

    if (pkt == NULL) {
        return 0;
    }

    header = (struct pkt_header *)pkt;
    header->reply = 0;
    header->ver = 1;
    header->cmd = htons(5);
    header->num = htons(num);

    payload = pkt + sizeof(struct pkt_header) + 4;

    status = get_status();
    len = sizeof(status);
    status = htons(status);
    payload_len = vpu_fill_info(payload, (uint8_t *)&status, len, 1);

    payload = pkt + sizeof(struct pkt_header);
    *((uint16_t *)payload) = htons(4);
    *((uint16_t *)(payload + 2)) = htons(payload_len);

    header->len = htons(payload_len + 4 + sizeof(struct pkt_header));

    return payload_len + 4 + sizeof(struct pkt_header);
}



uint32_t vpu_fill_regma_packet(uint8_t *pkt, uint16_t num)
{
    struct pkt_header *header;
    uint8_t *payload;
    uint16_t len;
    uint16_t payload_len;
    pid_t pid;
    uint32_t n_pid;
    uint32_t n_sn;

    if (pkt == NULL) {
        return 0;
    }

    //memset(pkt, 0, VPU_BUFSIZE);
    header = (struct pkt_header *)pkt;
    header->reply = 0;
    header->ver = 1;
    header->cmd = htons(16);
    header->num = htons(num);

    payload = pkt + sizeof(struct pkt_header) + 4;
    payload_len = 0;

    pid = getpid();
    n_pid = htonl(pid);
    len = 4;
    payload_len = vpu_fill_info(payload + payload_len, (uint8_t *)&n_pid, len, 1);

    len = 4;
    n_sn = htonl(vpu_conf.sn);
    payload_len += vpu_fill_info(payload + payload_len, (uint8_t *)&n_sn, len, 2);

    payload = pkt + sizeof(struct pkt_header);
    *((uint16_t *)payload) = htons(2);
    *((uint16_t *)(payload + 2)) = htons(payload_len);

    header->len = htons(payload_len + 4 + sizeof(struct pkt_header));

    return payload_len + 4 + sizeof(struct pkt_header);
}


int connect2sguard(void)
{
    int sockfd;
    struct sockaddr_in servaddr;
    struct timeval timeo = {3, 0};
    socklen_t len = sizeof(timeo);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "create socket to sguard fail");
        return -1;
    }

    //set the timeout period
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeo, len);

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(vpu_conf.port_sguard);
    if(inet_pton(AF_INET, vpu_conf.ip_sguard, &servaddr.sin_addr) != 1){  
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "inet_pton error for %s", vpu_conf.ip_sguard);  
        close(sockfd);
        return -1; 
    }

    if(connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0){
        if (errno == EINPROGRESS) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "connect to sguard timeout");
        }
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "connect to sguard error");
        close(sockfd);
        return -1;
    }
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_MA, "connect to sguard(ip: %s, port: %d) success", 
            vpu_conf.ip_sguard, vpu_conf.port_sguard);

    return sockfd;
}


void *conn_sguard(__attribute__((unused))void *arg)
{
    int sockfd;
    uint8_t pkt[VPU_BUFSIZE];
    uint32_t pkt_len;
    int ret;
    uint32_t conn_times;

    if (vpu_conf.port_sguard == 0 || vpu_conf.sn == 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_MA, "sguard port: %d, ma port: %d, sn: %d", 
                vpu_conf.port_sguard, vpu_conf.port_ma, vpu_conf.sn);
        return NULL;
    }

    sockfd = -1;
    conn_times = 0;
    while (1) {
        if (unlikely(svm_signal_flags & SVM_DONE)) {
            break;
        }

        if (unlikely(dms_sguard_reload == 1)) {
            close(sockfd);
            sockfd = -1;
            dms_sguard_reload = 0;
        }

        if (sockfd == -1) {
            if ((sockfd = connect2sguard()) == -1) {
                if (++conn_times >= 60) {
                    conn_times = 0;
                    applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_MA, "connect to sguard failed");
                }
                sleep(1);
                continue;
            }

            pkt_len = vpu_fill_regsguard_packet(pkt, sguard_num++, vpu_conf.app_name);
            write(sockfd, pkt, pkt_len);
            applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_MA, "send regist info %d bytes to sguard\n", 
                    pkt_len);
        }

        pkt_len = vpu_fill_alive_packet(pkt, sguard_num++);
        ret = write(sockfd, pkt, pkt_len);
        if (ret <= 0) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_MA, "write sguard socket return %d\n", ret);
            close(sockfd);
            sockfd = -1;
            continue;
        }
        sleep(2);
    }

    close(sockfd);
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_MA, "pthread conn_sguard exit\n");

    pthread_exit(NULL);
}



int ma_cmd_reload(uint8_t *data, uint32_t data_len)
{
    uint16_t type;
    uint16_t length;
    char filename[255];
    uint32_t left;

    if (data == NULL || data_len == 0) {
        return 0;
    }

    left = data_len;
    while (left >= 4) {
        type = ntohs(*(uint16_t *)data);
        length = ntohs(*(uint16_t *)(data + 2));
        data += 4;
        left -= 4;
        if (left < length || length > 255 || length <= 8) {
            return -1;
        }
        if (type == 1) {
            memcpy(filename, data, length);
            filename[length] = '\0';
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "reload config file %s", filename);
            if (memcmp(filename, "svm.yaml", 8) == 0) {
                reload_svm_conf(filename);
            } else if (memcmp(filename, "dms.yaml", 8) == 0) {
                reload_dms_conf(filename);
            } else if (memcmp(filename, "vpu.yaml", 8) == 0) {
                reload_vpu_conf(filename);
            } else {
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_MA, "reload config file %s error", filename);
            }
        } else {
            return -1;
        }
        data += length;
        left += length;
    }

    return 0;
}



int ma_cmd_get_counter(int sockfd, uint16_t sn)
{
    uint8_t pkt_buf[1500];
    uint16_t pkt_len;
    struct pkt_header *header;
    uint8_t *payload;
    uint16_t payload_len;
    uint16_t version;
    time_t count_time;
    all_count_t total_count;
    int ret;

    memset(&total_count, 0, sizeof(all_count_t));
    total_count.pkts = atomic64_read(&all_pkts);
    total_count.lost_pkts = atomic64_read(&lost_pkts);
    total_count.voice_pkts = atomic64_read(&voice_pkts);
    total_count.other_pkts = atomic64_read(&other_pkts);
    total_count.call_start = atomic64_read(&call_start);
    total_count.call_end = atomic64_read(&call_end);
    total_count.call_timeout = atomic64_read(&call_timeout);
    total_count.file_ok = atomic64_read(&file_ok);
    total_count.file_fail = atomic64_read(&file_fail);
    total_count.fax_lost = atomic64_read(&fax_lost);
    total_count.fax_enqueue = atomic64_read(&fax_enqueue);
    total_count.fax_dequeue = atomic64_read(&fax_dequeue);
    total_count.fax_yes = atomic64_read(&fax_yes);
    total_count.fax_no = atomic64_read(&fax_no);


    header = (struct pkt_header *)pkt_buf;
    header->reply = 1;
    header->ver = 1;
    header->cmd = htons(19);
    header->num = htons(sn);
    pkt_len = sizeof(struct pkt_header);
    payload = pkt_buf + pkt_len;

    version = htons(1);
    payload_len = vpu_fill_info(payload, (uint8_t *)&version, 2, 2);
    pkt_len += payload_len;
    payload = payload + payload_len;

    count_time = time(NULL);
    count_time = htobe64(count_time);
    payload_len = vpu_fill_info(payload, (uint8_t *)&count_time, sizeof(time_t), 1000);
    pkt_len += payload_len;
    payload = payload + payload_len;

#if 1
    applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "pkts: %lu, lost_pkts: %lu, voice_pkts: %lu, other_pkts: %lu", 
            total_count.pkts, total_count.lost_pkts, total_count.voice_pkts, total_count.other_pkts);
    applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "call_start: %lu, call_end: %lu, call_timeout: %lu", 
            total_count.call_start, total_count.call_end, total_count.call_timeout);
    applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "open file ok: %lu, open file fail: %lu", 
            total_count.file_ok, total_count.file_fail);
    applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "fax_enqueue: %lu, fax_lost: %lu, fax_dequeue: %lu, fax_yes: %lu, fax_no: %lu", 
            total_count.fax_enqueue, total_count.fax_lost, total_count.fax_dequeue, total_count.fax_yes, total_count.fax_no);

    applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "call online: %lu", atomic64_read(&call_online));
#endif

    total_count.pkts = htobe64(total_count.pkts);
    total_count.lost_pkts = htobe64(total_count.lost_pkts);
    total_count.voice_pkts = htobe64(total_count.voice_pkts);
    total_count.other_pkts = htobe64(total_count.other_pkts);
    total_count.call_start = htobe64(total_count.call_start);
    total_count.call_end = htobe64(total_count.call_end);
    total_count.call_timeout = htobe64(total_count.call_timeout);
    total_count.file_ok = htobe64(total_count.file_ok);
    total_count.file_fail = htobe64(total_count.file_fail);
    total_count.fax_enqueue = htobe64(total_count.fax_enqueue);
    total_count.fax_lost = htobe64(total_count.fax_lost);
    total_count.fax_dequeue = htobe64(total_count.fax_dequeue);
    total_count.fax_yes = htobe64(total_count.fax_yes);
    total_count.fax_no = htobe64(total_count.fax_no);

    pkt_len += vpu_fill_info(payload, (uint8_t *)&total_count, sizeof(all_count_t), 3);

    header->len = htons(pkt_len);
    ret = write(sockfd, pkt_buf, pkt_len);
    if (ret != pkt_len) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "write counter data %dbytes less than pkt_len %dbytes", ret, pkt_len);
    }

    return 0;
}


int ma_cmd_software_status(uint8_t *data, uint32_t data_len)
{
    uint16_t type;
    uint16_t length;
    uint32_t left;
    uint16_t sub_type;
    uint16_t sub_length;
    uint32_t sub_left;
    uint8_t *sub_data;
    uint8_t vpu_flag;
    uint32_t status = 0;
    uint32_t fax_id = 0;

    if (data == NULL || data_len < 4) {
        return 0;
    }

    vpu_flag = 0;
    left = data_len;
    while (left >= 4) {
        type = ntohs(*(uint16_t *)data);
        length = ntohs(*(uint16_t *)(data + 2));
        data += 4;
        left -= 4;
        if (left < length) {
            return -1;
        }
        if (type == 1) {
            if (memcmp(data, "vpu", 3) != 0) {
                return -2;
            }
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "software statut to vpu");
            vpu_flag = 1;
        } else if (type == 2 && vpu_flag == 1) {
            sub_data = data;
            sub_left = length;
            while (sub_left >= 4) {
                sub_type = ntohs(*(uint16_t *)sub_data);
                sub_length = ntohs(*(uint16_t *)(sub_data + 2));
                sub_data += 4;
                sub_left -= 4;
                if (sub_left < sub_length) {
                    return -1;
                }
                if (sub_type == 1 && sub_length == 4) {
                    status = ntohl(*(uint32_t *)sub_data);
                } else if (sub_type == 2) {
                } else if (sub_type == 3) {
                    if (memcmp(sub_data, "fpu", 3) != 0) {
                        return -2;
                    }
                    applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "software fpu statut to vpu");
                } else if (sub_type == 4 && sub_length == 4) {
                    fax_id = ntohl(*(uint32_t *)sub_data);
                    applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "software fpu %u statut to vpu", fax_id);
                } else {
                    return 0;
                }
                sub_data += sub_length;
                sub_left -= sub_length;
            }

            change_fax_status(fax_id, status);

        } else {
            return -1;
        }
        data += length;
        left += length;
    }


    return 0;
}

struct count_struct g_counter_field_arrays[14]={
	{"pkts_all", 8}, {"pkts_lost", 8}, {"pkts_voice", 8}, {"pkts_other", 8},
	{"call_start", 8}, {"call_end", 8}, {"call_timeout", 8}, {"file_ok", 8},
	{"file_fail", 8}, {"fax_lost", 8}, {"fax_enqueue", 8}, {"fax_dequeue", 8},
	{"fax_yes", 8}, {"fax_no", 8}, };

int ma_cmd_parser(uint8_t *pkt, uint32_t pkt_len, int sockfd)
{
    struct pkt_header *header;
    uint16_t header_len;
    uint8_t *data;
    uint32_t data_len;
    uint8_t *left;
    uint32_t left_len;
    uint16_t cmd;
    uint32_t v;
    uint8_t pkt_buf[32], pkt_big[2048];
    int ret;

    if (pkt == NULL) {
        return 0;
    }

    header_len = sizeof(struct pkt_header);
    left = pkt;
    left_len = pkt_len;
    while (left_len >= header_len) {
        header = (struct pkt_header *)left;
        data = left + header_len;
        data_len = ntohs(header->len) - header_len;
        header->num = ntohs(header->num);
        cmd = ntohs(header->cmd);
        switch (cmd) {
            case RELOAD:
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "get a ma reload config file cmd");
                ma_cmd_reload(data, data_len);
                break;
            case GET_COUNTER:
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "get a ma counter cmd");
                ma_cmd_get_counter(sockfd, header->num);
                break;
            case COUNTER_STRUCT:
                applog(LOG_DEBUG, APP_VPU_LOG_MASK_MA, "get counter struct cmd from ma");
                ret = fill_ma_count_info_struct(pkt_big, header->num, "vpu", "vpu",
                      sizeof(g_counter_field_arrays)/sizeof(struct count_struct), g_counter_field_arrays);
                sock_send_buf(sockfd, pkt_big, ret);
                break;
            case SOFTWARE_STATUS:
                applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "get a ma software status");
                ma_cmd_software_status(data, data_len);
                break;
            case DEBUG_ON:
                applog_debug_on(&ma_sockfd);
                break;
            case DEBUG_OFF:
                applog_debug_off();
                break;
            case SYSLOG_LEVEL:
                ma_cmd_get_loglevel(data, data_len, &v);
                applog_set_log_level(v);
                break;
            case DEBUG_MASK:
                ma_cmd_get_logmask(data, data_len, &v);
                applog_set_debug_mask(v);
                break;
            case SHOW_SYS_LEV:
                ret = fill_loglevel_packet(pkt_buf, header->num, g_app_log_level);
                sock_send_buf(sockfd, pkt_buf, ret);
                break;
            case SHOW_DEBUG_MASK:
                ret = fill_logmask_packet(pkt_buf, header->num, g_app_debug_mask);
                sock_send_buf(sockfd, pkt_buf, ret);
            default:
                applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_MA, "unknown cmd %d form ma", cmd);
                break;
        }

        left = left + header_len + data_len;
        left_len = left_len - header_len - data_len;
    }

    return 0;
}


int connect2ma(void)
{
    int sockfd;
    struct sockaddr_in servaddr;
    struct timeval timeo = {3, 0};
    socklen_t len = sizeof(timeo);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "create socket to ma fail");
        return -1;
    }

    //set the timeout period
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeo, len);

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(vpu_conf.port_ma);
    if(inet_pton(AF_INET, vpu_conf.ip_ma, &servaddr.sin_addr) != 1){  
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "inet_pton error for %s", vpu_conf.ip_ma);  
        close(sockfd);
        return -1; 
    }

    if(connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0){
        if (errno == EINPROGRESS) {
            applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "connect to ma timeout");
        }
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "connect to ma error");
        close(sockfd);
        return -1;
    }
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_MA, "connect to ma(ip: %s, port: %d) success", 
            vpu_conf.ip_ma, vpu_conf.port_ma);

    return sockfd;
}


void *conn_ma(__attribute__((unused))void *arg)
{
    uint8_t pkt[VPU_BUFSIZE];
    uint32_t pkt_len;
    int ret;
    struct timeval tv;
    fd_set readfds;
    int nfds;
    uint32_t conn_times;

    if (vpu_conf.port_ma == 0 || vpu_conf.sn == 0) {
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_MA, "ma port: %d, sn: %d", 
                vpu_conf.port_ma, vpu_conf.sn);
        return NULL;
    }

    ma_sockfd = -1;
    conn_times = 0;
    while (1) {
        if (unlikely(svm_signal_flags & SVM_DONE)) {
            break;
        }

        if (unlikely(dms_ma_reload == 1)) {
            close(ma_sockfd);
            ma_sockfd = -1;
            dms_ma_reload = 0;
        }
        if (ma_sockfd == -1) {
            if ((ma_sockfd = connect2ma()) == -1) {
                if (++conn_times >= 60) {
                    conn_times = 0;
                    applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_MA, "connect to ma failed");
                }
                sleep(1);
                continue;
            }
            pkt_len = vpu_fill_regma_packet(pkt, ma_num++);
            write(ma_sockfd, pkt, pkt_len);
            applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_MA, "send regist info %d bytes to ma", pkt_len);
        }

        tv.tv_sec = 1;
        tv.tv_usec = 0;
        FD_ZERO(&readfds);
        FD_SET(ma_sockfd, &readfds);
        nfds = ma_sockfd + 1;
        ret = select(nfds, &readfds, NULL, NULL, &tv);
        if (ret <= 0) {
            continue;
        }

        ret = read(ma_sockfd, pkt, VPU_BUFSIZE);
        if (ret <= 0) {
            applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_MA, "read ma socket return %d", ret);
            close(ma_sockfd);
            ma_sockfd = -1;
            continue;
        }
        applog(APP_LOG_LEVEL_DEBUG, APP_VPU_LOG_MASK_MA, "recv %d bytes from ma socket", ret);
        ma_cmd_parser(pkt, ret, ma_sockfd);
    }

    close(ma_sockfd);
    applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_MA, "pthread conn_ma exit");

    pthread_exit(NULL);
}
