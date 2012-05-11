/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
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
 * struct btrfs_kobject is defined to allow kobjects to be created and defined
 * under btrfs_kset.
 *
 * The #define to_btrfs_kobject(x) is used to get the pointer to the structure 
 * using the pointer to a member of the structure.
 *
 * v1 Comment:As of now the struct contains just the kobject and a void pointer
 * The idea is that the void pointer can be put to use in certain cases without 
 * complicating the code.
 */

struct btrfs_kobject{
        struct kobject kobj;
	struct completion btrfs_kobj_unregister;
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
	int offset;
};
#define to_btrfs_device_attr(x) container_of(x, struct btrfs_device_attr,attr)
#define to_dev_kobj(x) container_of(x,struct btrfs_device,device_kobj);

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
 * Our next goal is to define btrfs_ktype 
 * A kobject_type needs three things:
 * sysfs_ops: Default operations associated with the ktype. For btrfs_ktype 
 * 		it has been defined as btrfs_sysfs_ops 
 * release: Function to clean up. Defined as btrfs_kobject_release
 * default_attrs: These are the default attributes associated with every 
 *		kobject that will be created. For btrfs_ktype defined as 
 * 		btrfs_default_attrs
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
	/*
	 * We are using global static btrfs_kobject definitions
	 * so we don't have to allocate memory dynamically and hence
	 * not free it also. However, in case we switch to dynamically
	 * created btrfs_kobject then the code given below should be used
	 */
	 struct btrfs_kobject *tmp_kobj;
	 tmp_kobj = to_btrfs_kobject(kobj);
	 complete(&tmp_kobj->btrfs_kobj_unregister);
}

static void btrfs_device_release(struct kobject *kobj)
{
	struct btrfs_device *device;
	device = to_dev_kobj(kobj);
	complete(&device->btrfs_device_unregister);
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
 *	static ssize_t btrfs_attr_show(struct btrfs_kobject *btrfs_kobj, \
 *		struct btrfs_kobject_attr *attr, char *buf)
 *	{
 *		return sprintf(buf, "%d\n", btrfs_kobj->val);
 *	}
 *
 *	static ssize_t btrfs_attr_store(struct btrfs_kobject *btrfs_kobj, \
 *		struct btrfs_kobject_attr *attr, const char *buf, size_t len)
 *	{
 *		sscanf(buf, "%du", &btrfs_kobj->val);
 *		return strlen(buf);
 *	}
 */

#define BTRFS_ATTR(_name,_mode,_show,_store) \
struct btrfs_kobject_attr btrfs_attr_##_name = __ATTR(_name,_mode,_show,_store)

#define ATTR_LIST(name) &btrfs_attr_##name.attr

#define BTRFS_DEVICE_OFFSET_ATTR(_name,_mode,_show,_store,_elname) \
struct btrfs_device_attr btrfs_dev_attr_##_name = {	\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show = _show, 						\
	.store = _store,					\
	.offset = offsetof(struct btrfs_device, _elname),	\
}

#define BTRFS_DEVICE_ATTR(_name,_mode,_show,_store) \
struct btrfs_device_attr btrfs_dev_attr_##_name = __ATTR(_name,_mode,_show,_store)

#define DEVICE_ATTR_LIST(_name) &btrfs_dev_attr_##_name.attr

/*
 * Example Usage::
 *
 *	static BTRFS_ATTR(dummy,0666,btrfs_attr_show, btrfs_attr_store);
 *
 *	static struct attribute *btrfs_default_attrs[] = {
 *		ATTR_LIST(dummy),
 *		NULL,
 *	};
 *
 *
 * static struct kobj_type btrfs_ktype is defined to be the ktype of 
 * btrfs_kobject. It includes the implementations of sysfs_ops, release
 * and definition of btrfs_attrs for the same.
 *
 * REFERENCE DEFINITION
 *	static struct kobj_type btrfs_ktype = {
 *	.sysfs_ops = &btrfs_sysfs_ops,
 *	.release = btrfs_kobject_release,
 *	.default_attrs = btrfs_default_attrs,
 * };
 */

/* /sys/fs/btrfs/ entry */
static struct kset *btrfs_kset;

/*
 * The next goal is to implement the first level of directory structure under
 * /sys/fs/btrfs ie. to create btrfs_kobject under btrfs_kset
 * 
 * /sys/fs/btrfs/
 *					|->devices
 */
