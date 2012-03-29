/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 * Copyright (C) 2012 Gautam Akiwate <gautam.akiwate@gmail.com>
 * Copyright (C) 2012 Shravan Aras <shravan@kushraho.com>
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/module.h>
#include <linux/kobject.h>

#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "volumes.h"
/*
 * struct btrfs_kobject is defined to allow kobjects to be created under 
 * btrfs_kset and also the kobjects defined under btrfs_kset.
 * 
 * The #define to_btrfs_kobject(x) is used to get the pointer to the structure 
 * using the pointer to a member of the structure.
 * 
 * v1 Comment: As of now the struct contains just the kobject and a void pointer
 * The idea is that the void pointer can be put to use in certain cases without 
 * complicating the code.
 */
struct btrfs_kobject {
	struct kobject kobj;
	void *ptr;
};
#define to_btrfs_kobject(x) container_of(x, struct btrfs_kobject, kobj)

/* 
 * btrfs_kobject_attr lists the attributes for the struct btrfs_kobject
 * The attribute list contains the usual struct attribute and also two 
 * defined functions for showing and storing. This can be added on to.
 *
 * The #define to_btrfs_kobject_attr(x) is used to get the pointer to the 
 * structure using the pointer to a member of the structure.
 * 
 */
 
struct btrfs_kobject_attr {
	struct attribute attr;
	ssize_t (*show)(struct btrfs_kobject *kobj, \
		struct btrfs_kobject_attr *attr, char *buf);
	ssize_t (*store)(struct btrfs_kobject *kobj, \
		struct btrfs_kobject_attr *attr, const char *buf, size_t len);
};

#define to_btrfs_kobject_attr(x) container_of(x, struct btrfs_kobject_attr,attr)


 
struct btrfs_device_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj, \
			struct btrfs_device_attr *attr, char *buf);
	ssize_t (*store)(struct kobject *kobj, \
			struct btrfs_device_attr *attr, const char *buf, size_t len);
};

#define to_btrfs_device_attr(x) container_of(x, struct btrfs_device_attr,attr)

/*
 * static ssize_t btrfs_kobject_attr_show and 
 * static ssize_t btrfs_kobject_attr_store is defined as the default show and  
 * store functions for btrfs_kobject_attr. These functions will be called by 
 * sysfs by default the respective function is called by the user on a sysfs 
 * file associated with the kobjects we have registered.
 */
static ssize_t btrfs_kobject_attr_store(struct kobject *kobj, \
			     struct attribute *attr, const char *buf, size_t len)
{
	struct btrfs_kobject_attr *btrfs_attr;
	struct btrfs_kobject *btrfs_kobj;

	btrfs_attr = to_btrfs_kobject_attr(attr);
	btrfs_kobj = to_btrfs_kobject(kobj);

	if (!btrfs_attr->store)
		return -EIO;

	return btrfs_attr->store(btrfs_kobj, btrfs_attr, buf,len);
}

static ssize_t btrfs_kobject_attr_show(struct kobject *kobj, \
				struct attribute *attr, char *buf)
{
	struct btrfs_kobject_attr *btrfs_attr;
	struct btrfs_kobject *btrfs_kobj;

	btrfs_attr = to_btrfs_kobject_attr(attr);
	btrfs_kobj = to_btrfs_kobject(kobj);

	if (!btrfs_attr->show)
		return -EIO;

	return btrfs_attr->show(btrfs_kobj, btrfs_attr, buf);
}


static ssize_t btrfs_device_attr_store(struct kobject *kobj, \
			     struct attribute *attr, const char *buf, size_t len)
{
	struct btrfs_device_attr *btrfs_attr;

	btrfs_attr = to_btrfs_device_attr(attr);
	if (!btrfs_attr->store)
		return -EIO;

	return btrfs_attr->store(kobj,btrfs_attr,buf,len);
}

static ssize_t btrfs_device_attr_show(struct kobject *kobj, \
				struct attribute *attr, char *buf)
{

