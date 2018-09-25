/*
*使用互斥信号量来做互斥
*/
#include<linux/module.h>
#include<linux/kernel.h>
#include<linux/cdev.h>
#include<linux/fs.h>
#include<linux/kdev_t.h>
#include<linux/types.h>
#include<asm/uaccess.h>
#include<linux/string.h>
#include <linux/slab.h>
#include <linux/errno.h>

#include "scull0.h"
unsigned int Major=20;
unsigned int Minor=0;
static struct scull_dev scull_devices[scull_nr_devs];

int scull_trim(struct scull_dev *dev)
{
    struct scull_qset *next=NULL, *dptr=NULL;
    printk(KERN_ERR "scull_trim\n");
    int qset = dev->qset;
    int i;
    //所有链表
    for(dptr = dev->data; dptr; dptr = next)
    {
 //   	printk("2\n");
        if(dptr->data){
 //       printk("3\n");
            for(i=0; i<qset; i++) {
                kfree(dptr->data[i]);   //删除量子里的一个数据
//                printk("4.%d\n", i);
            }
            kfree(dptr->data);  //删除一个量子
//            printk("5\n");
            dptr->data = NULL;
//            printk("6\n");
        }
    }
    dev->total_size = 0;
//    printk("7\n");
    return 0;
}

/*参数说明
*item:代表第几个scull_device设备
*PS:scull_device链表头由全局变量dev->data标识.
*/
struct scull_qset *scull_follow(struct scull_dev *dev, int item)
{
	struct scull_qset *dev_dst=NULL;	//第几个链表项	
	int i=0;
	printk("scull_follow\n");
	if(dev->data == NULL)
	{
		printk(KERN_ERR "new qset and this first qset\n");
		dev_dst = kmalloc(sizeof(struct scull_qset), GFP_KERNEL); //初始化第一个链表项
		memset(dev_dst, 0, sizeof(struct scull_qset));
		dev->data = dev_dst;
	}
//沿着dev->data根据item下标找到具体的dev_dst
	dev_dst = dev->data;	
//	printk("scull_follow:1.1\n");
	for(i=1; i<=item; i++)
	{
		dev_dst = dev_dst->next;
//		printk("scull_follow:%d.3\n", i+2);
	}

	return dev_dst;
}

/*
*目前驱动申请TOTAL_BLOCK个链表项,只能容纳 (SCULL_QUANTUN*SCULL_QSET*TOTAL_BLOCK)个字节数据
*/
int scull_open(struct inode *inode,struct file *filp)
{
    printk("scull open\n");
    /* device information 根据打开的不同scull设备来找寻dev info*/
    struct scull_dev *dev = container_of(inode->i_cdev, struct scull_dev, cdev);

    filp->private_data = dev;	/* for other methods */
    /* now trim to 0 the length of the device if open was write-only */
    if((filp->f_flags & O_ACCMODE) == O_WRONLY)
    	scull_trim(dev);

    printk("[open] sem count:%d\n", dev->sem.count);
    return 0;
}

ssize_t scull_write(struct file *filp, const char __user *usr, size_t len, loff_t *off)
{
	printk(KERN_ERR "scull write\n");
    struct scull_dev *dev = filp->private_data;	/* device information */
    
    struct scull_qset *dptr;    //第几个链表项
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset; //该链表项中有多少字节
    int item, s_pos, q_pos, rest;
    ssize_t retval = ENOMEM;

	/* 返回-ERESTART 意味着驱动能够撤销已经对任何用户做出的修改，简单的说就是驱动已经还原状态
	如果驱动无法还原这些状态的话需要返回-EINTR
	*/
    if(down_interruptible(&dev->sem))
    	return -ERESTART;

	 /* 在量子集中寻找链表项、qset索引以及偏移量*/
    item = (long)*off / itemsize;	//第几个链表项
    rest = (long)*off % itemsize;	//具体链表项下的基地址到达off的偏移
	s_pos = rest / quantum;			//用上面的偏移求出第几个原子项
	q_pos = rest % quantum;			//具体原子项下的基地址到达off的偏移
    /* 沿该链表前行,直到正确的链表项位置 */
	dptr = scull_follow(dev, item);
	if(dptr == NULL) {
		printk(KERN_ERR "can not find a block!\n");
		goto out;
	}
	if(!dptr->data) {
		printk(KERN_ERR "kmalloc a qset\n");
		dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
		if(!dptr->data)
			goto out;
		memset(dptr->data, 0, qset * sizeof(char *));
	}
	if(!dptr->data[s_pos]) {
		printk(KERN_ERR "kmalloc a quantum\n");
		dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
		if(!dptr->data[s_pos])
			goto out;
	}

	/* 将数据写入该量子,直到结尾 */
	if(len > quantum - q_pos)
		len = quantum - q_pos;
	if(copy_from_user(dptr->data[s_pos]+q_pos, usr, len)) {
		printk(KERN_ERR "Bad address\n");
		retval = -EFAULT;
		goto out;
	}
	*off += len;
	retval = len;

	/* 更新文件大小 */
	if(dev->total_size < *off)
		dev->total_size = *off;
	
	printk(KERN_ERR " write length:%d\n now block dev size:%d\n", len, (int)dev->total_size);
	
out:
	printk(KERN_ERR "exit write\n\n");
	up(&dev->sem);
	return retval;
}

