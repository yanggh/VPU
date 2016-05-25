/*
 * SpanDSP - a series of DSP components for telephony
 *
 * fax_decode.c - a simple FAX audio decoder
 *
 * Written by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2005 Steve Underwood
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*! \page fax_decode_page FAX decoder
\section fax_decode_page_sec_1 What does it do?
???.

\section fax_decode_tests_page_sec_2 How does it work?
???.
*/

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "fax_queue.h"
#include "fax_decode.h"
#include "conf.h"

#include "counter.h"
#include "atomic.h"
#include "vpu.h"
#include "send_cdr.h"

//#if defined(WITH_SPANDSP_INTERNALS)
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
//#endif

#include "sndfile.h"
#include "spandsp.h"
#include <syslog.h>
#include <stdint.h>
#include <errno.h>
#include "../../common/applog.h"

#define SAMPLES_PER_CHUNK   160

#define DISBIT1     0x01
#define DISBIT2     0x02
#define DISBIT3     0x04
#define DISBIT4     0x08
#define DISBIT5     0x10
#define DISBIT6     0x20
#define DISBIT7     0x40
#define DISBIT8     0x80

enum
{
    FAX_NONE,
    FAX_V27TER_RX,
    FAX_V29_RX,
    FAX_V17_RX
};

static const struct
{
    int bit_rate;
    int modem_type;
    int which;
    uint8_t dcs_code;
} fallback_sequence[] =
{
    {14400, T30_MODEM_V17,    T30_SUPPORT_V17,    DISBIT6},
    {12000, T30_MODEM_V17,    T30_SUPPORT_V17,    (DISBIT6 | DISBIT4)},
    { 9600, T30_MODEM_V17,    T30_SUPPORT_V17,    (DISBIT6 | DISBIT3)},
    { 9600, T30_MODEM_V29,    T30_SUPPORT_V29,    DISBIT3},
    { 7200, T30_MODEM_V17,    T30_SUPPORT_V17,    (DISBIT6 | DISBIT4 | DISBIT3)},
    { 7200, T30_MODEM_V29,    T30_SUPPORT_V29,    (DISBIT4 | DISBIT3)},
    { 4800, T30_MODEM_V27TER, T30_SUPPORT_V27TER, DISBIT4},
    { 2400, T30_MODEM_V27TER, T30_SUPPORT_V27TER, 0},
    {    0, 0, 0, 0}
};

int decode_test = FALSE;
int rx_bits = 0;

t30_state_t t30_dummy[6];
t4_rx_state_t t4_rx_state[6];
int t4_up[6] = {FALSE, FALSE, FALSE, FALSE, FALSE, FALSE};

hdlc_rx_state_t hdlcrx[6];

int fast_trained[6] = {FAX_NONE, FAX_NONE, FAX_NONE, FAX_NONE, FAX_NONE, FAX_NONE};

uint8_t ecm_data[6][256][260];
int16_t ecm_len[6][256];

int line_encoding[6] = {T4_COMPRESSION_ITU_T4_1D, T4_COMPRESSION_ITU_T4_1D, T4_COMPRESSION_ITU_T4_1D, T4_COMPRESSION_ITU_T4_1D, T4_COMPRESSION_ITU_T4_1D, T4_COMPRESSION_ITU_T4_1D};
int x_resolution[6] = {T4_X_RESOLUTION_R8, T4_X_RESOLUTION_R8, T4_X_RESOLUTION_R8, T4_X_RESOLUTION_R8, T4_X_RESOLUTION_R8, T4_X_RESOLUTION_R8};
int y_resolution[6] = {T4_Y_RESOLUTION_STANDARD, T4_Y_RESOLUTION_STANDARD, T4_Y_RESOLUTION_STANDARD, T4_Y_RESOLUTION_STANDARD, T4_Y_RESOLUTION_STANDARD, T4_Y_RESOLUTION_STANDARD};
int image_width[6] = {1728, 1728, 1728, 1728, 1728, 1728};
int octets_per_ecm_frame[6] = {256, 256, 256, 256, 256, 256};
int error_correcting_mode[6] = {FALSE, FALSE, FALSE, FALSE, FALSE, FALSE};
int current_fallback[6] = {0, 0, 0, 0, 0, 0};
int fax_flag[6] = {0, 0, 0, 0, 0, 0};


