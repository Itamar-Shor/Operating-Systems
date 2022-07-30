#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE


#include <linux/kernel.h>
#include <linux/module.h> 
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "message_slot.h"
#include "message_slot_structs.h"

MODULE_LICENSE("GPL");

/*=============================================================================*/
								//device fields//
/*=============================================================================*/

msg_slot_handler* handle;

/*=============================================================================*/
								//struct functions//
/*=============================================================================*/

/*if the slots exists: return a pointer to it, o.w return NULL*/
msg_slot_node* search_for_slot
(
	msg_slot_node* head,
	msg_slot_node* tail,
	int minor_num
)
{
	if (head == NULL) return NULL;
	while (head != tail) {
		if ((head->minor_num) == minor_num) return head;
		head = head->next;
	}
	if ((tail->minor_num) == minor_num) return tail;
	return NULL;
}

/*called if search_for_slot(minor_num) == 0
return 1 if allocation failed, o.w return 0*/
int allocate_new_slot
(
	msg_slot_node** head,
	msg_slot_node** tail,
	int minor_num
)
{
	msg_slot_node* new_slot = kcalloc(1, sizeof(msg_slot_node), GFP_KERNEL);
	if (!new_slot) {
		return !SUCCESS;
	}
	new_slot->channels_head = NULL;
	new_slot->channels_tail = new_slot->channels_head;
	new_slot->minor_num = minor_num;
	new_slot->nof_channels = 0;

	if (!(*head)) {
		*head = new_slot;
		*tail = *head;
	}
	else {
		(*tail)->next = new_slot;
		(*tail) = (*tail)->next;
	}
	// printk("[allocate_new_slot]: allocated new slot - %d\n", minor_num);
	return SUCCESS;
}

/*if the channel exists: return a pointer to it, o.w return NULL*/
msg_channel* search_for_channel
(
	unsigned long channel_id,
	msg_channel* channels_head,
	msg_channel* channels_tail
)
{
	if (channels_head == NULL) return NULL;
	while (channels_head != channels_tail) {
		if ((channels_head->channel_id) == channel_id) return channels_head;
		channels_head = channels_head->next;
	}
	if ((channels_tail->channel_id) == channel_id) return channels_tail;
	return NULL;
}

/*assuming channel_id is not 0*/
int allocate_new_channel
(
	msg_channel** channels_head,
	msg_channel** channels_tail,
	unsigned long channel_id
)
{
	msg_channel* channel = kcalloc(1, sizeof(msg_channel), GFP_KERNEL);
	if (!channel) {
		return !SUCCESS;
	}
	channel->channel_id = channel_id;
	channel->msg_size = 0;
	memset(channel->message, 0, sizeof(char)*BUF_LEN);
	if (!(*channels_head)) {
		*channels_head = channel;
		*channels_tail = *channels_head;
	}
	else {
		(*channels_tail)->next = channel;
		*channels_tail = (*channels_tail)->next;
	}
	// printk("[allocate_new_channel]: allocated new channel - %ld\n", channel_id);
	return SUCCESS;
}

void free_channels
(
	msg_channel* channels_head,
	msg_channel* channels_tail
)
{
	msg_channel* temp;
	while (channels_head != channels_tail) {
		temp = channels_head;
		channels_head = channels_head->next;
		// printk("[free_channels]: deallocated channel - %ld\n", temp->channel_id);
		kfree(temp);
	}
	// printk("[free_channels]: deallocated channel - %ld\n", channels_tail->channel_id);
	kfree(channels_tail);
}

void free_all_slots
(
	msg_slot_node* head,
	msg_slot_node* tail
)
{
	msg_slot_node* temp;
	while (head != tail) {
		free_channels(head->channels_head, head->channels_tail);
		temp = head;
		head = head->next;
		// printk("[free_all_slots]: deallocated slot - %d\n", temp->minor_num);
		kfree(temp);
	}
	free_channels(tail->channels_head, tail->channels_tail);
	// printk("[free_all_slots]: deallocated slot - %d\n", tail->minor_num);
	kfree(tail);
	return;
}


/*=============================================================================*/
								//device functions//
/*=============================================================================*/
static int device_open
(
	struct inode* inode, 
	struct file*  file
)
{
	int minor;
	msg_slot_node* slot;
	device_info* info;

	minor = iminor(inode);
	slot = search_for_slot(handle->head, handle->tail, minor);
	if (!slot) {
		if (handle->nof_slots == MAX_DIFFERENT_SLOTS) {
			printk("[device_open]: cannot create new slot since there are already %d slots\n", MAX_DIFFERENT_SLOTS);
			return -ENOMEM;
		}
		if (allocate_new_slot(&(handle->head), &(handle->tail), minor) != SUCCESS) {
			printk("[device_open]: failed to allocate new slot: %d\n",minor);
			return -ENOMEM;
		}
		(handle->nof_slots)++;
	}
	info = kcalloc(1, sizeof(device_info), GFP_KERNEL);
	if (!info) {
		printk("[device_open]: failed to allocate info struct for slot: %d\n", minor);
		return -ENOMEM;
	}
	// printk("[device_open]: allocated info struct \n");
	info->channel_id = 0;
	info->minor_num = minor;
	file->private_data = (void*)info;

	// printk("[device_open]: opened device with minor number %d\n", minor);
	return SUCCESS;
}


