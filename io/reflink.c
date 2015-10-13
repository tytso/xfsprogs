/*
 * Copyright (c) 2015 Oracle, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <sys/uio.h>
#include <xfs/xfs.h>
#include "command.h"
#include "input.h"
#include "init.h"
#include "io.h"

static cmdinfo_t reflink_cmd;

static void
reflink_help(void)
{
	printf(_(
"\n"
" Links a range of bytes (in block size increments) from a file into a range \n"
" of bytes in the open file.  The two extent ranges need not contain identical\n"
" data. \n"
"\n"
" Example:\n"
" 'reflink some_file 0 4096 32768' - links 32768 bytes from some_file at \n"
"                                    offset 0 to into the open file at \n"
"                                    position 4096\n"
" 'reflink some_file' - links all bytes from some_file into the open file\n"
"                       at position 0\n"
"\n"
" Reflink a range of blocks from a given input file to the open file.  Both\n"
" files share the same range of physical disk blocks; a write to the shared\n"
" range of either file should result in the write landing in a new block and\n"
" that range of the file being remapped (i.e. copy-on-write).  Both files\n"
" must reside on the same filesystem.\n"
" -w   -- call fdatasync(2) at the end (included in timing results)\n"
" -W   -- call fsync(2) at the end (included in timing results)\n"
"\n"));
}

static int
reflink_f(
	int		argc,
	char		**argv)
{
	off64_t		soffset = 0, doffset = 0;
	long long	count = 0, total;
	char		s1[64], s2[64], ts[64];
	char		*infile = NULL;
	int		Cflag, qflag, wflag, Wflag;
	struct xfs_ioctl_clone_range_args	args;
	size_t		fsblocksize, fssectsize;
	struct timeval	t1, t2;
	int		c, fd = -1;

	Cflag = qflag = wflag = Wflag = 0;
	init_cvtnum(&fsblocksize, &fssectsize);

	while ((c = getopt(argc, argv, "CqwW")) != EOF) {
		switch (c) {
		case 'C':
			Cflag = 1;
			break;
		case 'q':
			qflag = 1;
			break;
		case 'w':
			wflag = 1;
			break;
		case 'W':
			Wflag = 1;
			break;
		default:
			return command_usage(&reflink_cmd);
		}
	}
	if (optind != argc - 4 && optind != argc - 1)
		return command_usage(&reflink_cmd);
	infile = argv[optind];
	optind++;
	if (optind == argc)
		goto clone_all;
	soffset = cvtnum(fsblocksize, fssectsize, argv[optind]);
	if (soffset < 0) {
		printf(_("non-numeric src offset argument -- %s\n"), argv[optind]);
		return 0;
	}
	optind++;
	doffset = cvtnum(fsblocksize, fssectsize, argv[optind]);
	if (doffset < 0) {
		printf(_("non-numeric dest offset argument -- %s\n"), argv[optind]);
		return 0;
	}
	optind++;
	count = cvtnum(fsblocksize, fssectsize, argv[optind]);
	if (count < 1) {
		printf(_("non-positive length argument -- %s\n"), argv[optind]);
		return 0;
	}

clone_all:
	c = IO_READONLY;
	fd = openfile(infile, NULL, c, 0);
	if (fd < 0)
		return 0;

	gettimeofday(&t1, NULL);
	if (count) {
		args.src_fd = fd;
		args.src_offset = soffset;
		args.src_length = count;
		args.dest_offset = doffset;
		c = ioctl(file->fd, XFS_IOC_CLONE_RANGE, &args);
	} else {
		c = ioctl(file->fd, XFS_IOC_CLONE, fd);
	}
	if (c < 0) {
		perror(_("reflink"));
		goto done;
	}
	total = count;
	c = 1;
	if (Wflag)
		fsync(file->fd);
	if (wflag)
		fdatasync(file->fd);
	if (qflag)
		goto done;
	gettimeofday(&t2, NULL);
	t2 = tsub(t2, t1);

	/* Finally, report back -- -C gives a parsable format */
	timestr(&t2, ts, sizeof(ts), Cflag ? VERBOSE_FIXED_TIME : 0);
	if (!Cflag) {
		cvtstr((double)total, s1, sizeof(s1));
		cvtstr(tdiv((double)total, t2), s2, sizeof(s2));
		printf(_("linked %lld/%lld bytes at offset %lld\n"),
			total, count, (long long)doffset);
		printf(_("%s, %d ops; %s (%s/sec and %.4f ops/sec)\n"),
			s1, c, ts, s2, tdiv((double)c, t2));
	} else {/* bytes,ops,time,bytes/sec,ops/sec */
		printf("%lld,%d,%s,%.3f,%.3f\n",
			total, c, ts,
			tdiv((double)total, t2), tdiv((double)c, t2));
	}
done:
	close(fd);
	return 0;
}

void
reflink_init(void)
{
	reflink_cmd.name = "reflink";
	reflink_cmd.altname = "rl";
	reflink_cmd.cfunc = reflink_f;
	reflink_cmd.argmin = 4;
	reflink_cmd.argmax = -1;
	reflink_cmd.flags = CMD_NOMAP_OK | CMD_FOREIGN_OK;
	reflink_cmd.args =
_("infile src_off dst_off len");
	reflink_cmd.oneline =
		_("reflinks a number of bytes at a specified offset");
	reflink_cmd.help = reflink_help;

	add_command(&reflink_cmd);
}
