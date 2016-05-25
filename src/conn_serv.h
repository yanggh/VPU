#ifndef __CONN_SERV_H__
#define __CONN_SERV_H__



#define RELOAD          23
#define GET_COUNTER     19
#define SOFTWARE_STATUS 14
#define DEBUG_ON        30
#define DEBUG_OFF       31
#define SYSLOG_LEVEL    32
#define SHOW_SYS_LEV    33
#define DEBUG_MASK      34
#define SHOW_DEBUG_MASK 35
#define COUNTER_STRUCT 39


#if 0
struct pkt_header {
    uint8_t ver;
    uint8_t reply;
    uint16_t cmd;
    uint16_t num;
    uint16_t len;
};
#endif

uint16_t vpu_fill_info(uint8_t *pkt, uint8_t *info, uint16_t len, uint16_t type);
uint32_t vpu_fill_regsguard_packet(uint8_t *pkt, uint16_t num, char *name);
uint32_t vpu_fill_alive_packet(uint8_t *pkt, uint16_t num);
uint32_t vpu_fill_regma_packet(uint8_t *pkt, uint16_t num);

int ma_cmd_get_counter(int sockfd, uint16_t sn);
int ma_cmd_reload(uint8_t *data, uint32_t data_len);
int ma_cmd_parser(uint8_t *pkt, uint32_t pkt_len, int sockfd);

void *conn_sguard(void *arg);
void *conn_ma(void *arg);

int connect2ma(void);
int connect2sguard(void);

#endif
