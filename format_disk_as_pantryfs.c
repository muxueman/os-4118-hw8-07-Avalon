#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stropts.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

/* These are the same on a 64-bit architecture */
#define timespec64 timespec

#include "pantryfs_inode.h"
#include "pantryfs_file.h"
#include "pantryfs_sb.h"


void passert(int condition, char *message)
{
	printf("[%s] %s\n", condition ? " OK " : "FAIL", message);
	if (!condition)
		exit(1);
}

void inode_reset(struct pantryfs_inode *inode)
{
	struct timespec current_time;

	/* In case inode is uninitialized/previously used */
	memset(inode, 0, sizeof(*inode));
	memset(&current_time, 0, sizeof(current_time));

	/* first account created on VM */
	inode->uid = 1000;
	inode->gid = 100;

	/* Current time UTC */
	clock_gettime(CLOCK_REALTIME, &current_time);
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time;
}

void dentry_reset(struct pantryfs_dir_entry *dentry)
{
	memset(dentry, 0, sizeof(*dentry));
}

int main(int argc, char *argv[])
{
	int fd;
	ssize_t ret;
	struct pantryfs_super_block sb;
	struct pantryfs_inode inode;
	struct pantryfs_dir_entry dentry;

	char *hello_contents = "Hello world!\n";
	char *names_contents =
		"Avalon members: Ruixin Liu, Yixuan Wang, Xueman Mu\n";
	char buf[PFS_BLOCK_SIZE];

	size_t len;
	const char zeroes[PFS_BLOCK_SIZE] = { 0 };

	if (argc != 2) {
		printf("Usage: ./format_disk_as_pantryfs DEVICE_NAME.\n");
		return -1;
	}

	fd = open(argv[1], O_RDWR);
	if (fd == -1) {
		perror("Error opening the device");
		return -1;
	}
	memset(&sb, 0, sizeof(sb));

	sb.version = 1;
	sb.magic = PANTRYFS_MAGIC_NUMBER;

	/* The first four inodes and datablocks are taken by the root, members,
	 * names.txt hello.txt file, respectively. Mark them as such.
	 */
	SETBIT(sb.free_inodes, 0);
	SETBIT(sb.free_inodes, 1);
	SETBIT(sb.free_inodes, 2);
	SETBIT(sb.free_inodes, 3);

	SETBIT(sb.free_data_blocks, 0);
	SETBIT(sb.free_data_blocks, 1);
	SETBIT(sb.free_data_blocks, 2);
	SETBIT(sb.free_data_blocks, 3);

	/* Write the superblock to the first block of the filesystem. */
	ret = write(fd, (char *)&sb, sizeof(sb));
	passert(ret == PFS_BLOCK_SIZE, "Write superblock");

	inode_reset(&inode);
	inode.mode = S_IFDIR | 0777;
	inode.nlink = 3;
	inode.data_block_number = PANTRYFS_ROOT_DATABLOCK_NUMBER;

	/* Write the root inode starting in the second block. */
	ret = write(fd, (char *)&inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write root inode");

	/* Set up the members inode and write it */
	inode_reset(&inode);
	inode.nlink = 2;
	inode.mode = S_IFDIR | 0777;
	inode.data_block_number = PANTRYFS_ROOT_DATABLOCK_NUMBER + 1;

	ret = write(fd, (char *)&inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write members inode");

	/* The hello.txt file will take
	 * inode num following members inode num.
	 */
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.data_block_number = PANTRYFS_ROOT_DATABLOCK_NUMBER + 2;
	inode.file_size = strlen(hello_contents);

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write hello.txt inode");

	/* The names.txt file will take inode num following hello inode num. */
	inode_reset(&inode);
	inode.nlink = 1;
	inode.mode = S_IFREG | 0666;
	inode.data_block_number = PANTRYFS_ROOT_DATABLOCK_NUMBER + 3;
	inode.file_size = strlen(names_contents);

	ret = write(fd, (char *) &inode, sizeof(inode));
	passert(ret == sizeof(inode), "Write names.txt inode");

	ret = lseek(fd, PFS_BLOCK_SIZE - 4 * sizeof(struct pantryfs_inode),
		SEEK_CUR);
	passert(ret >= 0, "Seek past inode table");

	/* Setup and write the hello dentry in /root */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "hello.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = PANTRYFS_ROOT_INODE_NUMBER + 2;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for hello.txt");

	/* Setup and write the members dentry in /root */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "members", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = PANTRYFS_ROOT_INODE_NUMBER + 1;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for /members");

	len = PFS_BLOCK_SIZE - 2 * sizeof(struct pantryfs_dir_entry);
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of root dentries");

	/* Setup and write the names dentry in /members */
	dentry_reset(&dentry);
	strncpy(dentry.filename, "names.txt", sizeof(dentry.filename));
	dentry.active = 1;
	dentry.inode_no = PANTRYFS_ROOT_INODE_NUMBER + 3;

	ret = write(fd, (char *) &dentry, sizeof(dentry));
	passert(ret == sizeof(dentry), "Write dentry for names.txt");

	len = PFS_BLOCK_SIZE - sizeof(struct pantryfs_dir_entry);
	ret = write(fd, zeroes, len);
	passert(ret == len, "Pad to end of members dentries");

	strncpy(buf, hello_contents, sizeof(buf));
	ret = write(fd, buf, sizeof(buf));
	passert(ret == sizeof(buf), "Write hello.txt contents");

	memset(buf, 0, sizeof(buf));

	strncpy(buf, names_contents, sizeof(buf));
	ret = write(fd, buf, sizeof(buf));
	passert(ret == sizeof(buf), "Write names.txt contents");

	ret = fsync(fd);
	passert(ret == 0, "Flush writes to disk");

	close(fd);
	printf("Device [%s] formatted successfully.\n", argv[1]);

	return 0;
}
