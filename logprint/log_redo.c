/*
 * Copyright (c) 2000-2003,2005 Silicon Graphics, Inc.
 * Copyright (c) 2016 Oracle, Inc.
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
#include "libxfs.h"
#include "libxlog.h"

#include "logprint.h"

/* Extent Free Items */

static int
xfs_efi_copy_format(
	char			  *buf,
	uint			  len,
	struct xfs_efi_log_format *dst_efi_fmt,
	int			  continued)
{
	uint i;
	uint nextents = ((xfs_efi_log_format_t *)buf)->efi_nextents;
	uint dst_len = sizeof(xfs_efi_log_format_t) + (nextents - 1) * sizeof(xfs_extent_t);
	uint len32 = sizeof(xfs_efi_log_format_32_t) + (nextents - 1) * sizeof(xfs_extent_32_t);
	uint len64 = sizeof(xfs_efi_log_format_64_t) + (nextents - 1) * sizeof(xfs_extent_64_t);

	if (len == dst_len || continued) {
		memcpy((char *)dst_efi_fmt, buf, len);
		return 0;
	} else if (len == len32) {
		xfs_efi_log_format_32_t *src_efi_fmt_32 = (xfs_efi_log_format_32_t *)buf;

		dst_efi_fmt->efi_type	 = src_efi_fmt_32->efi_type;
		dst_efi_fmt->efi_size	 = src_efi_fmt_32->efi_size;
		dst_efi_fmt->efi_nextents = src_efi_fmt_32->efi_nextents;
		dst_efi_fmt->efi_id	   = src_efi_fmt_32->efi_id;
		for (i = 0; i < dst_efi_fmt->efi_nextents; i++) {
			dst_efi_fmt->efi_extents[i].ext_start =
				src_efi_fmt_32->efi_extents[i].ext_start;
			dst_efi_fmt->efi_extents[i].ext_len =
				src_efi_fmt_32->efi_extents[i].ext_len;
		}
		return 0;
	} else if (len == len64) {
		xfs_efi_log_format_64_t *src_efi_fmt_64 = (xfs_efi_log_format_64_t *)buf;

		dst_efi_fmt->efi_type	 = src_efi_fmt_64->efi_type;
		dst_efi_fmt->efi_size	 = src_efi_fmt_64->efi_size;
		dst_efi_fmt->efi_nextents = src_efi_fmt_64->efi_nextents;
		dst_efi_fmt->efi_id	   = src_efi_fmt_64->efi_id;
		for (i = 0; i < dst_efi_fmt->efi_nextents; i++) {
			dst_efi_fmt->efi_extents[i].ext_start =
				src_efi_fmt_64->efi_extents[i].ext_start;
			dst_efi_fmt->efi_extents[i].ext_len =
				src_efi_fmt_64->efi_extents[i].ext_len;
		}
		return 0;
	}
	fprintf(stderr, _("%s: bad size of efi format: %u; expected %u or %u; nextents = %u\n"),
		progname, len, len32, len64, nextents);
	return 1;
}

int
xlog_print_trans_efi(
	char			**ptr,
	uint			src_len,
	int			continued)
{
	xfs_efi_log_format_t	*src_f, *f = NULL;
	uint			dst_len;
	xfs_extent_t		*ex;
	int			i;
	int			error = 0;
	int			core_size = offsetof(xfs_efi_log_format_t, efi_extents);

	/*
	 * memmove to ensure 8-byte alignment for the long longs in
	 * xfs_efi_log_format_t structure
	 */
	if ((src_f = (xfs_efi_log_format_t *)malloc(src_len)) == NULL) {
		fprintf(stderr, _("%s: xlog_print_trans_efi: malloc failed\n"), progname);
		exit(1);
	}
	memmove((char*)src_f, *ptr, src_len);
	*ptr += src_len;

	/* convert to native format */
	dst_len = sizeof(xfs_efi_log_format_t) + (src_f->efi_nextents - 1) * sizeof(xfs_extent_t);

	if (continued && src_len < core_size) {
		printf(_("EFI: Not enough data to decode further\n"));
		error = 1;
		goto error;
	}

	if ((f = (xfs_efi_log_format_t *)malloc(dst_len)) == NULL) {
		fprintf(stderr, _("%s: xlog_print_trans_efi: malloc failed\n"), progname);
		exit(1);
	}
	if (xfs_efi_copy_format((char*)src_f, src_len, f, continued)) {
		error = 1;
		goto error;
	}

	printf(_("EFI:  #regs: %d	num_extents: %d  id: 0x%llx\n"),
		f->efi_size, f->efi_nextents, (unsigned long long)f->efi_id);

	if (continued) {
		printf(_("EFI free extent data skipped (CONTINUE set, no space)\n"));
		goto error;
	}

	ex = f->efi_extents;
	for (i=0; i < f->efi_nextents; i++) {
		printf("(s: 0x%llx, l: %d) ",
			(unsigned long long)ex->ext_start, ex->ext_len);
		if (i % 4 == 3) printf("\n");
		ex++;
	}
	if (i % 4 != 0)
		printf("\n");
error:
	free(src_f);
	free(f);
	return error;
}	/* xlog_print_trans_efi */

