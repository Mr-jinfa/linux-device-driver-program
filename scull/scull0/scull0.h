#ifndef __scull0_H
#define __scull0_H

#define SCULL_QUANTUN 4000	//量子大小
#define SCULL_QSET    1000	//qset中由多少个量子组成

struct scull_qset {
	void **data;
	struct scull_qset *next;
};

struct scull_dev 
{
	struct scull_qset *data;	//指向第一个量子集的指针
	int quantum;				//当前量子的大小
	int qset;					//当前数组的大小
	unsigned long total_size;	//保存在其中的数据总量
	unsigned int access_key;	//由sculluid和scullpriv使用
	struct semaphore sem;		//互斥信号量
	struct cdev cdev;			//字符结构
};

#define TOTAL_BLOCK 	3





#endif