struct btrfs_kobject btrfs_devices;

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
 * Setup for /sys/fs/btrfs/devices Directory
 */

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

static ssize_t btrfs_device_show(struct kobject *btrfs_dev_kobj, \
	struct btrfs_device_attr *attr, char *buf)
{
	int *device_attr;
	struct btrfs_device *device;
	device = to_dev_kobj(btrfs_dev_kobj);
	device_attr = (int *) (((char *) device ) + attr->offset);
	return sprintf(buf, "%d\n", *device_attr);
}

static BTRFS_DEVICE_OFFSET_ATTR(cnt_write_io_errs,0444,btrfs_device_show,NULL,cnt_write_io_errs.counter);
static BTRFS_DEVICE_OFFSET_ATTR(cnt_read_io_errs,0444,btrfs_device_show,NULL,cnt_read_io_errs.counter);
static BTRFS_DEVICE_OFFSET_ATTR(cnt_flush_io_errs,0444,btrfs_device_show,NULL,cnt_flush_io_errs.counter);
static BTRFS_DEVICE_OFFSET_ATTR(cnt_corruption_errs,0444,btrfs_device_show,NULL,cnt_corruption_errs.counter);
static BTRFS_DEVICE_OFFSET_ATTR(cnt_generation_errs,0444,btrfs_device_show,NULL,cnt_generation_errs.counter);

static struct attribute *btrfs_device_default_attrs[] = {
	DEVICE_ATTR_LIST(cnt_write_io_errs),
	DEVICE_ATTR_LIST(cnt_read_io_errs),
	DEVICE_ATTR_LIST(cnt_flush_io_errs),
	DEVICE_ATTR_LIST(cnt_corruption_errs),
	DEVICE_ATTR_LIST(cnt_generation_errs),
	NULL,
};
static struct kobj_type btrfs_ktype_device = {
	.sysfs_ops = &btrfs_device_sysfs_ops,
	.release = btrfs_device_release,
	.default_attrs = btrfs_device_default_attrs,
};

static int btrfs_kobject_init_sysfs(void)
{
	int ret;
	btrfs_devices.kobj.kset = btrfs_kset;
	init_completion(&btrfs_devices.btrfs_kobj_unregister);
	ret = kobject_init_and_add(&btrfs_devices.kobj,&btrfs_ktype_device_dir,\
					NULL,"%s","devices");
	if (ret)
	{
		kobject_put(&btrfs_devices.kobj);
		return -EINVAL;
	}
	return 0;
}

int btrfs_init_sysfs(void)
{
	btrfs_kset = kset_create_and_add("btrfs", NULL, fs_kobj);
	if (!btrfs_kset)
		return -ENOMEM;
	return btrfs_kobject_init_sysfs();
}

int btrfs_create_device(struct kobject *device_kobj,char *dev_name)
{
	int ret;
	char device_name[64];
	char *prev;
	char *curr;
	char *ptr;
	struct btrfs_device *device;
	/*
	 * Convert from /dev/<device_name> to <device_name> so as to
 	 * make it more readable.
	 */
	prev = NULL;
	strcpy(device_name,dev_name);
	for(ptr=device_name; (curr=strsep(&ptr,"/"))!=NULL; prev=curr);
	
	device = to_dev_kobj(device_kobj);
	init_completion(&device->btrfs_device_unregister);

	ret = kobject_init_and_add(device_kobj,&btrfs_ktype_device, \
			&btrfs_devices.kobj,"%s",prev);
	if(ret)
	{
		kobject_put(device_kobj);
		return -EINVAL;
	}
	printk(KERN_INFO "btrfs: added sysfs device entry: %s",prev);
	return 0;
}

void btrfs_kobject_destroy(struct btrfs_kobject *btrfs_kobj)
{
	kobject_put(&btrfs_kobj->kobj);
	wait_for_completion(&btrfs_kobj->btrfs_kobj_unregister);
}

void btrfs_kill_device(struct kobject *device_kobj)
{
	struct btrfs_device *device;
	device = to_dev_kobj(device_kobj);
	kobject_put(device_kobj);
	wait_for_completion(&device->btrfs_device_unregister);
}

void btrfs_exit_sysfs(void)
{
	btrfs_kobject_destroy(&btrfs_devices);
	kset_unregister(btrfs_kset);
}
