// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2021 LeavaTail
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <getopt.h>
#include <limits.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <mntent.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "list.h"
#include "diffexfat.h"

FILE *output = NULL;
unsigned int print_level = PRINT_WARNING;
static struct exfat_bootsec boot, boot2;
node_t *bootlist = NULL;
node_t *fatlist = NULL;
node_t *datalist = NULL;

/**
 * Special Option(no short option)
 */
enum
{
	GETOPT_HELP_CHAR = (CHAR_MIN - 2),
	GETOPT_VERSION_CHAR = (CHAR_MIN - 3)
};

/* option data {"long name", needs argument, flags, "short name"} */
static struct option const longopts[] =
{
	{"help", no_argument, NULL, GETOPT_HELP_CHAR},
	{"version", no_argument, NULL, GETOPT_VERSION_CHAR},
	{0,0,0,0}
};

/**
 * usage - print out usage
 */
static void usage(void)
{
	fprintf(stderr, "Usage: %s [OPTION]... FILE\n", PROGRAM_NAME);
	fprintf(stderr, "Compare 2 exfat image and print difference\n");
	fprintf(stderr, "\n");

	fprintf(stderr, "  --help\tdisplay this help and exit.\n");
	fprintf(stderr, "  --version\toutput version information and exit.\n");
	fprintf(stderr, "\n");
}

/**
 * hexdump - Hex dump of a given data
 * @data:    Input data
 * @size:    Input data size
 */
void hexdump(void *data, size_t size)
{
	unsigned long skip = 0;
	size_t line, byte = 0;
	size_t count = size / 0x10;
	const char zero[0x10] = {0};

	for (line = 0; line < count; line++) {
		if ((line != count - 1) && (!memcmp(data + line * 0x10, zero, 0x10))) {
			switch (skip++) {
				case 0:
					break;
				case 1:
					pr_msg("*\n");
					/* FALLTHROUGH */
				default:
					continue;
			}
		} else {
			skip = 0;
		}

		pr_msg("%08lX:  ", line * 0x10);
		for (byte = 0; byte < 0x10; byte++) {
			pr_msg("%02X ", ((unsigned char *)data)[line * 0x10 + byte]);
		}
		putchar(' ');
		for (byte = 0; byte < 0x10; byte++) {
			char ch = ((unsigned char *)data)[line * 0x10 + byte];
			pr_msg("%c", isprint(ch) ? ch : '.');
		}
		pr_msg("\n");
	}
}

/**
 * get_sector - Get Raw-Data from any sector
 * @data:       Sector raw data (Output)
 * @f:          file descriptor
 * @index:      Start bytes
 *
 * @return       0 (success)
 *              -1 (failed to read)
 *
 * NOTE: Need to allocate @data before call it.
 */
int get_sector(void *data, int f, off_t index)
{
	return get_sectors(data, f, index, 1);
}

/**
 * set_sector - Set Raw-Data from any sector
 * @data:       Sector raw data
 * @f:          file descriptor
 * @index:      Start bytes
 *
 * @return       0 (success)
 *              -1 (failed to read)
 *
 * NOTE: Need to allocate @data before call it.
 */
int set_sector(void *data, int f, off_t index)
{
	return set_sectors(data, f, index, 1);
}

/**
 * get_sectors - Get Raw-Data from any sectors
 * @data:        Sector raw data (Output)
 * @f:           file descriptor
 * @index:       Start bytes
 * @count:       The number of sectors
 *
 * @return        0 (success)
 *               -1 (failed to read)
 *
 * NOTE: Need to allocate @data before call it.
 */
