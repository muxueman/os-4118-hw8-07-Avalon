#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>

#include "pantryfs_inode.h"
#include "pantryfs_inode_ops.h"
#include "pantryfs_file.h"
#include "pantryfs_file_ops.h"
#include "pantryfs_sb.h"
#include "pantryfs_sb_ops.h"

int pantryfs_iterate(struct file *filp, struct dir_context *ctx)
{
	unsigned long long parent, data_block_num, ps_inode_no;
	struct inode *inode;
	struct buffer_head *bh;
	struct pantryfs_dir_entry *ps_dir_entry;
	struct pantryfs_inode *ps_inode;
	int loop_counter;

	inode = file_inode(filp);
	parent = inode->i_ino;
	loop_counter = 0;
	data_block_num = ((struct pantryfs_inode *)
		(inode->i_private))->data_block_number;

	/* End condition */
	if (ctx->pos == 10)
		return 0;

	/* Read the datablock storing entries from pantryfs */
	bh = sb_bread(inode->i_sb, data_block_num);
	if (!bh)
		return -EINVAL;

	ps_dir_entry = (struct pantryfs_dir_entry *)(bh->b_data);

	/* Add . and .. */
	if (!dir_emit(ctx, ".", 1, parent + 1, DT_DIR))
		return 0;
	ctx->pos++;

	if (!dir_emit(ctx, "..", 2, parent + 1, DT_DIR))
		return 0;
	ctx->pos++;

	/* Traverse the datablock to retrieve all the entries under */
	/* this directroy */
	while (loop_counter < (PFS_BLOCK_SIZE /
				sizeof(struct pantryfs_dir_entry))) {
		if (ps_dir_entry->active == 0) {
			ps_dir_entry += 1;
			loop_counter++;
			ctx->pos++;
			continue;
		}
		ps_inode_no = ps_dir_entry->inode_no;
		ps_inode = (struct pantryfs_inode *)
			(inode->i_sb->s_root->d_inode->i_private) +
			(ps_inode_no - PANTRYFS_ROOT_INODE_NUMBER);

		if (!dir_emit(ctx, ps_dir_entry->filename,
					PANTRYFS_FILENAME_BUF_SIZE,
					parent + 1,
					(ps_inode->mode) >> 12)) {
			brelse(bh);
			return 0;
		}
		ps_dir_entry += 1;
		loop_counter++;
		ctx->pos++;
	}
	brelse(bh);
	ctx->pos = 10;

	return 0;
}

ssize_t pantryfs_read(struct file *filp, char __user *buf, size_t len,
		loff_t *ppos)
{
	struct inode *inode;
	struct super_block *sb;
	struct buffer_head *bh;
	struct pantryfs_inode *ps_inode;
	int data_block_num;
	unsigned long long file_len;
	struct timespec64 ts64;

	/* Get file size and datablock number from inode in PFS */
	inode = file_inode(filp);
	sb = inode->i_sb;
	ps_inode = (struct pantryfs_inode *)(inode->i_private);

	file_len = inode->i_size;
	data_block_num = ps_inode->data_block_number;

	/* Update access time */
	ktime_get_coarse_real_ts64(&ts64);
	inode->i_atime = ts64;

	/* Read the data from the corrisponding datablock */
	bh = sb_bread(sb, data_block_num);
	if (!bh)
		return -EIO;

	/* Validate the len argument */
	if (*ppos > file_len)
		return 0;
	if (len > file_len - *ppos)
		len = file_len - *ppos;

	/* Copy to user space */
	if (copy_to_user(buf, bh->b_data + *ppos, len))
		return -EFAULT;

	mark_inode_dirty(inode);
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	/* Update ppos */
	*ppos += len;

	return len;
}

