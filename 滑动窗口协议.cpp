#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define MAX_SEQ 7
#define inc(k) if(k < MAX_SEQ)k++;else k=0

//typedef enum { frame_arrival, cksum_err, timeout, network_layer_ready, ack_timeout ,physical_layer_ready} event_type;
typedef enum { data, ack } frame_kind;
typedef enum { false, true } boolean;
typedef unsigned char seq_nr;

static seq_nr out_buf[MAX_SEQ+1][PKT_LEN];
static seq_nr in_buf[MAX_SEQ + 1][PKT_LEN];

typedef struct {
	unsigned char kind; /* FRAME_DATA */
	unsigned char ack;
	unsigned char seq;
	unsigned char info[PKT_LEN];
	unsigned int  padding;
}frame;

typedef struct {
	unsigned char kind; /* FRAME_DATA */
	unsigned char ack;
	unsigned int  padding;
}ack_frame;

static unsigned ack_timer = 1000;
static unsigned data_timer = 2000;
static int phl_ready = 0;

static void put_frame(unsigned char* frame, int len)
{
	*(unsigned int*)(frame + len) = crc32(frame, len);
	send_frame(frame, len + 4);
	phl_ready = 0;
}

static boolean between(seq_nr a, seq_nr b, seq_nr c) {
	return ((a <= b) && (b < c)) || ((c < a) && (a <= b)) || ((b < c) && (c < a));
}

static void send_data_frame(seq_nr frame_nr, seq_nr frame_expected) {
	frame s;

	s.kind = data;
	s.seq = frame_nr;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	memcpy(s.info, out_buf[frame_nr], PKT_LEN);
	dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short*)s.info);
	put_frame((unsigned char*)& s, 3 + PKT_LEN);
	start_timer(frame_nr , data_timer);
	stop_ack_timer();
}
static void send_ack_frame( seq_nr frame_nr, seq_nr frame_expected)
{
	ack_frame s;

	s.kind = ack;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	dbg_frame("Send ACK  %d\n", s.ack);
	put_frame((unsigned char*)& s, 2);
	stop_ack_timer();
}

int main(int argc, char** argv)
{
	int event, arg;
	int len = 0;

	seq_nr ack_expected;
	seq_nr next_frame_to_send;
	seq_nr frame_expected;
	frame r;
	seq_nr nbuffered;
	
	lprintf("Designed by Yang Chengdong, build: " __DATE__"  "__TIME__"\n");
	protocol_init(argc, argv);
	
	disable_network_layer();
	ack_expected = 0;
	next_frame_to_send = 0;
	frame_expected = 0;
	nbuffered = 0;
	
	while (true) {
		event = wait_for_event(&arg);

		switch (event) {
		case NETWORK_LAYER_READY:
			dbg_event("Nerwork layer ready:\n");
			nbuffered++;
			get_packet(out_buf[next_frame_to_send]);
			send_data_frame(next_frame_to_send, frame_expected);
			inc(next_frame_to_send);
			break;

		case PHYSICAL_LAYER_READY:
			dbg_event("Physical layer ready:\n");
			phl_ready = 1;
			break;

		case FRAME_RECEIVED:
			dbg_event("Frame received:\n");
			len = recv_frame((unsigned char*)& r, sizeof r);
			if (len < 5 || crc32((unsigned char*)& r, len) != 0) {
				dbg_event("**** Receiver Error, Bad CRC Checksum\n");
				break;
			}
			else
			{
				if (r.kind == ack)
					dbg_frame("Recv ACK  %d\n", r.ack);
				if (r.kind == data)
				{
					dbg_frame("Recv DATA %d %d, ID %d\n", r.seq, r.ack, *(short*)r.info);
					if (r.seq == frame_expected)
					{
						put_packet(r.info, len - 7);
						inc(frame_expected);  
						start_ack_timer(ack_timer);
					}
				}
				while (between(ack_expected, r.ack, next_frame_to_send))
				{
					nbuffered--;
					stop_timer(ack_expected);
					inc(ack_expected);
				}
			}
			break;

		case ACK_TIMEOUT:
			dbg_event("ACK %d timeout\n", arg);
			send_ack_frame(next_frame_to_send, frame_expected);
			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n", arg);
			next_frame_to_send= ack_expected; 
			for (int i = 1; i <= nbuffered; i++)
			{
				send_data_frame(next_frame_to_send, frame_expected);
				inc(next_frame_to_send);
			}
			break;
		}
		if (nbuffered < MAX_SEQ && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}