	struct btrfs_device_attr *btrfs_attr;

	btrfs_attr = to_btrfs_device_attr(attr);
	if (!btrfs_attr->show)
		return -EIO;

	return btrfs_attr->show(kobj,btrfs_attr,buf);
}

/*
 * Our next goal is to define btrfs_ktype ie. a kobject_type or ktype in short. 
 * A kobject_type needs three things:
 * sysfs_ops: Default operations associated with the ktype. For btrfs_ktype 
 * 		it has been defined as btrfs_sysfs_ops 
 * release: Function to free the kobject memory once used. For btrfs_ktype 
 * 		defined as btrfs_kobject_release
 * default_attrs: These are the default attributes associated with every kobject
 * 		that will be created. For btrfs_ktype defined as btrfs_default_attrs
 */


/* 
 * btrfs_sysfs_ops is the sysfs_ops which will be associated with btrfs_ktype
 */

static const struct sysfs_ops btrfs_sysfs_ops = {
	.store = btrfs_kobject_attr_store,
	.show = btrfs_kobject_attr_show,
};

static const struct sysfs_ops btrfs_device_sysfs_ops = {
	.store = btrfs_device_attr_store,
	.show = btrfs_device_attr_show,
};

static void btrfs_kobject_release(struct kobject *kobj)
{
	struct btrfs_kobject *tmp_kobj;

	tmp_kobj = to_btrfs_kobject(kobj);
	kfree(tmp_kobj);
}

/*
 * This release function will be used with dynamically 
 * created sysfs device entries. This kobject will 
 * point to the superblock kobject and hence we cannot 
 * free it. Instead we use kobject_put that will decrement
 * the kref count and free it if it reaches zero.
 */
static void btrfs_device_release(struct kobject *kobj)
{
	kobject_put(kobj);
}

/*
 * The btrfs_sysfs_ops defines two default functions. These functions in turn 
 * call the default show and store functions of the attributes that has been 
 * passed to the function. 
 * 
 * btrfs_attr_show and btrfs_attr_store are functions defined to be the show 
 * and store functions for use to define attributes of type btrfs_kobject_attr
 *
 *
 * Example Store and Show Functions
	static ssize_t btrfs_attr_show(struct btrfs_kobject *btrfs_kobj, \
		struct btrfs_kobject_attr *attr, char *buf)
	{
		return sprintf(buf, "%d\n", btrfs_kobj->val);
	}

	static ssize_t btrfs_attr_store(struct btrfs_kobject *btrfs_kobj, \
		struct btrfs_kobject_attr *attr, const char *buf, size_t len)
	{
		sscanf(buf, "%du", &btrfs_kobj->val);
		return strlen(buf);
	}
 */
 
/*
 * Define the default attributes that need to be a part of every btrfs_kobject
 * A attribute can be defined easily by using the macro
 * #define BTRFS_ATTR(_name,_mode,_show,_store)
 * 
 * Note: If you want to add an attribute define the attribute and add it to the
 * btrfs_default_attrs array.
 * 
 * v1 comment: As of now no need for a default attribute in all kobject. But 
 * to show usage a dummy attribute is defined and added to the default list
 */

#define BTRFS_ATTR(_name,_mode,_show,_store) \
struct btrfs_kobject_attr btrfs_attr_##_name = __ATTR(_name,_mode,_show,_store)

#define BTRFS_DEVICE_ATTR(_name,_mode,_show,_store) \
struct btrfs_device_attr btrfs_attr_##_name = __ATTR(_name,_mode,_show,_store)

#define ATTR_LIST(name) &btrfs_attr_##name.attr