static void decode_20digit_msg(const uint8_t *pkt, int len)
{
    int p;
    int k;
    char msg[T30_MAX_IDENT_LEN + 1];

    if (len > T30_MAX_IDENT_LEN + 3)
    {
        fprintf(stderr, "XXX %d %d\n", len, T30_MAX_IDENT_LEN + 1);
        msg[0] = '\0';
        return;
    }
    pkt += 2;
    p = len - 2;
    /* Strip trailing spaces */
    while (p > 1  &&  pkt[p - 1] == ' ')
        p--;
    /* The string is actually backwards in the message */
    k = 0;
    while (p > 1)
        msg[k++] = pkt[--p];
    msg[k] = '\0';
    fprintf(stderr, "%s is: \"%s\"\n", t30_frametype(pkt[0]), msg);
}
/*- End of function --------------------------------------------------------*/

static void print_frame(unsigned int idx, const char *io, const uint8_t *fr, int frlen)
{
    int i;
    int type;
    const char *country;
    const char *vendor;
    const char *model;
    
    fprintf(stderr, "%s %s:", io, t30_frametype(fr[2]));
    for (i = 2;  i < frlen;  i++)
        fprintf(stderr, " %02x", fr[i]);
    fprintf(stderr, "\n");
    type = fr[2] & 0xFE;
    if (type == T30_DIS  ||  type == T30_DTC  ||  type == T30_DCS)
        t30_decode_dis_dtc_dcs(&t30_dummy[idx], fr, frlen);
    if (type == T30_CSI  ||  type == T30_TSI  ||  type == T30_PWD  ||  type == T30_SEP  ||  type == T30_SUB  ||  type == T30_SID)
        decode_20digit_msg(fr, frlen);
    if (type == T30_NSF  ||  type == T30_NSS  ||  type == T30_NSC)
    {
        if (t35_decode(&fr[3], frlen - 3, &country, &vendor, &model))
        {
            if (country)
                fprintf(stderr, "The remote was made in '%s'\n", country);
            if (vendor)
                fprintf(stderr, "The remote was made by '%s'\n", vendor);
            if (model)
                fprintf(stderr, "The remote is a '%s'\n", model);
        }
    }
}
/*- End of function --------------------------------------------------------*/

static int find_fallback_entry(int dcs_code)
{
    int i;

    /* The table is short, and not searched often, so a brain-dead linear scan seems OK */
    for (i = 0;  fallback_sequence[i].bit_rate;  i++)
    {
        if (fallback_sequence[i].dcs_code == dcs_code)
            break;
    }
    if (fallback_sequence[i].bit_rate == 0)
        return -1;
    return i;
}
/*- End of function --------------------------------------------------------*/

