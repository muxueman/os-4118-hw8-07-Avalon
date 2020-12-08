Names & UNIs
	Ruixin Liu(rl3063), Xueman Mu(xm2232), Yixuan Wang(yw3345)

Homework assignment number
	HW8

Descriptions
1.Part1
In this part, we looked into details about original format_disk_as_pantryfs.c.
To add a new directory, we had to add a new dir_entry in the root_dir_block.
Then, this dir_entry should point to a inode defined in inode store. This inode
should pointer to another dir data block containing a dir_entry of names.txt which
pointed to a inode that indicated to the regular data block.
In short, we created two new inodes and two new data blocks on top of original
.c file. The nlink of members inode was 2, while the nlink of root dir was 3.

2.Part2
In this part, we were asked to finished mount and umount functionalities.
In order to achieve that, we modified pantryfs_fill_super() function.
We did have to fill in some of the superblock fields in fill_super(), though.
The code starts out like this: sb_set_blocksize() to constrain the block size,
We also used sb->s_magic = PANTRYFS_MAGIC_NUMBER, sb->s_op = &pantryfs_sb_ops,
and sb->s_fs_info = pfs_sb_bh to indicate sb_bh and i_store_bh. In detail, we
applied sb_bread(sb, 0) and sb_bread(sb, 1) to read superblock data block and
inode data block, respectively. In addition, to make sure we mount a filesystem
that is PantryFS, we checked the magic number in b_data of the buffer head of
superblock which should be exactly the same as what we defined in pantryfs_sb.h.
Then, we created the root directory using iget_locked and the parameter root_ino
was 0 and set inode_mode to be S_IFDIR | 0777. This directory inode must be put
into the directory cache (by way of a dentry structure), so that the VFS could
find it. This was done by using d_make_root(root_inode), which at the same time,
linking the inode with dentry. Finally, sb->s_root was set to be that root_dentry.

3.Part3
In this part, we add some codes in pantryfs_fill_super and finish pantryfs_iterate
to associate the root VFS inode with the root PantryFS inode. First, in pantryfs_full_super
we linked the i_private member in root VFS inode to the i_store_bh member in the
PantryFS, then set the i_sb, i_op and i_fop menber in VFS using the associating
PantryFS member. As for pantryfs_iterate, we firstly get the inode number of the
filp in VFS, then got the associating data_block_num. So we can use sb_bread() to
get the buffer head which stored the dentries in current directory, and the b_data
was what we need. Fisrtly we added "." and "..", then we used a while loop to traverse
all the dentries under this directory, we used the inode in VFS to find the corresponding
inode in PantryFS, and moved to the next dentry by increasing the pointer by 1.
The while loop will end if the dentry.active is 0. Also we need to set the ending pos
to stop calling pantryfs_iterate.

4.Part4
In this part, we mainly implemented the lookup function of inode_operations.
Lookup function was called when the VFS needed to look up an inode in a parent
directory. We used the similar while loop like part3 to find the determined subdirs.
We set new_ino to be -1 and new_inode to be NULL. If we didn't anything, we would d_add()
NULL. In detail, for example, in pantry directory, it looked up hello.txt when strcmp()
return 0 and called iget_locked() function to create a new inode with corresponding
mode(S_IFREG | 0666), sb(parent->i_sb), op(pantryfs_inode_ops), fop(pantryfs_file_
ops), private attributes(parent->i_private) + new_ino * sizeof(struct pantryfs_inode)).
Then, we used d_add() to insert the found inode into the dentry.
In addition, we also modified pantryfs_iterate() to be able to iterate in members dir.

5.Part5
In this part, we added pantryfs_read().
First, we should make sure that the length we copy_to_user() was no larger than the
file length. The method we got access to data: firstly, we got the inode and the
corresponding data block number from *filp, and then, we got the bh->b_data using
sb_bread(). The b_data was what we needed, so finally, we used copy_to_user() to read
data from kernel to buffer. Here we incremented the *ppos-offset by *ppos += count,
then passed the count number back to the caller. Particularly, when *ppos was larger
than file_len, we immediately returned 0 in order to finish the read(). In the end, we
released buffer head by using breles(bh).

6.Part6
In this part, we added some parameters from pantryfs to VFS, such as size, link count,
timestamps, permissions, owner, and group, to make sure that we got exactlly the same
"stat" command result as module of ref/pantryfs.ko. We tested and passed all other
stress tests mentioned in the task requirments.

7.Part7
In this part, we added pantryfs_write() and write_inode() to complete the overwriting
function. In the pantryfs_write(), first we got the vfs inode by the given filp, then
got the pfs inode by i_private, so we could have the data block number. Then we created
a buffer head to read contents from the corresponding data block. We need to validated the
len argument so that we could update the file size, also there were some other variables
we updated such as access time and modified time. We used copy_from_user() to added the
new content in buf to the opened file. In the end, we updated *ppos and marked the inode
and the buffer head as dirty, so the vfs will call write_inode() and write the changing
back to the disk. What's more we must added sync_dirty_buffer() so that our pfs can use
vim to edit our files.
In the write_inode(), we created a new buffer head and used sb_bread() to find the block
that stored the inode information and then updated them, including mode, i_mtime, i_atime,
file_size, nlink, uid and gid. Also we need to mark the buffer head as dirty and add
sync_dirty_buffer() to fix the fsync() failing problem.

8.Part8
In this part, we combined what had already learned from the previous parts.
First, we needed to find corresponding numbers of free data blocks and free inodes to
store our new created files. We implemented this by checking IS_SET(superblock->free_
data_blocks/free_inodes, i). Then, we shall create new pantryfs_inode. The dentry of
this inode should also be added to its parent_dir_entry. So, we used sb_bread(sb, ((
struct pantryfs_inode *)(parent->i_private))->data_block_number) to find the stored
dentries in that parent dir and followed by new file dentry. We almost done with pantryfs
part and finally, created VFS inode, and unlinked it with pantryfs_dentry. We made sure
that all changes to be written back to disk using mark_buffer_dirty().