/*
 * Example Usage::
 *
	static BTRFS_ATTR(dummy,0666,btrfs_attr_show, btrfs_attr_store);
 
	static struct attribute *btrfs_default_attrs[] = {
		ATTR_LIST(dummy),
		NULL,
	};


 * static struct kobj_type btrfs_ktype is defined to be the ktype of 
 * btrfs_kobject. It includes the implementations of sysfs_ops, release
 * and definition of btrfs_attrs for the same.
 *
 * REFERENCE DEFINITION
	static struct kobj_type btrfs_ktype = {
	.sysfs_ops = &btrfs_sysfs_ops,
	.release = btrfs_kobject_release,
	.default_attrs = btrfs_default_attrs,
};
 */
	

/* /sys/fs/btrfs/ entry */
static struct kset *btrfs_kset;

/*
 * The next goal is to implement the first level of directory structure under
 * /sys/fs/btrfs ie. to create btrfs_kobject under btrfs_kset
 * 
 * /sys/fs/btrfs/
 *		 	 |-> devices
 *			 |-> health
 *			 |-> info
 *
 * To add to the above list define 
 * static struct btrfs_kobject *<var-name> and then call in btrfs_init_sysfs
 * static struct btrfs_kobject *btrfs_kobject_create the return value of which
 * is assigned to the above.
 * static void btrfs_kobject_destroy is also needed to destroy the kobjects
 */

/*
 * Declare btrfs_kobject(s) that will be under btrfs_kset to form the first
 * level of directories under /sys/fs/btrfs
 */
static struct btrfs_kobject *btrfs_devices;
static struct btrfs_kobject *btrfs_health;
static struct btrfs_kobject *btrfs_info;

/*
 * The next goal is to define the pre-requisites needed for defining ktype(s)
 * for the first level of directories. This needs to be done by defining
 * 	1. sysfs_ops	
 *	2. release function
 * 	3. default_attrs
 * We have defined a generic sysfs_ops and release functions for btrfs_kobjects 
 * and in nearly all the case btrfs_sysfs_ops and btrfs_kobject_release 
 * should suffice.
 */

/*
 * Setup for /sys/fs/btrfs/info Directory
 */
static BTRFS_ATTR(num_devices,0444,NULL,NULL);
static struct attribute *btrfs_info_default_attrs[] = {
	ATTR_LIST(num_devices),
	NULL,
};

static struct kobj_type btrfs_ktype_info = {
	.sysfs_ops = &btrfs_sysfs_ops,
	.release = btrfs_kobject_release,
	.default_attrs = btrfs_info_default_attrs,
};

/*
 * Setup for /sys/fs/btrfs/health Directory
 * 
 * v1 Comment: Filling up with dummy variable
 */
static BTRFS_ATTR(dummy,0444,NULL,NULL);
static struct attribute *btrfs_health_default_attrs[] = {
	ATTR_LIST(dummy),
	NULL,
};
static struct kobj_type btrfs_ktype_health = {
	.sysfs_ops = &btrfs_sysfs_ops,
	.release = btrfs_kobject_release,
	.default_attrs = btrfs_health_default_attrs,
};

/*
 * Setup for /sys/fs/btrfs/devices Directory
 * 
 */
static BTRFS_ATTR(dummy1,0444,NULL,NULL);
static struct attribute *btrfs_devices_default_attrs[] = {
	ATTR_LIST(dummy1),
	NULL,
};
static struct kobj_type btrfs_ktype_devices = {
	.sysfs_ops = &btrfs_sysfs_ops,
	.release = btrfs_kobject_release,
	.default_attrs = btrfs_devices_default_attrs,
};
static struct attribute *btrfs_device_dir_default_attrs[] = {
	NULL,
};
static struct kobj_type btrfs_ktype_device_dir = {
	.sysfs_ops = &btrfs_sysfs_ops,
	.release = btrfs_kobject_release,
	.default_attrs = btrfs_device_dir_default_attrs,
};

/*
 * Setup for /sys/fs/btrfs/devices/<device> Directory
 */

