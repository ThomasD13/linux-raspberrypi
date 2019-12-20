/*
 * helloworld.c
 *
 * This driver uses two frameworks:
 *  - platform device (to use devicetree to setup memory)
 *  - misc framework: To provide a interface to userspace
 *  Created on: Dec 2, 2019
 *      Author: thomas
 */

#include <linux/module.h>
#include <linux/fs.h> //For register character device
#include <linux/ioport.h> //For allocating memory
#include <uapi/asm-generic/errno-base.h> //For error codes
#include <linux/iomap.h> //For mapping
#include <linux/io.h> //For readb
#include <linux/device.h>
#include <linux/of.h> // of_property_read_string()
#include <linux/platform_device.h>
#include <linux/of_address.h> //of_address_to_resource
#include <linux/miscdevice.h> //Usage of misc framework
#include <linux/uaccess.h> // copy_to_user, etc...
#include <linux/kgdb.h>
#include <asm/io.h> // for ioremap_cache

int Major;
void __iomem * p_MappedRAM;


static int my_ipcmemory_open(struct inode *inode, struct file *file)
{
	pr_info("my_ipcmemory_open() is called\n");
	return 0;
}

static int my_ipcmemory_close(struct inode *inode, struct file *file)
{
	pr_info("my_ipcmemory_close() is called\n");
	return 0;
}

static long my_ipcmemory_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	//I don't consider that this call could be suspended by accessing swapped out memory pages
	pr_info("my_ipcmemory_ioctl() is called. cmd = %d, arg = %ld\ņ", cmd, arg);
	return 0;
}

ssize_t my_ipcmemory_write(struct file *file, const char __user * buf, size_t count, loff_t *ppos)
{
	unsigned long amountNotCopied;
	unsigned long long actualStart;
	unsigned long long actualEnd;
	pr_info("my_ipcmemory_write: Count: 0x%lx ppos: 0x%llx\n", count, *ppos);

	actualStart = p_MappedRAM + *ppos;
	actualEnd = actualStart + count;

	//Check if Start and End is inside the range
	//I assume p_MappedRAM is correct
	if((count + *ppos) >= 0x10000000) //TODO: Read this from DTS
	{
		pr_err("my_ipcmemory_write: Userspace requested memory outside: Count %lx ppos %lx",
				count, *ppos);
		return -EFAULT;
	}

	//I don't consider that this call could be suspended by accessing swapped out memory pages

	amountNotCopied = copy_from_user((void*)actualStart, buf, count);

	if(amountNotCopied != 0)
	{
		pr_err("my_ipcmemory_write: Could not write 0x%lx from 0x%lx bytes!\n", amountNotCopied, count);
		return -EFAULT;
	}
	pr_info("my_ipcmemory_write: Written 0x%lx bytes\n", count);
	*(ppos) += (count - amountNotCopied);

	return count-amountNotCopied;
}

/*
 * Returns number of bytes which are actually read.
 * */
ssize_t my_ipcmemory_read( struct file *filp, char __user *buf, size_t count, loff_t *ppos)
{
	unsigned long amountNotCopied;
	unsigned long long actualStart;
	unsigned long long actualEnd;
	pr_info("my_ipcmemory_read: Count: 0x%lx ppos: 0x%llx\n", count, *ppos);

	actualStart = p_MappedRAM + *ppos;
	actualEnd = actualStart + count;

	//Check if Start and End is inside the range
	//I assume p_MappedRAM is correct
	if((count + *ppos) >= 0x10000000) //TODO: Read this from DTS
	{
		pr_err("my_ipcmemory_read: Userspace requested memory outside: Count %lx ppos %lx",
				count, *ppos);
		return -EFAULT;
	}

	amountNotCopied = copy_to_user(buf, p_MappedRAM, count);
	if(amountNotCopied != 0)
	{
		pr_err("my_ipcmemory_read: Could not read 0x%lx from 0x%lx bytes!\n", amountNotCopied, count);
		return -EFAULT;
	}
	pr_info("my_ipcmemory_read: Returned 0x%lx bytes\n", count);
	*(ppos) += (count - amountNotCopied);


	return count-amountNotCopied;

}

static const struct file_operations my_ipcmemory_fops = {
		//.owner = THIS_MODULE,
		.open = my_ipcmemory_open,
		.release = my_ipcmemory_close,
		.unlocked_ioctl = my_ipcmemory_ioctl,
		.write = my_ipcmemory_write,
		.read = my_ipcmemory_read,

};


static struct miscdevice ipcmemory_miscdevice = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = "myIPCMemory",
		.fops = &my_ipcmemory_fops,
};

