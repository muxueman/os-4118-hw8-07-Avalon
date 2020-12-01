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
