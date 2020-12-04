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

	inode = file_inode(filp);
	parent = inode->i_ino;
	data_block_num = ((struct pantryfs_inode *)
		(inode->i_private))->data_block_number;

	if (ctx->pos == 10)
		return 0;

	bh = sb_bread(inode->i_sb, data_block_num);
	if (!bh)
		return -EINVAL;

	ps_dir_entry = (struct pantryfs_dir_entry *)(bh->b_data);

	if (!dir_emit(ctx, ".", 1, parent + 1, DT_DIR))
		return 0;
	ctx->pos++;

	if (!dir_emit(ctx, "..", 2, parent + 1, DT_DIR))
		return 0;
	ctx->pos++;

	while (true) {
		if (ps_dir_entry->active == 0)
			break;
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
		ctx->pos++;
	}

	brelse(bh);

	ctx->pos = 10;

	return 0;
}

ssize_t pantryfs_read(struct file *filp, char __user *buf, size_t len,
		loff_t *ppos)
{
	return -ENOSYS;
	struct inode *inode;
	struct super_block *sb;
	struct buffer_head *bh;
	struct pantryfs_inode *ps_inode;
	int data_block_num;
	unsigned long long file_len;

	/* Get file size and datablock number from inode in PFS */
	inode = file_inode(filp);
	sb = inode->i_sb;
	ps_inode = (struct pantryfs_inode *)(inode->i_private);

	file_len = ps_inode->file_size;
	data_block_num = ps_inode->data_block_number;

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

	brelse(bh);

	/* Update ppos */
	*ppos += len;

	return len;
}

int pantryfs_create(struct inode *parent, struct dentry *dentry,
		umode_t mode, bool excl)
{
	return -ENOSYS;
}

int pantryfs_unlink(struct inode *dir, struct dentry *dentry)
{
	return -ENOSYS;
}

int pantryfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	return -ENOSYS;
}

void pantryfs_evict_inode(struct inode *inode)
{
	/* Required to be called by VFS. If not called, evict() will BUG out.*/
	truncate_inode_pages_final(&inode->i_data);
	clear_inode(inode);
}

int pantryfs_fsync(struct file *filp, loff_t start, loff_t end, int datasync)
{
	return -ENOSYS;
}

ssize_t pantryfs_write(struct file *filp, const char __user *buf, size_t len,
		loff_t *ppos)
{
	return -ENOSYS;
}

struct dentry *pantryfs_lookup(struct inode *parent, struct dentry
		*child_dentry, unsigned int flags)
{
	return NULL;
	int new_ino;
	struct inode *new_inode;
	void *file_ops;
	const char *sub_file_name;
	unsigned short mode;

	new_ino = -1;
	new_inode = NULL;
	sub_file_name = child_dentry->d_name.name;

	if (parent->i_ino == 0) {
		if (strcmp(sub_file_name, "members") == 0) {
			new_ino = 1;
			mode = S_IFDIR | 0777;
			file_ops = &pantryfs_dir_ops;
		}
		if (strcmp(sub_file_name, "hello.txt") == 0) {
			new_ino = 2;
			mode = S_IFREG | 0666;
			file_ops = &pantryfs_file_ops;
		}
	}

	if (parent->i_ino == 1) {
		if (strcmp(sub_file_name, "names.txt") == 0) {
			new_ino = 3;
			mode = S_IFREG | 0666;
			file_ops = &pantryfs_file_ops;
		}
	}

	if (new_ino >= 0) {
		new_inode = iget_locked(parent->i_sb, new_ino);
		new_inode->i_mode = mode;
		new_inode->i_sb = parent->i_sb;
		new_inode->i_op = &pantryfs_inode_ops;
		new_inode->i_fop = file_ops;
		if (parent->i_ino == 0)
			new_inode->i_private = (parent->i_private) +
			new_ino * sizeof(struct pantryfs_inode);
		else
			new_inode->i_private = (parent->i_private) +
			(new_ino - 1) * sizeof(struct pantryfs_inode);
	}

	d_add(child_dentry, new_inode);
	pr_info("dentry has been added into dcache\n");

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
		return -EINVAL;
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
		return -EINVAL;
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

	/* Link the root dentry with inode */
	root_dentry = d_make_root(root_inode);
	if (!root_dentry)
		return -ENOMEM;
	sb->s_root = root_dentry;

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