static int check_rx_dcs(unsigned int idx, const uint8_t *msg, int len)
{
    static const int widths[3][4] =
    {
        { 864, 1024, 1216, -1}, /* R4 resolution - no longer used in recent versions of T.30 */
        {1728, 2048, 2432, -1}, /* R8 resolution */
        {3456, 4096, 4864, -1}  /* R16 resolution */
    };
    uint8_t dcs_frame[T30_MAX_DIS_DTC_DCS_LEN];

    /* Check DCS frame from remote */
    if (len < 6)
    {
        printf("Short DCS frame\n");
        return -1;
    }

    /* Make a local copy of the message, padded to the maximum possible length with zeros. This allows
       us to simply pick out the bits, without worrying about whether they were set from the remote side. */
    if (len > T30_MAX_DIS_DTC_DCS_LEN)
    {
        memcpy(dcs_frame, msg, T30_MAX_DIS_DTC_DCS_LEN);
    }
    else
    {
        memcpy(dcs_frame, msg, len);
        if (len < T30_MAX_DIS_DTC_DCS_LEN)
            memset(dcs_frame + len, 0, T30_MAX_DIS_DTC_DCS_LEN - len);
    }

    octets_per_ecm_frame[idx] = (dcs_frame[6] & DISBIT4)  ?  256  :  64;
    if ((dcs_frame[8] & DISBIT1))
        y_resolution[idx] = T4_Y_RESOLUTION_SUPERFINE;
    else if (dcs_frame[4] & DISBIT7)
        y_resolution[idx] = T4_Y_RESOLUTION_FINE;
    else
        y_resolution[idx] = T4_Y_RESOLUTION_STANDARD;
    image_width[idx] = widths[(dcs_frame[8] & DISBIT3)  ?  2  :  1][dcs_frame[5] & (DISBIT2 | DISBIT1)];

    /* Check which compression we will use. */
    if ((dcs_frame[6] & DISBIT7))
        line_encoding[idx] = T4_COMPRESSION_ITU_T6;
    else if ((dcs_frame[4] & DISBIT8))
        line_encoding[idx] = T4_COMPRESSION_ITU_T4_2D;
    else
        line_encoding[idx] = T4_COMPRESSION_ITU_T4_1D;
    fprintf(stderr, "Selected compression %d\n", line_encoding[idx]);

    if ((current_fallback[idx] = find_fallback_entry(dcs_frame[4] & (DISBIT6 | DISBIT5 | DISBIT4 | DISBIT3))) < 0)
        printf("Remote asked for a modem standard we do not support\n");
    error_correcting_mode[idx] = ((dcs_frame[6] & DISBIT3) != 0);

    //v17_rx_restart(&v17, fallback_sequence[fallback_entry].bit_rate, FALSE);
    return 0;
}
/*- End of function --------------------------------------------------------*/

static void hdlc_accept(void *user_data, const uint8_t *msg, int len, int ok)
{
    int type;
    int frame_no;
    int i;
	unsigned long idx;

	idx = (unsigned long)user_data;

    if (len < 0)
    {
        /* Special conditions */
        fprintf(stderr, "HDLC status is %s (%d)\n", signal_status_to_str(len), len);
        return;
    }

    if (ok)
    {
        if (msg[0] != 0xFF  ||  !(msg[1] == 0x03  ||  msg[1] == 0x13))
        {
            fprintf(stderr, "Bad frame header - %02x %02x\n", msg[0], msg[1]);
            return;
        }
        print_frame(idx, "HDLC: ", msg, len);
        type = msg[2] & 0xFE;
        switch (type)
        {
        case T4_FCD:
            if (len <= 4 + 256)
            {
                frame_no = msg[3];
                /* Just store the actual image data, and record its length */
                memcpy(&ecm_data[idx][frame_no][0], &msg[4], len - 4);
                ecm_len[idx][frame_no] = (int16_t) (len - 4);
            }
            break;
        case T30_DCS:
            check_rx_dcs(idx, msg, len);
            break;

		case T30_CFR:
			fax_flag[idx] = 1;
			break;
        }
    }
    else
    {
        fprintf(stderr, "Bad HDLC frame ");
        for (i = 0;  i < len;  i++)
            fprintf(stderr, " %02x", msg[i]);
        fprintf(stderr, "\n");
    }
}
/*- End of function --------------------------------------------------------*/