int pantryfs_create(struct inode *parent, struct dentry *dentry,
		umode_t mode, bool excl)
{
	int new_data_block_num, new_ps_inode_num, new_vfs_inode_num,
	    ps_dentry_data_block_num, loop_counter, i;
	struct super_block *sb;
	struct inode *new_vfs_inode;
	struct pantryfs_inode *new_ps_inode;
	struct pantryfs_dir_entry *ps_dentry;
	struct buffer_head *bh_sb, *bh_i_store, *bh_ps_dentry;
	struct pantryfs_super_block *ps_sb;
	bool db_flag, inode_flag, is_dir;
	struct timespec64 ts64;

	sb = parent->i_sb;
	db_flag = inode_flag = false;
	loop_counter = 0;

	/* Initialize the new ps dentry */
	ps_dentry_data_block_num = ((struct pantryfs_inode *)
		(parent->i_private))->data_block_number;
	bh_ps_dentry = sb_bread(sb, ps_dentry_data_block_num);
	if (!bh_ps_dentry)
		return -EIO;

	ps_dentry = (struct pantryfs_dir_entry *)
		(bh_ps_dentry->b_data);
	while (loop_counter < PFS_MAX_CHILDREN &&
			ps_dentry->active == 1) {
		loop_counter++;
		ps_dentry++;
	}

	/* Children are full */
	if (loop_counter == PFS_MAX_CHILDREN)
		return -EIO;

	/* Check if we are creating a directory */
	if (mode >> 12 == DT_DIR)
		is_dir = true;
	else
		is_dir = false;

	/* SETBIT for new datablock and new inodes in PS */
	bh_sb = sb_bread(sb, PANTRYFS_SUPERBLOCK_DATABLOCK_NUMBER);
	if (!bh_sb)
		return -EIO;

	ps_sb = (struct pantryfs_super_block *)(bh_sb->b_data);

	for (i = 0; i < PFS_MAX_INODES; i++) {
		if (!db_flag && !IS_SET(ps_sb->free_data_blocks, i)) {
			SETBIT(ps_sb->free_data_blocks, i);
			new_data_block_num = i +
				PANTRYFS_ROOT_DATABLOCK_NUMBER;
			db_flag = true;
		}
		if (!inode_flag && !IS_SET(ps_sb->free_inodes, i)) {
			SETBIT(ps_sb->free_inodes, i);
			new_ps_inode_num = i +
				PANTRYFS_ROOT_INODE_NUMBER;
			inode_flag = true;
		}
		if (inode_flag && db_flag)
			break;
	}

	/* No empty space in PantryFS */
	if (i == PFS_MAX_INODES)
		return -EIO;

	/* Fill in the ps dentry */
	ps_dentry->active = 1;
	ps_dentry->inode_no = new_ps_inode_num;
	strncpy(ps_dentry->filename, dentry->d_name.name,
			sizeof(ps_dentry->filename));
	/* Initialize the new pantryfs inode */
	bh_i_store = sb_bread(sb, PANTRYFS_INODE_STORE_DATABLOCK_NUMBER);
	if (!bh_i_store)
		return -EIO;

	new_ps_inode = ((struct pantryfs_inode *)(bh_i_store->b_data)) +
		new_ps_inode_num - PANTRYFS_ROOT_INODE_NUMBER;
	ktime_get_coarse_real_ts64(&ts64);
	new_ps_inode->i_atime = new_ps_inode->i_mtime =
		new_ps_inode->i_ctime = ts64;
	new_ps_inode->uid = 1000;
	new_ps_inode->gid = 100;
	new_ps_inode->nlink = 1;
	new_ps_inode->mode = mode;
	new_ps_inode->data_block_number = new_data_block_num;
	if (is_dir)
		new_ps_inode->file_size = PFS_BLOCK_SIZE;

	/* Create VFS inode and fill in the info */
	new_vfs_inode_num = new_ps_inode_num -
		PANTRYFS_ROOT_INODE_NUMBER;
	new_vfs_inode = iget_locked(sb, new_vfs_inode_num);
	new_vfs_inode->i_mode = new_ps_inode->mode;
	new_vfs_inode->i_sb = sb;
	new_vfs_inode->i_op = &pantryfs_inode_ops;
	new_vfs_inode->i_atime = new_ps_inode->i_atime;
	new_vfs_inode->i_mtime = new_ps_inode->i_mtime;
	new_vfs_inode->i_ctime = new_ps_inode->i_ctime;
	new_vfs_inode->i_size = new_ps_inode->file_size;
	set_nlink(new_vfs_inode, new_ps_inode->nlink);
	new_vfs_inode->i_uid.val = new_ps_inode->uid;
	new_vfs_inode->i_gid.val = new_ps_inode->gid;
	if (is_dir)
		new_vfs_inode->i_fop = &pantryfs_dir_ops;
	else
		new_vfs_inode->i_fop = &pantryfs_file_ops;

	new_vfs_inode->i_private = (void *)
		((struct pantryfs_inode *)
		 (sb->s_root->d_inode->i_private) +
		 (new_ps_inode_num -
		  PANTRYFS_ROOT_INODE_NUMBER));

	unlock_new_inode(new_vfs_inode);

	/* Add to dcache */
	d_add(dentry, new_vfs_inode);

	/* update to disk */
	mark_buffer_dirty(bh_sb);
	mark_buffer_dirty(bh_i_store);
	mark_buffer_dirty(bh_ps_dentry);
	sync_dirty_buffer(bh_sb);
	sync_dirty_buffer(bh_i_store);
	sync_dirty_buffer(bh_ps_dentry);
	brelse(bh_sb);
	brelse(bh_i_store);
	brelse(bh_ps_dentry);

	/* Update the nlink for the parent */
	if (is_dir) {
		set_nlink(parent, parent->i_nlink + 1);
		mark_inode_dirty(parent);
	}

	return 0;
}

int pantryfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct pantryfs_dir_entry *ps_dir_entry;
	struct inode *vfs_inode;
	struct super_block *sb;
	int ps_dentry_db_no, ps_inode_no;
	struct buffer_head *ps_dentry_bh;

	sb = dir->i_sb;
	ps_dentry_db_no = ((struct pantryfs_inode *)
			(dir->i_private))->data_block_number;
	vfs_inode = dentry->d_inode;

	/* Look for the corrisponding pantryfs dentry and */
	/* clear it */
	ps_dentry_bh = sb_bread(sb, ps_dentry_db_no);
	if (!ps_dentry_bh)
		return -EIO;

	ps_dir_entry = (struct pantryfs_dir_entry *)
		(ps_dentry_bh->b_data);
	while (strcmp(ps_dir_entry->filename,
				dentry->d_name.name) != 0)
		ps_dir_entry++;

	ps_inode_no = ps_dir_entry->inode_no;
	memset(ps_dir_entry, 0, sizeof(struct pantryfs_dir_entry));

	drop_nlink(vfs_inode);

	return 0;
}

int pantryfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	struct pantryfs_inode *ps_inode;
	struct buffer_head *bh;

	bh = sb_bread(inode->i_sb,
			PANTRYFS_INODE_STORE_DATABLOCK_NUMBER);
	if (!bh)
		return -EIO;

	ps_inode = (struct pantryfs_inode *)(bh->b_data)
		+ inode->i_ino;

	/* Update inode's information */
	ps_inode->mode = inode->i_mode;
	ps_inode->i_mtime = inode->i_mtime;
	ps_inode->i_atime = inode->i_atime;
	ps_inode->file_size = inode->i_size;
	ps_inode->nlink = inode->i_nlink;
	ps_inode->uid = inode->i_uid.val;
	ps_inode->gid = inode->i_gid.val;

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	return 0;
}

void pantryfs_evict_inode(struct inode *inode)
{
	struct super_block *sb;
	struct pantryfs_super_block *ps_sb;
	struct buffer_head *i_store_bh, *content_bh, *ps_sb_bh;
	struct pantryfs_inode *ps_inode;
	int ps_inode_no, vfs_inode_no, ps_db_no;

	sb = inode->i_sb;
	vfs_inode_no = inode->i_ino;
	ps_inode_no = vfs_inode_no + PANTRYFS_ROOT_INODE_NUMBER;

	/* Required to be called by VFS. If not called, evict() will BUG out.*/
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);

	/* Look for the corrsiponding pantryfs inode and clear it */
	i_store_bh = sb_bread(sb, PANTRYFS_INODE_STORE_DATABLOCK_NUMBER);
	if (!i_store_bh)
		return;

	ps_inode = ((struct pantryfs_inode *)(i_store_bh->b_data))
		+ ps_inode_no - PANTRYFS_ROOT_INODE_NUMBER;
	ps_db_no = ps_inode->data_block_number;
	memset(ps_inode, 0, sizeof(struct pantryfs_inode));

	/* Clear the content datablock */
	content_bh = sb_bread(sb, ps_db_no);
	if (!content_bh)
		return;

	memset(content_bh->b_data, 0, PFS_BLOCK_SIZE);

	/* Clear bit for ps superblock */
	ps_sb_bh = sb_bread(sb, PANTRYFS_SUPERBLOCK_DATABLOCK_NUMBER);
	if (!ps_sb_bh)
		return;

	ps_sb = (struct pantryfs_super_block *)(ps_sb_bh->b_data);
	CLEARBIT(ps_sb->free_inodes,
			ps_inode_no - PANTRYFS_ROOT_INODE_NUMBER);
	CLEARBIT(ps_sb->free_data_blocks,
			ps_db_no - PANTRYFS_ROOT_DATABLOCK_NUMBER);

	/* Update the disk */
	mark_buffer_dirty(i_store_bh);
	mark_buffer_dirty(content_bh);
	mark_buffer_dirty(ps_sb_bh);
	sync_dirty_buffer(i_store_bh);
	sync_dirty_buffer(content_bh);
	sync_dirty_buffer(ps_sb_bh);
	brelse(i_store_bh);
	brelse(content_bh);
	brelse(ps_sb_bh);
}