ssize_t scull_read(struct file *filp, char __user *usr, size_t len, loff_t *off)
{
	printk(KERN_ERR "scull read\n");
    struct scull_dev *dev = filp->private_data;	/* device information */

    struct scull_qset *dptr;    //第几个链表项
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset; //该链表项中有多少字节
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;

    down_interruptible(&dev->sem);

    if(*off >= dev->total_size) {
		printk(KERN_ERR "off:%d  total:%d  off>=total!\n", (int)*off, (int)dev->total_size);
    	goto out;
    }
    if(*off + len > dev->total_size)	//如果读取的位置溢出的话
    	len = dev->total_size - *off;
    /* 在量子集中寻找链表项、qset索引以及偏移量*/
    item = (long)*off / itemsize;	//链表项
    rest = (long)*off % itemsize;	//具体链表项下的基地址到达off的偏移
	s_pos = rest / quantum;			//用上面的偏移求出第几个原子项
	q_pos = rest % quantum;			//具体原子项下的基地址到达off的偏移

	/* 沿该链表前行,直到正确的链表项位置 */
	dptr = scull_follow(dev, item);
	if(dptr == NULL || !dptr->data || !dptr->data[s_pos]) {
		printk(KERN_ERR "can`t find correct place!\n");
		goto out;	//don`t fill holes
	}

	/* 读取该量子的数据直到结尾 */
	if(len > quantum - q_pos)
		len = quantum - q_pos;

	if(copy_to_user(usr, dptr->data[s_pos] + q_pos, len)) {
		printk(KERN_ERR "Bad address\n");
		retval = -EFAULT;
		goto out;
	}
	//更新文件指针
	*off += len;
	retval = len;
	printk(KERN_ERR " read length:%d\n now block dev size:%d\n", len, (int)dev->total_size);

out:
	printk(KERN_ERR "exit read\n\n");
	up(&dev->sem);
	return retval;
}

int scull_release(struct inode *inode, struct file *file)
{
    printk("close\n");
    return 0;

}

struct file_operations scull_fops=
{
    .owner=THIS_MODULE,
    .open = scull_open,
    .write = scull_write,
    .read = scull_read,
    .release = scull_release,
};

static int scull_setup_cdev(struct scull_dev *dev, int index)
{
	int err;
	dev_t dev_no;
    dev_no = MKDEV(Major, Minor+index);
    if(dev_no>0)
        err = register_chrdev_region(dev_no,1,"scull");    
    else
        alloc_chrdev_region(&dev_no,0,1,"scull");

    cdev_init(&dev->cdev,&scull_fops);
    dev->cdev.owner=THIS_MODULE;
    dev->cdev.ops = &scull_fops;
    cdev_add(&dev->cdev,dev_no,1);
    if(err<0) {
		printk(KERN_ERR "Error %d adding scull%d", err, index);
        return err;
    }
    return 0;
}


static int __init scull_init(void)
{
	int i=0;

    for(i=0; i< scull_nr_devs; i++)
    {
		//申请下block device
		scull_devices[i].quantum = SCULL_QUANTUN;
		scull_devices[i].qset = SCULL_QSET;
		init_MUTEX(&scull_devices[i].sem);
		scull_setup_cdev(&scull_devices[i], i);
    }
    printk("hello scull\n");
    return 0;
}

static void __exit scull_exit(void)
{
	dev_t dev_no;
	int index=0;
    for(index=0; index< scull_nr_devs; index++)
    {
	    dev_no = MKDEV(Major, Minor+index);
	    scull_trim(&scull_devices[index]);
	    kfree(scull_devices[index].data);
	    unregister_chrdev_region(dev_no,1);
	    cdev_del(&scull_devices[index].cdev);
	    printk(KERN_ERR "scull_%d exit\n", index);
	}

}

module_init(scull_init);
module_exit(scull_exit);


MODULE_AUTHOR("Jin Fa");
MODULE_DESCRIPTION("scull");
MODULE_LICENSE("GPL");
MODULE_VERSION("V1.0");