static int __init ipcmemory_probe(struct platform_device *pdev)
{
	/*
	 * This function will read the memory requirements of DTS node
	 * which is compatible with "IPCMemory".
	 * After that, it will allocate the defined memory region and
	 * map it into virtual kernel space.
	 * Furthermore it registers a misc driver, which creates nodes to
	 * provide a interface to userspace. (open/read/write/..)
	 * */

	//struct device_node *dtsNode;	//Reading some information from DTS nodes, just for debugging

	struct resource res;
	int rc;

	//kgdb_breakpoint();
	//This only gets called if DTS compatible string matches and
	//this driver are registered at bus "simple-bus"
	pr_info("Called probe of IPCMEMORY\n");

	//Lets get some information from the devicetree file
	pr_info("platform_device->name: %s\n", pdev->name);
	pr_info("platform_device->dev.driver->name: %s\n", pdev->dev.driver->name);


	pr_info("platform_device->dev.of_node->full_name: %s\n", pdev->dev.of_node->full_name);
	pr_info("platform_device->dev.of_node->name: %s\n", pdev->dev.of_node->name);

	res = *(platform_get_resource(pdev, IORESOURCE_MEM, 0));
	pr_info("Mapping %llx with size %llx", res.start, resource_size(&res));

	/*int lengthProperty;
	struct property* p_prop;
	p_prop = of_find_property(pdev->dev.of_node, "reg", &lengthProperty);

	if(lengthProperty > 0)
	{
		pr_info("Find Property: Name: %s\n ", p_prop->name);
		pr_info("Find Property: Value: %s\n ", p_prop->value);
	}

	dtsNode = of_parse_phandle(pdev->dev.of_node, "reg", 0);
	if(!dtsNode)
	{
		pr_err("Could not get \"reg\" from %s\n", pdev->name);
		of_node_put(dtsNode);
		return -1;
	}

	rc = of_address_to_resource(dtsNode, 0, &res);
	//Finish with dtsNode
	of_node_put(dtsNode);

	if(rc)
	{
		pr_err("Could not get struct resource!\n");
		return -1;
	}


	//Try to allocate some memory
	p_res = request_mem_region((resource_size_t)0x40000000, (resource_size_t)0x10000000, "foobar");
	if(p_res->start != 0x40000000 && p_res->end != (0x40000000 + 0x10000000))
	{
		pr_err("Allocating memory failed: Start: %llx End: %llx\n", p_res->start, p_res->end);
		return -ENOMEM;
	}
	*/
	//p_MappedRAM = devm_ioremap_nocache(&pdev->dev, res.start, resource_size(&res));
	p_MappedRAM = ioremap_cache(res.start, resource_size(&res));
	pr_info("Value of p_MappedRAM: %llx\n", p_MappedRAM);
	//Try to read the first 8 Bytes (2 UInts)
	//For this we need to translate physical to virtual address

	unsigned int value1 = readw(p_MappedRAM);
	unsigned int value2 = readw(p_MappedRAM + 4);
	pr_info("Value 1: %x\n", value1);
	pr_info("Value 2: %x\n", value2);

	//register misc device to create interface to userspace
	rc = misc_register(&ipcmemory_miscdevice);
	if(rc != 0)
	{
		pr_err("Could not register misc device ipcmemory\n");
		return rc;
	}

	pr_info("ipcmemory: got minor %i\n", ipcmemory_miscdevice.minor);
	return 0;
}

static int __exit ipcmemory_remove(struct platform_device *pdev)
{
	pr_info("Called REMOVE of IPCMEMORY\n");
	return 0;
}

static const struct of_device_id my_of_ids[] = {
	{ .compatible = "IPCMemory"},
	{},
};
MODULE_DEVICE_TABLE(of, my_of_ids);

static struct platform_driver ipcmemory_platform_driver = {
	.probe = ipcmemory_probe,
	.remove = ipcmemory_remove,
	.driver = {
		.name = "ipcmemory",
		.of_match_table = my_of_ids,
		.owner = THIS_MODULE,
	}
};

static int __init hello_init(void)
{
	int retVal;
	struct file_operations fops;
	struct resource* p_res;
	//struct device dev;

	pr_info("Hello world init\n");
	pr_info("Register platform driver. Compatible name: %s\n", ipcmemory_platform_driver.driver.of_match_table->compatible);

	retVal = platform_driver_register(&ipcmemory_platform_driver);
	pr_info("Returnvalue for platform driver register: %d\n", retVal);

	return 0;
}



static void __exit hello_exit(void)
{
	/*
	iounmap(p_MappedRAM);
	release_mem_region(0x40000000, 0x10000000);
	unregister_chrdev(Major, "IPCMemory");
	*/
	platform_driver_unregister(&ipcmemory_platform_driver);
	misc_deregister(&ipcmemory_miscdevice);
	pr_info("Unregister platform driver and misc device\n");
}

/*
 * This function is not called. Just to check if this generates a .text section in elf-file
 * */
void uselessfunction(void)
{
	int i = 5;
	i++;
	pr_info("This is useless! %d", i);
}



module_init(hello_init);
module_exit(hello_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Thomas Dorsch <t.dorsch@ibgndt.de>");
MODULE_DESCRIPTION("This is a print out Hello World module");
