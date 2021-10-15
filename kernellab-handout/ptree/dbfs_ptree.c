#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/stat.h>

#define MAXLEN 512

MODULE_LICENSE("GPL");

static struct dentry *dir, *inputdir, *ptreedir;
static struct task_struct *curr;
static char buf[MAXLEN];

static ssize_t write_pid_to_input(struct file *fp, 
                                const char __user *user_buffer, 
                                size_t length, 
                                loff_t *position)
{
    pid_t input_pid;
	char buf_add[MAXLEN];
	ssize_t len = 0;
	ssize_t len_add;

	if(copy_from_user(buf, user_buffer, length))
		return -EFAULT;
    sscanf(buf, "%u", &input_pid);

	memset(buf, 0, MAXLEN);

    curr = pid_task(find_get_pid(input_pid), PIDTYPE_PID);
	while(curr->pid){
		memset(buf_add, 0, MAXLEN);
		while((len_add =
			snprintf(buf_add, MAXLEN, "%s (%d)\n", curr->comm, curr->pid))
				== 0);
		memcpy(buf_add+len_add, buf, MAXLEN-len_add);
		memcpy(buf, buf_add, MAXLEN);
		len += len_add;
		curr = curr->parent;
	}
    return len;
}

static ssize_t read_trace_from_output(struct file *fp,
				char __user *user_buffer,
				size_t length,
				loff_t *position)
{
	ssize_t len = length < (MAXLEN-(*position)) ? length : (MAXLEN-(*position));

	if(copy_to_user(user_buffer, buf, len))
	       return -EFAULT;
	(*position) += len;
	return len;	
}

static const struct file_operations fops_input = {
    .write = write_pid_to_input,
};

static const struct file_operations fops_ptree = {
	.read = read_trace_from_output,
};

static int __init dbfs_module_init(void)
{      
    dir = debugfs_create_dir("ptree", NULL); 
    if (!dir) {
        printk("Cannot create ptree dir\n");
        return -1;
    }
    inputdir = debugfs_create_file("input", S_IRWXU, dir, NULL, &fops_input);
	if (!inputdir) {
		printk("Cannot create input file\n");
		return -1;
	}
    ptreedir = debugfs_create_file("ptree", S_IRWXU, dir, NULL, &fops_ptree);
	if (!ptreedir) {
		printk("Cannot create ptree file\n");
		return -1;
	}
	printk("dbfs_ptree module initialize done\n");
    return 0;
}

static void __exit dbfs_module_exit(void)
{
	debugfs_remove_recursive(dir);	
	printk("dbfs_ptree module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
