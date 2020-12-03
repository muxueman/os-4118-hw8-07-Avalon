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

4.Part4
In this part, we mainly implemented the lookup function of inode_operations.
Lookup function was called when the VFS needed to look up an inode in a parent
directory. For example, in pantry directory, it looked up hello.txt when strcmp()
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