static void t4_begin(unsigned int idx)
{
    int i;

    //printf("Begin T.4 - %d %d %d %d\n", line_encoding, x_resolution, y_resolution, image_width);
    t4_rx_set_rx_encoding(&t4_rx_state[idx], line_encoding[idx]);
    t4_rx_set_x_resolution(&t4_rx_state[idx], x_resolution[idx]);
    t4_rx_set_y_resolution(&t4_rx_state[idx], y_resolution[idx]);
    t4_rx_set_image_width(&t4_rx_state[idx], image_width[idx]);

    t4_rx_start_page(&t4_rx_state[idx]);
    t4_up[idx] = TRUE;

    for (i = 0;  i < 256;  i++)
        ecm_len[idx][i] = -1;
}
/*- End of function --------------------------------------------------------*/

static void t4_end(unsigned int idx)
{
    t4_stats_t stats;
    int i;

    if (!t4_up[idx])
        return;
    if (error_correcting_mode[idx])
    {
        for (i = 0;  i < 256;  i++)
        {
            if (ecm_len[idx][i] > 0)
                t4_rx_put_chunk(&t4_rx_state[idx], ecm_data[idx][i], ecm_len[idx][i]);
            fprintf(stderr, "%d", (ecm_len[idx][i] <= 0)  ?  0  :  1);
        }
        fprintf(stderr, "\n");
    }
    t4_rx_end_page(&t4_rx_state[idx]);
    t4_rx_get_transfer_statistics(&t4_rx_state[idx], &stats);
    fprintf(stderr, "Pages = %d\n", stats.pages_transferred);
    fprintf(stderr, "Image size = %dx%d\n", stats.width, stats.length);
    fprintf(stderr, "Image resolution = %dx%d\n", stats.x_resolution, stats.y_resolution);
    fprintf(stderr, "Bad rows = %d\n", stats.bad_rows);
    fprintf(stderr, "Longest bad row run = %d\n", stats.longest_bad_row_run);
    t4_up[idx] = FALSE;
}
/*- End of function --------------------------------------------------------*/

static void v21_put_bit(void *user_data, int bit)
{
	unsigned long idx;

	idx = (unsigned long)user_data;

    if (bit < 0)
    {
        /* Special conditions */
        fprintf(stderr, "V.21 rx status is %s (%d)\n", signal_status_to_str(bit), bit);
        switch (bit)
        {
        case SIG_STATUS_CARRIER_DOWN:
            //t4_end();
            break;
        }
        return;
    }
    if (fast_trained[idx] == FAX_NONE)
        hdlc_rx_put_bit(&hdlcrx[idx], bit);
    //printf("V.21 Rx bit %d - %d\n", rx_bits++, bit);
}
/*- End of function --------------------------------------------------------*/

static void v17_put_bit(void *user_data, int bit)
{
	unsigned long idx;

	idx = (unsigned long)user_data;

    if (bit < 0)
    {
        /* Special conditions */
        fprintf(stderr, "V.17 rx status is %s (%d)\n", signal_status_to_str(bit), bit);
        switch (bit)
        {
        case SIG_STATUS_TRAINING_SUCCEEDED:
            fast_trained[idx] = FAX_V17_RX;
            t4_begin(idx);
            break;
        case SIG_STATUS_CARRIER_DOWN:
            t4_end(idx);
            if (fast_trained[idx] == FAX_V17_RX)
                fast_trained[idx] = FAX_NONE;
            break;
        }
        return;
    }
    if (error_correcting_mode[idx])
    {
        hdlc_rx_put_bit(&hdlcrx[idx], bit);
    }
    else
    {
        if (t4_rx_put_bit(&t4_rx_state[idx], bit))
        {
            t4_end(idx);
            fprintf(stderr, "End of page detected\n");
        }
    }
    //printf("V.17 Rx bit %d - %d\n", rx_bits++, bit);
}
/*- End of function --------------------------------------------------------*/

