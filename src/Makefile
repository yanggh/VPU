CC = gcc
CFLAGS = -Wall -Werror -O3 -mcmodel=medium -g -I../../include

LIBS = -L../../lib -lpthread -lpcap -lyaml -lspandsp -ltiff -lm -lsndfile

OBJS += vpu.o
OBJS += vpu_worker.o 
OBJS += voice_parser.o
OBJS += pkt_fifo.o
OBJS += conf.o
OBJS += yaml_conf.o
OBJS += util-strlcatu.o
OBJS += util-strlcpyu.o
OBJS += conn_serv.o
OBJS += counter.o
OBJS += file_server.o
OBJS += cycle_operation.o
OBJS += daemon.o
OBJS += fax_queue.o
OBJS += fax_decode.o
OBJS += send_cdr.o
OBJS += ../../common/applog.o
OBJS += ../../common/ma_pkt.o

TARGET = vpu

.PHONY: all clean

all: $(TARGET)
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

$(OBJS): %.o : %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJS) $(TARGET)
