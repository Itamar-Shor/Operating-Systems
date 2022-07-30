#ifndef MSG_SLOT_STRUCT
#define MSG_SLOT_STRUCT

#include "message_slot.h"

typedef struct device_info {
	unsigned long channel_id;
	int minor_num;
}device_info;

typedef struct msg_channel {
	unsigned long channel_id;
	int msg_size;
	char message[BUF_LEN];
	struct msg_channel* next;
}msg_channel;

typedef struct msg_slot_node {
	int minor_num;
	int nof_channels;
	msg_channel* channels_head;
	msg_channel* channels_tail;
	struct msg_slot_node* next;
}msg_slot_node;

typedef struct msg_slot_handler {
	int nof_slots;
	struct msg_slot_node* head;
	struct msg_slot_node* tail;
}msg_slot_handler;

#endif