int get_sectors(void *data, int f, off_t index, size_t count)
{
	size_t sector_size = boot.BytesPerSectorShift;

	pr_debug("Get: Sector from 0x%lx to 0x%lx\n", index , index + (count * sector_size) - 1);
	if ((pread(f, data, count * sector_size, index)) < 0) {
		pr_err("read: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

/**
 * set_sectors - Set Raw-Data from any sectors
 * @data:        Sector raw data
 * @f:           file descriptor
 * @index:       Start bytes
 * @count:       The number of sectors
 *
 * @return        0 (success)
 *               -1 (failed to read)
 *
 * NOTE: Need to allocate @data before call it.
 */
int set_sectors(void *data, int f, off_t index, size_t count)
{
	size_t sector_size = boot.BytesPerSectorShift;

	pr_debug("Set: Sector from 0x%lx to 0x%lx\n", index, index + (count * sector_size) - 1);
	if ((pwrite(f, data, count * sector_size, index)) < 0) {
		pr_err("write: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

/**
 * get_cluster - Get Raw-Data from any cluster
 * @data:        cluster raw data (Output)
 * @index:       Start cluster index
 *
 * @return        0 (success)
 *               -1 (failed to read)
 *
 * NOTE: Need to allocate @data before call it.
 */
int get_cluster(void *data, off_t index)
{
	return get_clusters(data, index, 1);
}

/**
 * set_cluster - Set Raw-Data from any cluster
 * @data:        cluster raw data
 * @index:       Start cluster index
 *
 * @return        0 (success)
 *               -1 (failed to read)
 *
 * NOTE: Need to allocate @data before call it.
 */
int set_cluster(void *data, off_t index)
{
	return set_clusters(data, index, 1);
}

/**
 * get_clusters - Get Raw-Data from any cluster
 * @data:         cluster raw data (Output)
 * @index:        Start cluster index
 * @num:          The number of clusters
 *
 * @return         0 (success)
 *                -1 (failed to read)
 *
 * NOTE: Need to allocate @data before call it.
 */
int get_clusters(void *data, off_t index, size_t num)
{
	size_t sector_size  = 1 << boot.BytesPerSectorShift;
	size_t cluster_size = (1 << boot.SectorsPerClusterShift) * sector_size;
	size_t clu_per_sec = cluster_size / sector_size;
	off_t heap_start = boot.ClusterHeapOffset * sector_size;

	if (index < 2 || index + num > boot.ClusterCount) {
		pr_err("invalid cluster index %lu.\n", index);
		return -1;
	}

	return get_sector(data,
			heap_start + ((index - 2) * cluster_size),
			clu_per_sec * num);
}

/**
 * set_clusters - Set Raw-Data from any cluster
 * @data:         cluster raw data
 * @index:        Start cluster index
 * @num:          The number of clusters
 *
 * @return         0 (success)
 *                -1 (failed to read)
 *
 * NOTE: Need to allocate @data before call it.
 */
int set_clusters(void *data, off_t index, size_t num)
{
	size_t sector_size  = 1 << boot.BytesPerSectorShift;
	size_t cluster_size = (1 << boot.SectorsPerClusterShift) * sector_size;
	size_t clu_per_sec = cluster_size / sector_size;
	off_t heap_start = boot.ClusterHeapOffset * sector_size;

	if (index < 2 || index + num > boot.ClusterCount) {
		pr_err("invalid cluster index %lu.\n", index);
		return -1;
	}

	return set_sector(data,
			heap_start + ((index - 2) * cluster_size),
			clu_per_sec * num);
}
/**
 * version        - print out program version
 * @command_name:   command name
 * @version:        program version
 * @author:         program authoer
 */
static void version(const char *command_name, const char *version, const char *author)
{
	fprintf(stdout, "%s %s\n", command_name, version);
	fprintf(stdout, "\n");
	fprintf(stdout, "Written by %s.\n", author);
}

/**
 * print_sector - print any sector
 * @sector:       sector index to display
 *
 * return:        0 (succeeded in obtaining filesystem)
 */
int print_sector(uint32_t sector)
{
	void *data;
	size_t sector_size  = 1 << boot.BytesPerSectorShift;

	data = malloc(sector_size);
	if (!get_sector(data, sector, 1)) {
		pr_msg("Sector #%u:\n", sector);
		hexdump(data, sector_size);
	}
	free(data);
	return 0;
}

/**
 * print_cluster - print any cluster
 * @index:         cluster index to display
 *
 * @return        0 (success)
 */
int print_cluster(uint32_t index)
{
	void *data;
	size_t sector_size  = 1 << boot.BytesPerSectorShift;
	size_t cluster_size = (1 << boot.SectorsPerClusterShift) * sector_size;

	data = malloc(cluster_size);
	if (!get_cluster(data, index)) {
		pr_msg("Cluster #%u:\n", index);
		hexdump(data, cluster_size);
	}
	free(data);
	return 0;
}

/**
 * main   - main function
 * @argc:   argument count
 * @argv:   argument vector
 */
int main(int argc, char *argv[])
{
	int opt;
	int longindex;
	int ret = 0;
	int fd[2] = {0};
	struct stat st[2]  = {0};
	FILE *fp = NULL;
	size_t count;
	char buffer[CMDSIZE] = {0};
	char cmdline[CMDSIZE] = {0};
	unsigned long x;

	while ((opt = getopt_long(argc, argv,
					"",
					longopts, &longindex)) != -1) {
		switch (opt) {
			case GETOPT_HELP_CHAR:
				usage();
				exit(EXIT_SUCCESS);
			case GETOPT_VERSION_CHAR:
				version(PROGRAM_NAME, PROGRAM_VERSION, PROGRAM_AUTHOR);
				exit(EXIT_SUCCESS);
			default:
				usage();
				exit(EXIT_FAILURE);
		}
	}

#ifdef DEBUGFATFS_DEBUG
	print_level = PRINT_DEBUG;
#endif

	if (optind != argc - 2) {
		usage();
		exit(EXIT_FAILURE);
	}

	output = stdout;

	if ((fd[0] = open(argv[1], O_RDONLY)) < 0) {
		pr_err("open %s: %s\n", argv[1], strerror(errno));
		ret = -errno;
		goto out;
	}

	if ((fstat(fd[0], &(st[0]))) < 0) {
		pr_err("stat %s: %s\n", argv[1], strerror(errno));
		ret = -errno;
		goto out2;
	}

	if ((fd[1] = open(argv[2], O_RDONLY)) < 0) {
		pr_err("open %s: %s\n", argv[2], strerror(errno));
		ret = -errno;
		goto out1;
	}

	if ((fstat(fd[1], &(st[1]))) < 0) {
		pr_err("stat %s: %s\n", argv[2], strerror(errno));
		ret = -errno;
		goto out1;
	}

	if (st[0].st_size != st[1].st_size) {
		pr_err("file size is different.(%ld != %ld)\n", st[0].st_size, st[1].st_size);
		ret = -fd[0];
		goto out1;
	}

	count = pread(fd[0], &boot, SECSIZE, 0);
	if (count < 0) {
		pr_err("read: %s\n", strerror(errno));
		ret = -count;
		goto out1;
	}

	if (pread(fd[1], &boot2, SECSIZE, 0) != count) {
		pr_err("read: %s\n", strerror(errno));
		ret = -count;
		goto out1;
	}

	if (memcmp(&boot, &boot2, SECSIZE)) {
		pr_err("Boot sector is different.\n");
		ret = -count;
		goto out1;
	}

	snprintf(cmdline, CMDSIZE, "/bin/cmp -l %s %s", argv[1], argv[2]);
	if ((fp = popen(cmdline, "r")) == NULL) {
		pr_err("popen %s: %s\n", cmdline, strerror(errno));
		ret = -errno;
		goto out1;
	}

	while(fgets(buffer, CMDSIZE - 1, fp) != NULL) {
		x = strtoul(buffer, NULL, 0);
		if (x < EXFAT_FAT(boot)) {
			append_node(&bootlist, x);
		} else if (x < EXFAT_HEAP(boot)) {
			append_node(&fatlist, x);
		} else {
			append_node(&datalist, x);
		}
	}
	printf("\n");

	pr_msg("===== Boot Region =====\n");
	print_node(bootlist);
	pr_msg("===== FAT Region =====\n");
	print_node(fatlist);
	pr_msg("===== DATA Region =====\n");
	print_node(datalist);

	delete_node(&bootlist);
	delete_node(&fatlist);
	delete_node(&datalist);

	pclose(fp);

out1:
	if (fd[0])
		close(fd[0]);
out2:
	if (fd[1])
		close(fd[1]);
out:
	return ret;
}