void
xlog_recover_print_efi(
	xlog_recover_item_t	*item)
{
	xfs_efi_log_format_t	*f, *src_f;
	xfs_extent_t		*ex;
	int			i;
	uint			src_len, dst_len;

	src_f = (xfs_efi_log_format_t *)item->ri_buf[0].i_addr;
	src_len = item->ri_buf[0].i_len;
	/*
	 * An xfs_efi_log_format structure contains a variable length array
	 * as the last field.
	 * Each element is of size xfs_extent_32_t or xfs_extent_64_t.
	 * Need to convert to native format.
	 */
	dst_len = sizeof(xfs_efi_log_format_t) +
		(src_f->efi_nextents - 1) * sizeof(xfs_extent_t);
	if ((f = (xfs_efi_log_format_t *)malloc(dst_len)) == NULL) {
		fprintf(stderr, _("%s: xlog_recover_print_efi: malloc failed\n"),
			progname);
		exit(1);
	}
	if (xfs_efi_copy_format((char*)src_f, src_len, f, 0)) {
		free(f);
		return;
	}

	printf(_("	EFI:  #regs:%d	num_extents:%d  id:0x%llx\n"),
		   f->efi_size, f->efi_nextents, (unsigned long long)f->efi_id);
	ex = f->efi_extents;
	printf("	");
	for (i=0; i< f->efi_nextents; i++) {
		printf("(s: 0x%llx, l: %d) ",
			(unsigned long long)ex->ext_start, ex->ext_len);
		if (i % 4 == 3)
			printf("\n");
		ex++;
	}
	if (i % 4 != 0)
		printf("\n");
	free(f);
}

int
xlog_print_trans_efd(char **ptr, uint len)
{
	xfs_efd_log_format_t *f;
	xfs_efd_log_format_t lbuf;
	/* size without extents at end */
	uint core_size = sizeof(xfs_efd_log_format_t) - sizeof(xfs_extent_t);

	/*
	 * memmove to ensure 8-byte alignment for the long longs in
	 * xfs_efd_log_format_t structure
	 */
	memmove(&lbuf, *ptr, MIN(core_size, len));
	f = &lbuf;
	*ptr += len;
	if (len >= core_size) {
		printf(_("EFD:  #regs: %d	num_extents: %d  id: 0x%llx\n"),
			f->efd_size, f->efd_nextents,
			(unsigned long long)f->efd_efi_id);

		/* don't print extents as they are not used */

		return 0;
	} else {
		printf(_("EFD: Not enough data to decode further\n"));
	return 1;
	}
}	/* xlog_print_trans_efd */

void
xlog_recover_print_efd(
	xlog_recover_item_t	*item)
{
	xfs_efd_log_format_t	*f;

	f = (xfs_efd_log_format_t *)item->ri_buf[0].i_addr;
	/*
	 * An xfs_efd_log_format structure contains a variable length array
	 * as the last field.
	 * Each element is of size xfs_extent_32_t or xfs_extent_64_t.
	 * However, the extents are never used and won't be printed.
	 */
	printf(_("	EFD:  #regs: %d	num_extents: %d  id: 0x%llx\n"),
		f->efd_size, f->efd_nextents,
		(unsigned long long)f->efd_efi_id);
}
