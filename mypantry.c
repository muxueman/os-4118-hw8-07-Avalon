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
	unsigned long long root_ino_ps;

	root_ino_ps = 1;

	if (ctx->pos == 10)
		return 0;

	if (!dir_emit(ctx, ".", 1, root_ino_ps, DT_DIR))
		return 0;
	ctx->pos++;

	if (!dir_emit(ctx, "..", 2, root_ino_ps, DT_DIR))
		return 0;
	ctx->pos++;

	if (!dir_emit(ctx, "hello.txt", 9, root_ino_ps, DT_REG))
		return 0;

	ctx->pos++;
	if (!dir_emit(ctx, "members", 7, root_ino_ps, DT_DIR))
		return 0;

	ctx->pos = 10;

	return 0;
}

ssize_t pantryfs_read(struct file *filp, char __user *buf, size_t len,
		loff_t *ppos)
{
	return -ENOSYS;
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