static void v29_put_bit(void *user_data, int bit)
{
	unsigned long idx;

	idx = (unsigned long)user_data;

    if (bit < 0)
    {
        /* Special conditions */
        fprintf(stderr, "V.29 rx status is %s (%d)\n", signal_status_to_str(bit), bit);
        switch (bit)
        {
        case SIG_STATUS_TRAINING_SUCCEEDED:
            fast_trained[idx] = FAX_V29_RX;
            t4_begin(idx);
            break;
        case SIG_STATUS_CARRIER_DOWN:
            t4_end(idx);
            if (fast_trained[idx] == FAX_V29_RX)
                fast_trained[idx] = FAX_NONE;
            break;
        }
        return;
    }
    if (error_correcting_mode[idx])
    {
        hdlc_rx_put_bit(&hdlcrx[idx], bit);
    }
    else
    {
        if (t4_rx_put_bit(&t4_rx_state[idx], bit))
        {
            t4_end(idx);
            fprintf(stderr, "End of page detected\n");
        }
    }
    //printf("V.29 Rx bit %d - %d\n", rx_bits++, bit);
}
/*- End of function --------------------------------------------------------*/

static void v27ter_put_bit(void *user_data, int bit)
{
	unsigned long idx;

	idx = (unsigned long)user_data;

    if (bit < 0)
    {
        /* Special conditions */
        fprintf(stderr, "V.27ter rx status is %s (%d)\n", signal_status_to_str(bit), bit);
        switch (bit)
        {
        case SIG_STATUS_TRAINING_SUCCEEDED:
            fast_trained[idx] = FAX_V27TER_RX;
            t4_begin(idx);
            break;
        case SIG_STATUS_CARRIER_DOWN:
            t4_end(idx);
            if (fast_trained[idx] == FAX_V27TER_RX)
                fast_trained[idx] = FAX_NONE;
            break;
        }
        return;
    }
    if (error_correcting_mode[idx])
    {
        hdlc_rx_put_bit(&hdlcrx[idx], bit);
    }
    else
    {
        if (t4_rx_put_bit(&t4_rx_state[idx], bit))
        {
            t4_end(idx);
            fprintf(stderr, "End of page detected\n");
        }
    }
    //printf("V.27ter Rx bit %d - %d\n", rx_bits++, bit);
}
/*- End of function --------------------------------------------------------*/

int decode_alaw(unsigned long idx, char *decode_name)
{
	fsk_rx_state_t *fsk;
	v17_rx_state_t *v17;
	v29_rx_state_t *v29;
	v27ter_rx_state_t *v27ter;
	int16_t amp[SAMPLES_PER_CHUNK];
	char    tmp_file[256];
	SNDFILE *inhandle;
	SF_INFO info;
	int ret = 0;
	int len;

	memset(&info, 0, sizeof(info));
	if ((inhandle = sf_open(decode_name, SFM_READ, &info)) == NULL)
	{
	    applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "Cannot open audio file '%s' for reading\n", decode_name);
        return -2;
	}
	if (info.samplerate != SAMPLE_RATE)
	{
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "Unexpected sample rate in audio file '%s'\n", decode_name);
		return -2;
	}
	if (info.channels != 1)
	{
        applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX,  "Unexpected number of channels in audio file '%s'\n", decode_name);
		return -2;
	}

	memset(&t30_dummy[idx], 0, sizeof(t30_state_t));
	span_log_init(&t30_dummy[idx].logging, SPAN_LOG_FLOW, NULL);
	span_log_set_protocol(&t30_dummy[idx].logging, "T.30");

	hdlc_rx_init(&hdlcrx[idx], FALSE, TRUE, 5, hdlc_accept, (void *)idx);
	fsk = fsk_rx_init(NULL, &preset_fsk_specs[FSK_V21CH2], FSK_FRAME_MODE_SYNC, v21_put_bit, (void *)idx);
	v17 = v17_rx_init(NULL, 14400, v17_put_bit, (void *)idx);
	v29 = v29_rx_init(NULL, 9600, v29_put_bit, (void *)idx);
	//v29 = v29_rx_init(NULL, 7200, v29_put_bit, NULL);
	v27ter = v27ter_rx_init(NULL, 4800, v27ter_put_bit, (void *)idx);

	fsk_rx_signal_cutoff(fsk, -45.5);
	v17_rx_signal_cutoff(v17, -45.5);
	v29_rx_signal_cutoff(v29, -45.5);
	v27ter_rx_signal_cutoff(v27ter, -40.0);

	bzero(tmp_file, 256);
	snprintf(tmp_file, 256, "%s.tif", decode_name);
	if (t4_rx_init(&t4_rx_state[idx], tmp_file, T4_COMPRESSION_ITU_T4_2D) == NULL)
	{
		applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX,  "Failed to init");
		return -2;
	}

	for (;;)
	{
		len = sf_readf_short(inhandle, amp, SAMPLES_PER_CHUNK);
		if (len < SAMPLES_PER_CHUNK)
			break;

		fsk_rx(fsk, amp, len);
		v17_rx(v17, amp, len);
		v29_rx(v29, amp, len);
		v27ter_rx(v27ter, amp, len);
	}
	t4_rx_release(&t4_rx_state[idx]);

	if (sf_close(inhandle))
	{
		applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX,  "Cannot close audio file '%s'\n", decode_name);
		return -2;
	}

    bzero(tmp_file, 256);
	snprintf(tmp_file, 256, "%s.tif", decode_name);
	ret = access(tmp_file, F_OK);

	if(0 == ret)
	{
		remove(tmp_file);
		return 1;
	}

	return fax_flag[idx];
}

