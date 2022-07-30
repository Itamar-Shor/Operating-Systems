#include <fcntl.h>      /* open */ 
#include <unistd.h>     /* exit */
#include <sys/ioctl.h>  /* ioctl */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "message_slot.h"

int main
(
	int argc, 
	char *argv[]
)
{
	int file_desc;
	int ret_val;
	unsigned long channel_id;
	int msg_length;

	if (argc < 4) {
		fprintf(stderr, "got %0d arguments, expected at least 4 arguments!\n",argc);
		exit(1);
	}
	/*set to write only*/
	file_desc = open(argv[1], O_WRONLY);
	if (file_desc < 0)
	{
		perror("");
		exit(1);
	}
	channel_id = atoi(argv[2]);
	/*setting the channel*/
	ret_val = ioctl(file_desc, MSG_SLOT_CHANNEL, channel_id);
	if (ret_val != SUCCESS) {
		perror("");
		exit(1);
	}
	msg_length = strlen(argv[3]);
	ret_val = write(file_desc, argv[3], msg_length);
	if (ret_val != msg_length) {
		perror("");
		exit(1);
	}
	close(file_desc);
	return 0;
}