int pantryfs_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	return 0;
}

ssize_t pantryfs_write(struct file *filp, const char __user *buf, size_t len,
		loff_t *ppos)
{
	struct inode *vfs_inode;
	struct pantryfs_inode *ps_inode;
	struct buffer_head *bh;
	int data_block_num;
	struct timespec64 ts64;

	vfs_inode = file_inode(filp);
	ps_inode = (struct pantryfs_inode *)
		(vfs_inode->i_private);
	data_block_num = ps_inode->data_block_number;

	/* Update access time */
	ktime_get_coarse_real_ts64(&ts64);
	vfs_inode->i_atime = ts64;

	/* Read contents from corrisponding datablock */
	bh = sb_bread(vfs_inode->i_sb, data_block_num);
	if (!bh)
		return -EIO;

	/* Validate the len argument */
	if (*ppos > PFS_BLOCK_SIZE)
		return 0;
	if (len > PFS_BLOCK_SIZE - *ppos)
		len = PFS_BLOCK_SIZE - *ppos;

	/* Update the vfs inode */
	vfs_inode->i_mtime = ts64;
	vfs_inode->i_size = *ppos + len;

	/*Mark this inode as dirty */
	mark_inode_dirty(vfs_inode);

	/* Copy from user space */
	if (copy_from_user((bh->b_data) + *ppos, buf, len))
		return -EFAULT;

	/* Update ppos */
	*ppos = *ppos + len;

	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

	return len;
}

struct dentry *pantryfs_lookup(struct inode *parent, struct dentry
		*child_dentry, unsigned int flags)
{
	unsigned long long data_block_num, ps_inode_no;
	int new_ino, loop_counter;
	struct inode *new_inode;
	struct buffer_head *bh;
	const char *sub_file_name;
	struct pantryfs_dir_entry *ps_dir_entry;
	struct pantryfs_inode *ps_inode;

	/* Get sub file name and data block number */
	new_ino = -1;
	loop_counter = 0;
	new_inode = NULL;
	sub_file_name = child_dentry->d_name.name;
	data_block_num = ((struct pantryfs_inode *)
		(parent->i_private))->data_block_number;

	bh = sb_bread(parent->i_sb, data_block_num);
	if (!bh)
		return -EIO;

	ps_dir_entry = (struct pantryfs_dir_entry *)(bh->b_data);

	/* Traverse the datablock to get the dir_entry with */
	/* the same name */
	while (loop_counter < (PFS_BLOCK_SIZE /
				sizeof(struct pantryfs_dir_entry))) {
		if (ps_dir_entry->active == 0) {
			ps_dir_entry += 1;
			loop_counter++;
			continue;
		}

		if (strcmp(sub_file_name, ps_dir_entry->filename) == 0) {
			ps_inode_no = ps_dir_entry->inode_no;
			new_ino = ps_inode_no - 1;
			ps_inode = (struct pantryfs_inode *)
				(parent->i_sb->s_root->d_inode->i_private) +
				(ps_inode_no - PANTRYFS_ROOT_INODE_NUMBER);

			break;
		}

		ps_dir_entry += 1;
		loop_counter++;
	}

	brelse(bh);

	/* Create the new inode for VFS */
	if (new_ino > 0) {
		new_inode = iget_locked(parent->i_sb, new_ino);
		new_inode->i_mode = ps_inode->mode;
		new_inode->i_sb = parent->i_sb;
		new_inode->i_op = &pantryfs_inode_ops;
		new_inode->i_atime = ps_inode->i_atime;
		new_inode->i_mtime = ps_inode->i_mtime;
		new_inode->i_ctime = ps_inode->i_ctime;
		new_inode->i_size = ps_inode->file_size;
		set_nlink(new_inode, ps_inode->nlink);
		new_inode->i_uid.val = ps_inode->uid;
		new_inode->i_gid.val = ps_inode->gid;
		if ((ps_inode->mode) >> 12 == DT_DIR) {
			new_inode->i_size = PFS_BLOCK_SIZE;
			new_inode->i_fop = &pantryfs_dir_ops;
		} else {
			new_inode->i_size = ps_inode->file_size;
			new_inode->i_fop = &pantryfs_file_ops;
		}

		new_inode->i_private = (void *)ps_inode;

		unlock_new_inode(new_inode);
	}

	/* Add to dcache */
	d_add(child_dentry, new_inode);

	return NULL;
}

int pantryfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct pantryfs_sb_buffer_heads *pfs_sb_bh;
	struct inode *root_inode;
	int root_ino;
	unsigned short root_mode;
	unsigned long fetch_magic;
	struct dentry *root_dentry;
	int ret;

	root_mode = S_IFDIR | 0777;
	root_ino = 0;
	pfs_sb_bh = kmalloc(sizeof(struct pantryfs_sb_buffer_heads),
			GFP_KERNEL);
	if (!pfs_sb_bh)
		return -ENOMEM;

	/* Set the b_data of sb_bh and i_store_bh */
	ret = sb_set_blocksize(sb, PFS_BLOCK_SIZE);
	if (ret == 0)
		return -EINVAL;
	pfs_sb_bh->sb_bh = sb_bread(sb, 0);
	if (!pfs_sb_bh->sb_bh) {
		pr_err("sb_bh bread crashed!");
		return -EIO;
	}

	/* Check if the target file system is pantryfs */
	fetch_magic = (unsigned long)(((struct pantryfs_super_block *)
				(pfs_sb_bh->sb_bh->b_data))->magic);
	if (fetch_magic !=  PANTRYFS_MAGIC_NUMBER) {
		pr_err("The target fs is not PantryFS!\n");
		return -EINVAL;
	}

	pfs_sb_bh->i_store_bh = sb_bread(sb, 1);
	if (!pfs_sb_bh->i_store_bh) {
		pr_err("i_store_bh bread crashed!");
		return -EIO;
	}

	/* Fill the fields of VFS super_block */
	sb->s_fs_info = pfs_sb_bh;
	sb->s_magic = PANTRYFS_MAGIC_NUMBER;
	sb->s_op = &pantryfs_sb_ops;

	/* Create a inode for root dir */
	root_inode = iget_locked(sb, root_ino);
	if (!root_inode)
		return -EINVAL;
	root_inode->i_mode = root_mode;
	root_inode->i_private = pfs_sb_bh->i_store_bh->b_data;
	root_inode->i_sb = sb;
	root_inode->i_op = &pantryfs_inode_ops;
	root_inode->i_fop = &pantryfs_dir_ops;
	root_inode->i_size = PFS_BLOCK_SIZE;
	set_nlink(root_inode, ((struct pantryfs_inode *)
			(root_inode->i_private))->nlink);

	/* Link the root dentry with inode */
	root_dentry = d_make_root(root_inode);
	if (!root_dentry)
		return -ENOMEM;
	sb->s_root = root_dentry;

	unlock_new_inode(root_inode);

	return 0;
}

static struct dentry *pantryfs_mount(struct file_system_type *fs_type,
		int flags, const char *dev_name,
		void *data)
{
	struct dentry *ret;

	/* mount_bdev is "mount block device". */
	ret = mount_bdev(fs_type, flags, dev_name, data, pantryfs_fill_super);

	if (unlikely(IS_ERR(ret)))
		pr_err("Error mounting mypantryfs");
	else
		pr_info("Mounted mypantryfs on [%s]\n", dev_name);

	return ret;
}

static void pantryfs_kill_superblock(struct super_block *sb)
{
	kill_block_super(sb);
	pr_info("mypantryfs superblock destroyed. Unmount successful.\n");
}

struct file_system_type pantryfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "mypantryfs",
	.mount = pantryfs_mount,
	.kill_sb = pantryfs_kill_superblock,
};

static int pantryfs_init(void)
{
	int ret;

	ret = register_filesystem(&pantryfs_fs_type);
	if (likely(ret == 0))
		pr_info("Successfully registered mypantryfs\n");
	else
		pr_err("Failed to register mypantryfs. Error:[%d]", ret);

	return ret;
}

static void pantryfs_exit(void)
{
	int ret;

	ret = unregister_filesystem(&pantryfs_fs_type);

	if (likely(ret == 0))
		pr_info("Successfully unregistered mypantryfs\n");
	else
		pr_err("Failed to unregister mypantryfs. Error:[%d]", ret);
}

module_init(pantryfs_init);
module_exit(pantryfs_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Group N");
