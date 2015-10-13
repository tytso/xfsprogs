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

static cmdinfo_t dedupe_cmd;

static void
dedupe_help(void)
{
	printf(_(
"\n"
" Links a range of bytes (in block size increments) from a file into a range \n"
" of bytes in the open file.  The contents of both file ranges must match.\n"
"\n"
" Example:\n"
" 'dedupe some_file 0 4096 32768' - links 32768 bytes from some_file at \n"
"                                    offset 0 to into the open file at \n"
"                                    position 4096\n"
"\n"
" Reflink a range of blocks from a given input file to the open file.  Both\n"
" files share the same range of physical disk blocks; a write to the shared\n"
" range of either file should result in the write landing in a new block and\n"
" that range of the file being remapped (i.e. copy-on-write).  Both files\n"
" must reside on the same filesystem, and the contents of both ranges must\n"
" match.\n"
" -w   -- call fdatasync(2) at the end (included in timing results)\n"
" -W   -- call fsync(2) at the end (included in timing results)\n"
"\n"));
}

static int
dedupe_f(
	int		argc,
	char		**argv)
{
	off64_t		soffset, doffset;
	long long	count, total;
	char		s1[64], s2[64], ts[64];
	char		*infile;
	int		Cflag, qflag, wflag, Wflag;
	struct xfs_ioctl_file_extent_same_args	*args = NULL;
	struct xfs_ioctl_file_extent_same_info	*info;
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
			return command_usage(&dedupe_cmd);
		}
	}
	if (optind != argc - 4)
		return command_usage(&dedupe_cmd);
	infile = argv[optind];
	optind++;
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

	c = IO_READONLY;
	fd = openfile(infile, NULL, c, 0);
	if (fd < 0)
		return 0;

	gettimeofday(&t1, NULL);
	args = calloc(1, sizeof(struct xfs_ioctl_file_extent_same_args) +
			 sizeof(struct xfs_ioctl_file_extent_same_info));
	if (!args)
		goto done;
	info = (struct xfs_ioctl_file_extent_same_info *)(args + 1);
	args->logical_offset = soffset;
	args->length = count;
	args->dest_count = 1;
	info->fd = file->fd;
	info->logical_offset = doffset;
	do {
		c = ioctl(fd, XFS_IOC_FILE_EXTENT_SAME, args);
		if (c)
			break;
		args->logical_offset += info->bytes_deduped;
		info->logical_offset += info->bytes_deduped;
		args->length -= info->bytes_deduped;
	} while (c == 0 && info->status == 0 && info->bytes_deduped > 0);
	if (c)
		perror(_("dedupe ioctl"));
	if (info->status < 0)
		printf("dedupe: %s\n", _(strerror(-info->status)));
	if (info->status == XFS_SAME_DATA_DIFFERS)
		printf(_("Extents did not match.\n"));
	if (c != 0 || info->status != 0)
		goto done;
	total = info->bytes_deduped;
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
	free(args);
	close(fd);
	return 0;
}

void
dedupe_init(void)
{
	dedupe_cmd.name = "dedupe";
	dedupe_cmd.altname = "dd";
	dedupe_cmd.cfunc = dedupe_f;
	dedupe_cmd.argmin = 4;
	dedupe_cmd.argmax = -1;
	dedupe_cmd.flags = CMD_NOMAP_OK | CMD_FOREIGN_OK;
	dedupe_cmd.args =
_("infile src_off dst_off len");
	dedupe_cmd.oneline =
		_("dedupes a number of bytes at a specified offset");
	dedupe_cmd.help = dedupe_help;

	add_command(&dedupe_cmd);
}
