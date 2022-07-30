#ifndef MSG_SLOT_H
#define MSG_SLOT_H

#include <linux/ioctl.h>

#define SUCCESS 0
#define MAJOR_NUMBER 240
#define DEVICE_RANGE_NAME "msg_slot_dev"
#define BUF_LEN 128
#define MAX_MSG_CHANNELS 0x100000
#define MAX_DIFFERENT_SLOTS 256
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUMBER, 0, unsigned long)

#define handle_errors(condition, errno_val)\
	do{\
		if(condition){\
			return -errno_val;\
		}\
	}while(0)

#endif