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
dev_t dev_no;
int ret;
static struct scull_dev *dev;

int scull_trim(void)
{
    struct scull_qset *next, *dptr;
//    printk("1\n");
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
    dev->quantum = SCULL_QUANTUN;
    dev->qset = SCULL_QSET;
    dev->data = NULL;
//    printk("7\n");
    return 0;
}

/*参数说明
*item:代表第几个scull_device设备
*PS:scull_device链表头由全局变量dev->data标识.
*/
struct scull_qset *scull_follow(int item)
{
	struct scull_qset *dev_dst=NULL;	//第几个链表项	
	int i=0;
	if(dev->data == NULL)
	{
		printk(KERN_ERR "can not follow\n");
		return NULL;
	}
	printk("scull_follow\n");
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
    return 0;
}

ssize_t scull_write(struct file *filp, const char __user *usr, size_t len, loff_t *off)
{
//	printk("scull write\n");
    struct scull_qset *dptr;    //第几个链表项
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset; //该链表项中有多少字节
    int item, s_pos, q_pos, rest;
    ssize_t retval = ENOMEM;

    if(down_interruptible(&dev->sem))
    	return -ERESTART;

	 /* 在量子集中寻找链表项、qset索引以及偏移量*/
    item = (long)*off / itemsize;	//第几个链表项
    rest = (long)*off % itemsize;	//具体链表项下的基地址到达off的偏移
	s_pos = rest / quantum;			//用上面的偏移求出第几个原子项
	q_pos = rest % quantum;			//具体原子项下的基地址到达off的偏移
    /* 沿该链表前行,直到正确的链表项位置 */
	dptr = scull_follow(item);
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
	
	printk(" write length:%d\n now block dev size:%d\n", len, dev->total_size);
	up(&dev->sem);
	return retval;
out:
	up(&dev->sem);
	return retval;
}

ssize_t scull_read(struct file *filp, char __user *usr, size_t len, loff_t *off)
{
	printk("scull read\n");
    struct scull_qset *dptr;    //第几个链表项
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset; //该链表项中有多少字节
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;
//    if(down_interruptible(&dev->sem));
//        return -ERESTARTSYS;
    if(*off >= dev->total_size) {
		printk(KERN_ERR "off:%d  total:%d  off>=total!\n", *off, dev->total_size);
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
	dptr = scull_follow(item);
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
//	up(&dev->sem);
	return retval;
out:
//	up(&dev->sem);
	return retval;
}

int scull_release(struct inode *inode, struct file *file)
{
    printk("close\n");
    return 0;

}

struct file_operations fops=
{
    .owner=THIS_MODULE,
    .open = scull_open,
    .write = scull_write,
    .read = scull_read,
    .release = scull_release,
};
static int __init scull_init(void)
{
	struct scull_qset* qset = NULL;
	int i=0;

    dev = kmalloc(sizeof(struct scull_dev), GFP_KERNEL);
    memset(dev, 0, sizeof(struct scull_dev));
    dev_no = MKDEV(Major,Minor);
    if(dev_no>0)
    {
        ret = register_chrdev_region(dev_no,1,"scull");    
    }
    else
    {
        alloc_chrdev_region(&dev_no,0,1,"scull");
    }
    if(ret<0)
    {
        return ret;
    }
    cdev_init(&dev->cdev,&fops);
    dev->cdev.owner=THIS_MODULE;
    cdev_add(&dev->cdev,dev_no,1);
    
	//先清空下block device
	scull_trim();
	//申请下block device
	dev->quantum = SCULL_QUANTUN;
	dev->qset = SCULL_QSET;
	dev->data = NULL;
	printk(KERN_ERR "\thello\n");
	/* 头插法 */
	for(i=0; i<TOTAL_BLOCK; i++)
	{
		qset = kmalloc(sizeof(struct scull_qset), GFP_KERNEL); //初始化第一个链表项
		memset(qset, 0, sizeof(struct scull_qset));
		if(dev->data != NULL)
			qset = dev->data->next;
		dev->data = qset;
	}	 
	
//	  dev->data->data = kmalloc(sizeof(char *) * dev->qset, GFP_KERNEL);	//初始化第一个qset PS:数组名等于数组第一个元素地址
//	  memset(dev->data->data, 0, dev->qset * sizeof(char *));
	sema_init(&dev->sem, TOTAL_BLOCK);
    printk("hello scull\n");
    return 0;
}

static int __exit scull_exit(void)
{
    scull_trim();
    
    kfree(dev);
    unregister_chrdev_region(dev_no,1);
    cdev_del(&dev->cdev);
    printk("scull exit\n");
    return 0;
}

module_init(scull_init);
module_exit(scull_exit);


MODULE_AUTHOR("Jin Fa");
MODULE_DESCRIPTION("scull");
MODULE_LICENSE("GPL");
MODULE_VERSION("V1.0");

