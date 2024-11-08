/*
 * BRIEF DESCRIPTION
 *
 * Ioctl operations.
 *
 * Copyright 2015-2016 Regents of the University of California,
 * UCSD Non-Volatile Systems Lab, Andiry Xu <jix024@cs.ucsd.edu>
 * Copyright 2012-2013 Intel Corporation
 * Copyright 2010-2011 Marco Stornelli <marco.stornelli@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/capability.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include "bytefs.h"
#include "inode.h"

long bytefs_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct address_space *mapping = filp->f_mapping;
	struct inode    *inode = mapping->host;
	struct bytefs_inode_info *si = BYTEFS_I(inode);
	struct bytefs_inode_info_header *sih = &si->header;
	struct bytefs_inode *pi;
	struct super_block *sb = inode->i_sb;
	struct bytefs_inode_update update;
	unsigned int flags;
	int ret;
	unsigned long irq_flags = 0;

	pi = bytefs_get_inode(sb, inode);
	if (!pi)
		return -EACCES;

	switch (cmd) {
	case FS_IOC_GETFLAGS:
		flags = (sih->i_flags) & BYTEFS_FL_USER_VISIBLE;
		return put_user(flags, (int __user *)arg);
	case FS_IOC_SETFLAGS: {
		unsigned int oldflags;
		u64 old_linkc = 0;
		u64 epoch_id;

		ret = mnt_want_write_file(filp);
		if (ret)
			return ret;

		if (!inode_owner_or_capable(inode)) {
			ret = -EPERM;
			goto flags_out;
		}

		if (get_user(flags, (int __user *)arg)) {
			ret = -EFAULT;
			goto flags_out;
		}

		inode_lock(inode);
		oldflags = le32_to_cpu(pi->i_flags);

		if ((flags ^ oldflags) &
		    (FS_APPEND_FL | FS_IMMUTABLE_FL)) {
			if (!capable(CAP_LINUX_IMMUTABLE)) {
				ret = -EPERM;
				goto flags_out_unlock;
			}
		}

		if (!S_ISDIR(inode->i_mode))
			flags &= ~FS_DIRSYNC_FL;

		epoch_id = bytefs_get_epoch_id(sb);
		flags = flags & FS_FL_USER_MODIFIABLE;
		flags |= oldflags & ~FS_FL_USER_MODIFIABLE;
		inode->i_ctime = current_time(inode);
		bytefs_set_inode_flags(inode, pi, flags);
		sih->i_flags = flags;

		update.tail = 0;
		update.alter_tail = 0;
		ret = bytefs_append_link_change_entry(sb, pi, inode,
					&update, &old_linkc, epoch_id);
		if (!ret) {
			bytefs_memunlock_inode(sb, pi, &irq_flags);
			bytefs_update_inode(sb, inode, pi, &update, 1);
			bytefs_memlock_inode(sb, pi, &irq_flags);
			bytefs_invalidate_link_change_entry(sb, old_linkc);
		}
		sih->trans_id++;
flags_out_unlock:
		inode_unlock(inode);
flags_out:
		mnt_drop_write_file(filp);
		return ret;
	}
	case FS_IOC_GETVERSION:
		return put_user(inode->i_generation, (int __user *)arg);
	case FS_IOC_SETVERSION: {
		u64 old_linkc = 0;
		u64 epoch_id;
		__u32 generation;

		if (!inode_owner_or_capable(inode))
			return -EPERM;
		ret = mnt_want_write_file(filp);
		if (ret)
			return ret;
		if (get_user(generation, (int __user *)arg)) {
			ret = -EFAULT;
			goto setversion_out;
		}

		epoch_id = bytefs_get_epoch_id(sb);
		inode_lock(inode);
		inode->i_ctime = current_time(inode);
		inode->i_generation = generation;

		update.tail = 0;
		update.alter_tail = 0;
		ret = bytefs_append_link_change_entry(sb, pi, inode,
					&update, &old_linkc, epoch_id);
		if (!ret) {
			bytefs_memunlock_inode(sb, pi, &irq_flags);
			bytefs_update_inode(sb, inode, pi, &update, 1);
			bytefs_memlock_inode(sb, pi, &irq_flags);
			bytefs_invalidate_link_change_entry(sb, old_linkc);
		}
		sih->trans_id++;
		inode_unlock(inode);
setversion_out:
		mnt_drop_write_file(filp);
		return ret;
	}
	case BYTEFS_PRINT_TIMING: {
		// bytefs_print_timing_stats(sb);
		return 0;
	}
	case BYTEFS_CLEAR_STATS: {
		// bytefs_clear_stats(sb);
		return 0;
	}
	case BYTEFS_PRINT_LOG: {
		// bytefs_print_inode_log(sb, inode);
		return 0;
	}
	case BYTEFS_PRINT_LOG_PAGES: {
		// bytefs_print_inode_log_pages(sb, inode);
		return 0;
	}
	case BYTEFS_PRINT_FREE_LISTS: {
		// bytefs_print_free_lists(sb);
		return 0;
	}
	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long bytefs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case FS_IOC32_GETFLAGS:
		cmd = FS_IOC_GETFLAGS;
		break;
	case FS_IOC32_SETFLAGS:
		cmd = FS_IOC_SETFLAGS;
		break;
	case FS_IOC32_GETVERSION:
		cmd = FS_IOC_GETVERSION;
		break;
	case FS_IOC32_SETVERSION:
		cmd = FS_IOC_SETVERSION;
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return bytefs_ioctl(file, cmd, (unsigned long)compat_ptr(arg));
}
#endif
