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
	char msg_buffer[BUF_LEN];

	if (argc < 3) {
		fprintf(stderr, "got %0d arguments, expected at least 3 arguments!\n", argc);
		exit(1);
	}
	/*set to read only*/
	file_desc = open(argv[1], O_RDONLY);
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
	ret_val = read(file_desc, msg_buffer, BUF_LEN);
	if (ret_val < 0) {
		perror("");
		exit(1);
	}
	ret_val = write(STDOUT_FILENO, msg_buffer, ret_val);
	if (ret_val < 0) {
		perror("");
		exit(1);
	}

	close(file_desc);
	return 0;
}