typedef struct  
{  
    uint32 ChunkID;             //00H 4 char "RIFF"标志  
    uint32 ChunkSize;           //04H 4 long int 文件长度 文总长-8  
    uint32 Format;              //08H 4 char "WAVE"标志  
    uint32 SubChunk1ID;         //0CH 4 char "fmt "标志  
    uint32 SubChunk1Size;       //10H 4 0x12000000H(ALAW)  
    uint16 AudioFormat;         //14H 2 int 格式类别 0x0600H  
    uint16 NumChannels;         //16H 2 int 通道数，单声道为1，双声道为2  
    uint32 SampleRate;          //18H 4 int 采样率（每秒样本数），表示每个通道的播放速度，  
    uint32 ByteRate;            //1CH 4 long int 波形音频数据传送速率，其值Channels×SamplesPerSec×BitsPerSample/8  
    uint16 BlockAlign;          //20H 2 int 数据块的调整数（按字节算的），其值为Channels×BitsPerSample/8  
    //uint16 BitsPerSample;       //22H 2 每样本的数据位数，表示每个声道中各个样本的数据位数。如果有多个声道，对每个声道而言，样本大小都一样。  
    uint32 BitsPerSample;       //22H 4 每样本的数据位数，表示每个声道中各个样本的数据位数。如果有多个声道，对每个声道而言，样本大小都一样。  
    uint32 WaveFact;                        //26H 4 char "fact"标志  
    uint32 Temp1;                               //2AH 4 0x04000000H  
    uint32 Temp2;                               //2EH 4 0x00530700H  
    uint32 DataTag;             //32H 4 char 数据标记符＂data＂  
    uint32 DataLen;                         //36H 4 long int 语音数据的长度(文长-58)  
}ALAW_HEAD, *PALAW_HEAD;



void init_alaw_head(PALAW_HEAD head)
{  
    head->ChunkID = 0x46464952;  
    //head->ChunkSize = 0x50c012;  
    head->Format = 0x45564157;  
    head->SubChunk1ID = 0x20746d66;  
    head->SubChunk1Size = 0x12;//0x10;  
    head->AudioFormat = 0x06;  
    head->NumChannels = 0x01;//0x2;  
    head->SampleRate = 0x1f40; //0x3E80;//0x1f40;//0xac44;                 // 采样率  
    head->ByteRate = 0x1f40;//0x1f40;//0x2b110;                  // 波形传送速率  
    head->BlockAlign = 0x01;//0x4;                       // 调整数  
    head->BitsPerSample = 0x08;//0x10;                   // 量化数  
    head->WaveFact = 0x74636166;  
    head->Temp1 = 0x04;  
    head->Temp2 = 0x075300;  
    head->DataTag = 0x61746164;  
}

