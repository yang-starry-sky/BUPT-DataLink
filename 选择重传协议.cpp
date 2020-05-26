#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

#define MAX_SEQ 31
#define NR_BUFS ((MAX_SEQ+1)/2)
#define inc(k) if(k < MAX_SEQ)k++;else k=0

//typedef enum { frame_arrival, cksum_err, timeout, network_layer_ready, ack_timeout ,physical_layer_ready} event_type;
typedef enum { data, nak ,ack} frame_kind;
typedef enum { false, true } boolean;
typedef unsigned char seq_nr;

static seq_nr out_buf[NR_BUFS][PKT_LEN];
static seq_nr in_buf[NR_BUFS][PKT_LEN];

typedef struct {
	unsigned char kind; /* FRAME_DATA */
	unsigned char ack;
	unsigned char seq;
	unsigned char info[PKT_LEN];
	unsigned int  padding;
}frame;

boolean no_nak = true;
static unsigned ack_timer = 1000;
static unsigned data_timer = 3000;
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

static void send_data_frame(frame_kind fk, seq_nr frame_nr, seq_nr frame_expected) {
	frame s;

	s.kind = fk;
	s.seq = frame_nr;
	s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
	if (fk == data) {
		for (int i = 0; i < PKT_LEN; i++)
			s.info[i] = out_buf[frame_nr % NR_BUFS][i];
		dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short*)s.info);
		put_frame((unsigned char*)& s, 3 + PKT_LEN);
		start_timer(frame_nr % NR_BUFS, data_timer);
	}
	if (fk == nak) {
		no_nak = false;
		dbg_frame("Send NAK %d\n", s.ack);
		put_frame((unsigned char*)& s, 2);
	}
	if (fk == ack) {
		dbg_frame("Send ACK %d\n", s.ack);
		put_frame((unsigned char*)& s, 2);
	}
		
	stop_ack_timer();
}

int main(int argc, char** argv)
{
	int event, arg;
	int len = 0;

	seq_nr ack_expected;
	seq_nr next_frame_to_send;
	seq_nr frame_expected;
	seq_nr too_far;
	frame r;

	boolean arrived[NR_BUFS];
	seq_nr nbuffered;
	//event_type event;

	protocol_init(argc, argv);
	lprintf("Designed by Yang Chengdong, build: " __DATE__"  "__TIME__"\n");

	enable_network_layer();
	ack_expected = 0;
	next_frame_to_send = 0;
	frame_expected = 0;
	too_far = NR_BUFS;
	nbuffered = 0;
	for (int i = 0; i < NR_BUFS; i++)
		arrived[i] = false;

	while (true) {
		event = wait_for_event(&arg);

		switch (event) {
		case NETWORK_LAYER_READY:
			dbg_event("Nerwork layer ready:\n");
			nbuffered++;
			get_packet(out_buf[next_frame_to_send % NR_BUFS]);
			send_data_frame(data, next_frame_to_send, frame_expected);
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
				if (no_nak)
					send_data_frame(nak, 0,frame_expected);
				break;
			}
			if (r.kind == data) {
				dbg_frame("Recv DATA %d %d, ID %d\n", r.seq, r.ack, *(short*)r.info);
				if ((r.seq != frame_expected) && no_nak)
					send_data_frame(nak, 0, frame_expected);
				else
					start_ack_timer(ack_timer);
				if (between(frame_expected, r.seq, too_far) && (arrived[r.seq % NR_BUFS] == false)) {
					arrived[r.seq % NR_BUFS] = true;
					for (int i = 0; i < len - 7; i++)
						in_buf[r.seq % NR_BUFS][i] = r.info[i];
					while (arrived[frame_expected % NR_BUFS]) {
						put_packet(in_buf[frame_expected % NR_BUFS], len-7);
						no_nak = true;
						arrived[frame_expected % NR_BUFS] = false;
						inc(frame_expected);
						inc(too_far);
						start_ack_timer(ack_timer);
					}
				}
			}
			if ((r.kind == nak) && between(ack_expected, (r.ack + 1) % (MAX_SEQ + 1), next_frame_to_send)) {
				dbg_frame("Recv NAK  %d\n", r.ack);
				send_data_frame(data, (r.ack + 1) % (MAX_SEQ + 1), frame_expected);
			}
			while (between(ack_expected, r.ack, next_frame_to_send)) {
				nbuffered--;
				stop_timer(ack_expected % NR_BUFS);
				inc(ack_expected);
			}
			break;

		case ACK_TIMEOUT:
			dbg_event("ACK %d timeout\n", arg);
			send_data_frame(ack, 0, frame_expected);
			break;

		case DATA_TIMEOUT:
			dbg_event("---- DATA %d timeout\n",arg);
			send_data_frame(data, ack_expected, frame_expected);
			break;
		}
		if (nbuffered < NR_BUFS && phl_ready)
			enable_network_layer();
		else
			disable_network_layer();
	}
}