static int device_release
(
	struct inode* inode, 
	struct file*  file
)
{
	device_info* info = (device_info*)file->private_data;
	// printk("[device_open]: released device with minor number %d\n", info->minor_num);
	kfree(info);
	// printk("[device_release]: deallocated info struct \n");
	return SUCCESS;
}


static ssize_t device_read
(
	struct file* file,
	char __user* buffer,
	size_t       length,
	loff_t*      offset
)
{
	device_info* info;
	msg_slot_node* slot;
	msg_channel* channel;
	int uncopied_data;

	info = (device_info*)file->private_data;
	slot = search_for_slot(handle->head, handle->tail, info->minor_num);
	handle_errors(!slot || !buffer, EINVAL);
	channel = search_for_channel(info->channel_id, slot->channels_head, slot->channels_tail);
	handle_errors(!channel, EINVAL);
	handle_errors(channel->msg_size == 0, EWOULDBLOCK);
	handle_errors(length < channel->msg_size, ENOSPC);
	handle_errors(!(access_ok(buffer, length)), EINVAL);

	uncopied_data = copy_to_user(buffer, channel->message, channel->msg_size);
	if (uncopied_data > 0) {
		return -1;
	}
	return channel->msg_size;
}


static ssize_t device_write
(
	struct file*       file,
	const char __user* buffer,
	size_t             length,
	loff_t*            offset
)
{
	device_info* info;
	msg_slot_node* slot;
	msg_channel* channel;
	char temp_buff[BUF_LEN];
	int i;

	info = (device_info*)file->private_data;
	slot = search_for_slot(handle->head, handle->tail, info->minor_num);
	handle_errors(!slot || !buffer, EINVAL);
	channel = search_for_channel(info->channel_id, slot->channels_head, slot->channels_tail);
	handle_errors(!channel, EINVAL);
	handle_errors(length == 0 || length > BUF_LEN, EMSGSIZE);
	handle_errors(!(access_ok(buffer, length)), EINVAL);

	for (i = 0; i < length; i++) {
		temp_buff[i] = (channel->message)[i];
		if (get_user((channel->message)[i], buffer + i) != 0) { /*write failed*/
			break;
		}
	}

	if (i < length) {
		/*If we could not write everything, we'll restore the previous message*/
		for (; i >= 0; i--) {
			(channel->message)[i] = temp_buff[i];
		}
		return -1;
	}
	channel->msg_size = length;
	return length;
}


static long device_ioctl
(
	struct   file* file,
	unsigned int   ioctl_command_id,
	unsigned long  ioctl_param
)
{
	device_info* info;
	msg_slot_node* slot;
	msg_channel* channel;

	handle_errors(ioctl_command_id != MSG_SLOT_CHANNEL, EINVAL);
	handle_errors(ioctl_param == 0, EINVAL);

	info = (device_info*)file->private_data;
	slot = search_for_slot(handle->head, handle->tail, info->minor_num);
	handle_errors(!slot, EINVAL);
	channel = search_for_channel(ioctl_param, slot->channels_head, slot->channels_tail);
	if (!channel) {
		if (slot->nof_channels == MAX_MSG_CHANNELS) {
			printk("[device_ioctl]: cannot create new channel since there are already %d channels for slot %d\n", MAX_DIFFERENT_SLOTS, slot->minor_num);
			return -ENOMEM;
		}
		if (allocate_new_channel(&(slot->channels_head), &(slot->channels_tail), ioctl_param) != SUCCESS) {
			printk("[device_ioctl]: failed to allocate new channel: %ld for slot %d\n", ioctl_param, slot->minor_num);
			return -ENOMEM;
		}
		(slot->nof_channels)++;
	}
	info->channel_id = ioctl_param;
	// printk("[device_ioctl]: set channel %ld for device with minor number %d \n", info->channel_id, info->minor_num);
	return SUCCESS;
}

/*=============================================================================*/
								//device setup//
/*=============================================================================*/
// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops =
{
  .owner = THIS_MODULE, // Required for correct count of module usage. This prevents the module from being removed while used.
  .read = device_read,
  .write = device_write,
  .open = device_open,
  .unlocked_ioctl = device_ioctl,
  .release = device_release,
};


static int __init simple_init(void)
{
	int rc = -1;
	handle = kcalloc(1, sizeof(msg_slot_handler), GFP_KERNEL);
	if (!handle) {
		printk("[simple_init]: failed to allocate handle for message slot module\n");
		return -ENOMEM;
	}
	memset(handle, 0, sizeof(msg_slot_handler)); /*initialize all fields to 0*/
	// printk("[simple_init]: created device handle \n");
	// Register driver capabilities. Obtain major num
	rc = register_chrdev(MAJOR_NUMBER, DEVICE_RANGE_NAME, &Fops);
	if (rc < 0){
		printk(KERN_ERR "%s registraion failed for  %d\n", DEVICE_RANGE_NAME, MAJOR_NUMBER);
		//handle errors
		return rc;
	}
	return 0;
}


static void __exit simple_cleanup(void)
{
	free_all_slots(handle->head, handle->tail);
	kfree(handle);
	// printk("[simple_init]: destroyed device handle \n");
	// Unregister the device
	// Should always succeed
	unregister_chrdev(MAJOR_NUMBER, DEVICE_RANGE_NAME);
}

module_init(simple_init);
module_exit(simple_cleanup);