int decode_mem(int pth_no, char *buf, int length, long long callid)
{
	int ret = 0;
	ALAW_HEAD  alaw_head;
	char filename[1024];

	bzero(filename, 1024);
	snprintf(filename, 1024, "/dev/shm/%lld", callid);
	FILE *fp = fopen(filename, "w");
	if(NULL == fp)
	{
    	applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "fopen %s error: %s", filename, strerror(errno));
		return 0;
	}

	init_alaw_head(&alaw_head);
	alaw_head.ChunkSize = length + 50;
	alaw_head.DataLen = length;

	fwrite(&alaw_head, sizeof(ALAW_HEAD), 1, fp);
	fwrite(buf, 1, length, fp);
	fflush(fp);
	fclose(fp);
	fp = NULL;

	ret = decode_alaw(pth_no, filename);
	remove(filename);
	return ret;
}

int cconnect(char *ip, int port)
{
	int sockfd;
	struct sockaddr_in servaddr;
	struct timeval timeo = {3, 0};
	socklen_t len = sizeof(timeo);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1)
	{
		applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "create socket ip %s, port %d fail\n", ip, port);
		return -1;
	}

	//set the timeout period
	setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeo, len);

	memset(&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons(port);
	if(inet_pton(AF_INET, ip, &servaddr.sin_addr) != 1)
	{
		applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "inet_pton error for %s\n", ip);
		close(sockfd);
		return -1;
	}

	if(connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0)
	{
		if (errno == EINPROGRESS)
		{
			applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "connect to ip %s, port is %d, overtime\n", ip, port);
		}
		close(sockfd);
		return -1;
	}

	return sockfd;
}

void init_connect()
{
	int i = 0;

	for(i = 0; i < fax_num; i++)
	{
		if(fax_infos[i].flag == 1)
		{
			if(fax_infos[i].fd == -1)
			{
				fax_infos[i].fd = cconnect(fax_infos[i].ip, fax_infos[i].port);
				if (fax_infos[i].fd == -1)
				{
					applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "fax ip %s and port %u connect error!", fax_infos[i].ip, fax_infos[i].port);
                    sleep(1);
				}
				else
				{
					applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "fax ip %s and port %u connect success!", fax_infos[i].ip, fax_infos[i].port);
				}
			}
		}
	}
}

