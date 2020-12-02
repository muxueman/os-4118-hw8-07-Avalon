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
filp in VFS, then we used dir_emit to obtain the information under the root directory.
One important thing is that the ino parameter in dir_emit is 1 instead of 0, which because
in the PantryFS the inode number start with 1 instead of 0, which is different from
VFS. Using dir_emit for four times, the four submember ".", "..", "hello.txt", "member"
can be shown. Also we need to set the ending time to stop calling pantryfs_iterate.
