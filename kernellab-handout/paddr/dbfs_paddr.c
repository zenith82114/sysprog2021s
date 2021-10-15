#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/stat.h>
#include <asm/pgtable.h>

#define MAXLEN 64

MODULE_LICENSE("GPL");

static struct dentry *dir, *output;
static struct task_struct *task;

static ssize_t read_output(struct file *fp,
                        char __user *user_buffer,
                        size_t length,
                        loff_t *position)
{
    struct mm_struct *mm;
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	pid_t pid = 0;
	uint64_t vaddr = 0;
	uint64_t paddr = 0;

	unsigned char buf[MAXLEN];
	size_t len = (length < MAXLEN) ? length : MAXLEN;
	int i;

	memset(buf, 0, MAXLEN);
	if(copy_from_user(buf, user_buffer, len))
		return -EFAULT;

	for(i=0; i<4; i++)
		pid += ((pid_t)(buf[i])) << (8*i);
	for(i=0; i<6; i++)
		vaddr += ((uint64_t)(buf[8+i])) << (8*i);

	task = pid_task(find_get_pid(pid), PIDTYPE_PID);
	if(!task) return -EINVAL;
	mm = task->mm;
	pgdp = pgd_offset(mm, vaddr);
	if(pgd_none(*pgdp) || pgd_bad(*pgdp)) return -EINVAL;
	p4dp = p4d_offset(pgdp, vaddr);
	if(p4d_none(*p4dp) || p4d_bad(*p4dp)) return -EINVAL;
	pudp = pud_offset(p4dp, vaddr);
	if(pud_none(*pudp) || pud_bad(*pudp)) return -EINVAL;
	pmdp = pmd_offset(pudp, vaddr);
	if(pmd_none(*pmdp) || pmd_bad(*pmdp)) return -EINVAL;
	ptep = pte_offset_kernel(pmdp, vaddr);
	if(pte_none(*ptep) || !pte_present(*ptep)) return -EINVAL;
	paddr = (pte_val(*ptep) & PAGE_MASK) | (vaddr & ~PAGE_MASK);

	for(i=0; i<5; i++)
		buf[16+i] = (paddr >> (8*i)) & 255;
	
	// debug
	//printk("%llx\n", paddr);
	//for(i=0; i<length; i++)
	//	printk("%d: %x\n", i, buf[i]);

	if(copy_to_user(user_buffer, buf, len))
		return -EFAULT;
	position += len;
	return len;

}

static const struct file_operations dbfs_fops = {
    .read = read_output,
};

static int __init dbfs_module_init(void)
{
    dir = debugfs_create_dir("paddr", NULL);
    if (!dir) {
        printk("Cannot create paddr dir\n");
        return -1;
    }

    output = debugfs_create_file("output", S_IRWXU, dir, NULL, &dbfs_fops);
	if (!output) {
		printk("Cannot create output file\n");
		return -1;
	}

	printk("dbfs_paddr module initialize done\n");
    return 0;
}

static void __exit dbfs_module_exit(void)
{
	debugfs_remove_recursive(dir);
	printk("dbfs_paddr module exit\n");
}

module_init(dbfs_module_init);
module_exit(dbfs_module_exit);