void *fax_pre(void *arg)
{
    int i = 0;
	int m = 0;
	int tmp_m = 0;
	STRUCT_CDR_PATH *p = NULL;

	PTH_STRUCT  *queue = (PTH_STRUCT*)arg;
	QUEUE* eq = queue->eq;
	QUEUE* fq = queue->fq;
	int   flag = queue->flag;

	while(1)
	{
			if (unlikely(svm_signal_flags & SVM_DONE))
			{
					break;
			}

			p = FETCH_PHEAD(fq);
			if(p == NULL)
			{
					usleep(100);
					continue;
			}

			atomic64_add(&fax_dequeue, 1);
			p->cdr.callflag = decode_mem(flag, p->buf_up, FAX_BUF_SIZE, p->cdr.callid);
			if(p->cdr.callflag != 1)
			{
				p->cdr.callflag = decode_mem(flag, p->buf_down, FAX_BUF_SIZE, p->cdr.callid);
				if(p->cdr.callflag != 1)
				{
					p->cdr.callflag = 0;
				}
			}

			fax_flag[flag] = 0;
			//send to cdr
			cdr_insert(&p->cdr);

			if(p->cdr.callflag == 1)
			{
					atomic64_add(&fax_yes, 1);
					tmp_m = m;
					//send to fax decode
					while(1)
					{
							if(svm_fax_reload == 1)
							{
									//reload connect
									int min = fax_num < r_fax_num ? fax_num : r_fax_num;
									for(i = 0; i < min; i++)
									{
											//Compared with existing(fax_infos and r_fax_info) data
											if((memcmp(fax_infos[i].ip, r_fax_infos[i].ip, strlen(r_fax_infos[i].ip)) != 0)
															|| (fax_infos[i].port != r_fax_infos[i].port))
											{
													//reload connect
													close(fax_infos[i].fd);
													memcpy(fax_infos[i].ip, r_fax_infos[i].ip, strlen(r_fax_infos[i].ip));
													fax_infos[i].port = r_fax_infos[i].port;
													fax_infos[i].fd = r_fax_infos[i].fd = -1;
													fax_infos[i].flag = r_fax_infos[i].flag = 1;
											}
									}

									if(r_fax_num < fax_num)
									{
											for(i = min; i < fax_num; i++)
											{
													fax_infos[i].id = 0;
													bzero(fax_infos[i].ip, 16);
													fax_infos[i].port = 0;
													fax_infos[i].fd = -1;
													fax_infos[i].flag = 0;
											}
									}
									else
									{
											for(i = min; i < r_fax_num; i++)
											{
													fax_infos[i].id = r_fax_infos[i].id;
													memcpy(fax_infos[i].ip, r_fax_infos[i].ip, strlen(r_fax_infos[i].ip));
													fax_infos[i].port = r_fax_infos[i].port;
													fax_infos[i].fd = r_fax_infos[i].fd = -1;
													fax_infos[i].flag = r_fax_infos[i].flag = 1;
											}
									}

									for(i = 0; i < r_fax_num; i++)
									{
											r_fax_infos[i].id = 0;
											bzero(r_fax_infos[i].ip, 16);
											r_fax_infos[i].port = 0;
											r_fax_infos[i].fd = -1;
											r_fax_infos[i].flag = 0;
									}

									fax_num = r_fax_num;
									svm_fax_reload = 0;
							}

							if(fax_infos[m].flag == 1)
							{
									if(fax_infos[m].fd == -1)
									{
											if(flag == 1)
											{
													fax_infos[m].fd = cconnect(fax_infos[m].ip, fax_infos[m].port);
													if(-1 == fax_infos[m].fd)
													{
															applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "fax_fd reconect error, fax_ip is %s, fax_port %d", fax_infos[m].ip, fax_infos[m].port);
															sleep(1);
													}
													else
													{
															applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "fax_fd reconect success, fax_ip is %s, fax_port %d", fax_infos[m].ip, fax_infos[m].port);
													}

											}
									}

									if(fax_infos[m].fd > 0)
									{
											if(sizeof(p->cdr) == send(fax_infos[m].fd, &(p->cdr), sizeof(p->cdr), 0))
											{
													break;     
											}
											else
											{
													close(fax_infos[m].fd);
													fax_infos[m].fd = -1;
											}
									}
							}

							m ++;
							if(m == fax_num)
							{
									m = 0;
							}
							//traverse  all fax service
							if(m == tmp_m)
							{
									applog(APP_LOG_LEVEL_ERR, APP_VPU_LOG_MASK_FAX, "send fax is error!\n");
									//insert queue
									break;
							}
					}

					m ++;
					if(m == fax_num)
					{
							m = 0;
					}
			}
			else
			{
					atomic64_add(&fax_no, 1);
			}
			p->next = NULL;
			EN_QUEUE(eq, p);
	}

	applog(APP_LOG_LEVEL_INFO, APP_VPU_LOG_MASK_FAX, "pthread fax_pre %d exit", flag);

	pthread_exit(NULL);
}

/*- End of function --------------------------------------------------------*/
/*- End of file ------------------------------------------------------------*/