static struct btrfs_device* device_stats(struct kobject *btrfs_kobj)
{
	struct btrfs_fs_info *container_fs_info;
	struct btrfs_super_block *device_super_copy;
	struct btrfs_dev_item *device_item;
	__le64 device_id;
	u8 *device_uuid;
	u8 *device_fsid;
	struct btrfs_root *device_root;
	struct btrfs_device *device;

	container_fs_info = container_of(btrfs_kobj,struct btrfs_fs_info,super_kobj);
	device_super_copy = container_fs_info->super_copy;
	device_item = &device_super_copy->dev_item;
	
	device_id = device_item->devid;
	device_uuid = device_item->uuid;
	device_fsid = device_item->fsid;

	device_root = container_of(&container_fs_info,struct btrfs_root,fs_info);

	device = btrfs_find_device(device_root,device_id,device_uuid,device_fsid);

	return device;	
}

static ssize_t device_write_io_err_show(struct kobject *btrfs_kobj, \
	struct btrfs_device_attr *attr, char *buf)
{
	struct btrfs_device *device;
	device = device_stats(btrfs_kobj);
	return sprintf(buf, "%d\n", device->cnt_write_io_errs.counter);
}

static ssize_t device_read_io_err_show(struct kobject *btrfs_kobj, \
	struct btrfs_device_attr *attr, char *buf)
{
	struct btrfs_device *device;
	device = device_stats(btrfs_kobj);
	return sprintf(buf, "%d\n", device->cnt_read_io_errs.counter);
}

static ssize_t device_flush_io_err_show(struct kobject *btrfs_kobj, \
	struct btrfs_device_attr *attr, char *buf)
{
	struct btrfs_device *device;
	device = device_stats(btrfs_kobj);
	return sprintf(buf, "%d\n", device->cnt_flush_io_errs.counter);
}

static ssize_t device_corruption_err_show(struct kobject *btrfs_kobj, \
	struct btrfs_device_attr *attr, char *buf)
{
	struct btrfs_device *device;
	device = device_stats(btrfs_kobj);
	return sprintf(buf, "%d\n", device->cnt_corruption_errs.counter);
}

static ssize_t device_generation_err_show(struct kobject *btrfs_kobj, \
	struct btrfs_device_attr *attr, char *buf)
{
	struct btrfs_device *device;
	device = device_stats(btrfs_kobj);
	return sprintf(buf, "%d\n", device->cnt_generation_errs.counter);
}


static BTRFS_DEVICE_ATTR(uuid,0444,NULL,NULL);
static BTRFS_DEVICE_ATTR(label,0444,NULL,NULL);
static BTRFS_DEVICE_ATTR(cnt_write_io_errs,0444,device_write_io_err_show,NULL);
static BTRFS_DEVICE_ATTR(cnt_read_io_errs,0444,device_read_io_err_show,NULL);
static BTRFS_DEVICE_ATTR(cnt_flush_io_errs,0444,device_flush_io_err_show,NULL);
static BTRFS_DEVICE_ATTR(cnt_corruption_errs,0444,device_corruption_err_show,NULL);
static BTRFS_DEVICE_ATTR(cnt_generation_errs,0444,device_generation_err_show,NULL);

static struct attribute *btrfs_device_default_attrs[] = {
	ATTR_LIST(uuid),
	ATTR_LIST(label),
	ATTR_LIST(cnt_write_io_errs),
	ATTR_LIST(cnt_read_io_errs),
	ATTR_LIST(cnt_flush_io_errs),
	ATTR_LIST(cnt_corruption_errs),
	ATTR_LIST(cnt_generation_errs),
	NULL,
};
static struct kobj_type btrfs_ktype_device = {
	.sysfs_ops = &btrfs_device_sysfs_ops,
	.release = btrfs_device_release,
	.default_attrs = btrfs_device_default_attrs,
};

/*
 * static struct btrfs_kobject *btrfs_kobject_create(const char *name) is used
 * to create btrfs_kobject(s) under btrfs_kset. 
 * 
 */
