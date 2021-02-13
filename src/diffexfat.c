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

#include "diffexfat.h"
FILE *output = NULL;
unsigned int print_level = PRINT_WARNING;
static struct exfat_bootsec boot, boot2;
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
	void *a, *b;
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
		pr_msg("%lu\n", x);
	}

cmd_free:
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