static struct btrfs_kobject *btrfs_kobject_create(const char *name, \
				struct kobj_type ktype, struct btrfs_kobject *btrfs_parent)
{
	struct btrfs_kobject *btrfs_kobj;
	struct kobject *parent_kobj;
	int ret;
	parent_kobj = NULL;
	/* Allocate memory for object */
	btrfs_kobj = kzalloc(sizeof(*btrfs_kobj), GFP_KERNEL);
	if (!btrfs_kobj)
		return NULL;
	if(btrfs_parent != NULL) 
		parent_kobj = &btrfs_parent->kobj;
	else
		btrfs_kobj->kobj.kset = btrfs_kset;
	/*
	 * Initialize and add the kobject to the kernel.  All the default files
	 * will be created here.  As we have already specified a kset for this
	 * kobject, we don't have to set a parent for the kobject, the kobject
	 * will be placed beneath that kset automatically.
	 */
	ret = kobject_init_and_add(&btrfs_kobj->kobj, &ktype, \
					NULL,"%s", name);
	if (ret) {
		kobject_put(&btrfs_kobj->kobj);
		return NULL;
	}
	/*
	 * If creation is successful we need to send an uevent informing that the 
	 * kobject was added to the system.
	 */
	printk(KERN_INFO "btrfs: About to notify the userspace abt this\n");
	//kobject_uevent(&btrfs_kobj->kobj, KOBJ_ADD);

	return btrfs_kobj;
}

static void btrfs_kobject_destroy(struct btrfs_kobject *btrfs_kobj)
{
	kobject_put(&btrfs_kobj->kobj);

}
/* 
 * btrfs_init_sysfs is used to initialize the btrfs sysfs implementation. 
 * This function is used to initialize btrfs_kset and also the btrfs_kobject
 * that come under it. To add an entry declare a btrfs_kobject and 
 * call btrfs_kobject_create. Entries should not be trivially added
 * Make sure that adequate error handling is done if entry is added.
 *
 */

int btrfs_static_init_sysfs(void)
{	
	btrfs_devices = btrfs_kobject_create("devices",btrfs_ktype_device_dir,NULL);
	if(!btrfs_devices)
		goto btrfs_devices_error;
	btrfs_health = btrfs_kobject_create("health",btrfs_ktype_health,NULL);
	if(!btrfs_health)
		goto btrfs_health_error;
	btrfs_info = btrfs_kobject_create("info",btrfs_ktype_info,NULL);
	if(!btrfs_info)
		goto btrfs_info_error;
	//btrfs_static_init_info_sysfs();
	return 0;

btrfs_info_error:
	btrfs_kobject_destroy(btrfs_info);
btrfs_health_error:
	btrfs_kobject_destroy(btrfs_health);
btrfs_devices_error:
	return -EINVAL;
}

int btrfs_init_sysfs(void)
{
	btrfs_kset = kset_create_and_add("btrfs", NULL, fs_kobj);
	if (!btrfs_kset)
		return -ENOMEM;
	return btrfs_static_init_sysfs();
}

/* Dynamic functions which create btrfs objects. */
/* Function to create devices and place them under the devices
 * directory.
 */
int btrfs_create_device(struct kobject *super_kobj, u8 *label)
{
	int ret;
	if(super_kobj->state_initialized) {
		kobject_get(super_kobj);
	}
	else {
		ret = kobject_init_and_add(super_kobj,&btrfs_ktype_device, \
								&btrfs_devices->kobj,"%s",label);
		if (ret) {
			kobject_put(super_kobj);
			return -EINVAL;
		}
	}
	return 0;
}

/* Seek and Destroy. */
void btrfs_kill_device(struct kobject *super_kobj)
{
	kobject_put(super_kobj);
}

void btrfs_exit_sysfs(void)
{
	btrfs_kobject_destroy(btrfs_devices);
	btrfs_kobject_destroy(btrfs_health);
	btrfs_kobject_destroy(btrfs_info);
	kset_unregister(btrfs_kset);
}

