/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
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

#ifndef __LIBXFS_H__
#define __LIBXFS_H__

#include <xfs/platform_defs.h>

#include <xfs/list.h>
#include <xfs/hlist.h>
#include <xfs/cache.h>
#include <xfs/bitops.h>
#include <xfs/kmem.h>
#include <xfs/radix-tree.h>
#include <xfs/swab.h>
#include <xfs/atomic.h>

#include <xfs/xfs_types.h>
#include <xfs/xfs_fs.h>
#include <xfs/xfs_arch.h>

#include <xfs/xfs_shared.h>
#include <xfs/xfs_format.h>
#include <xfs/xfs_log_format.h>
#include <xfs/xfs_quota_defs.h>
#include <xfs/xfs_trans_resv.h>


/* CRC stuff, buffer API dependent on it */
extern uint32_t crc32_le(uint32_t crc, unsigned char const *p, size_t len);
extern uint32_t crc32c_le(uint32_t crc, unsigned char const *p, size_t len);

#define crc32(c,p,l)	crc32_le((c),(unsigned char const *)(p),(l))
#define crc32c(c,p,l)	crc32c_le((c),(unsigned char const *)(p),(l))

#include <xfs/xfs_cksum.h>

/*
 * This mirrors the kernel include for xfs_buf.h - it's implicitly included in
 * every files via a similar include in the kernel xfs_linux.h.
 */
#include <xfs/libxfs_io.h>

#include <xfs/xfs_bit.h>
#include <xfs/xfs_sb.h>
#include <xfs/xfs_mount.h>
#include <xfs/xfs_da_format.h>
#include <xfs/xfs_da_btree.h>
#include <xfs/xfs_dir2.h>
#include <xfs/xfs_bmap_btree.h>
#include <xfs/xfs_alloc_btree.h>
#include <xfs/xfs_ialloc_btree.h>
#include <xfs/xfs_attr_sf.h>
#include <xfs/xfs_inode_fork.h>
#include <xfs/xfs_inode_buf.h>
#include <xfs/xfs_inode.h>
#include <xfs/xfs_alloc.h>
#include <xfs/xfs_btree.h>
#include <xfs/xfs_btree_trace.h>
#include <xfs/xfs_bmap.h>
#include <xfs/xfs_trace.h>
#include <xfs/xfs_trans.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#ifndef XFS_SUPER_MAGIC
#define XFS_SUPER_MAGIC 0x58465342
#endif

#define xfs_isset(a,i)	((a)[(i)/(sizeof((a))*NBBY)] & (1<<((i)%(sizeof((a))*NBBY))))

/*
 * Argument structure for libxfs_init().
 */
typedef struct {
				/* input parameters */
	char            *volname;       /* pathname of volume */
	char            *dname;         /* pathname of data "subvolume" */
	char            *logname;       /* pathname of log "subvolume" */
	char            *rtname;        /* pathname of realtime "subvolume" */
	int             isreadonly;     /* filesystem is only read in applic */
	int             isdirect;       /* we can attempt to use direct I/O */
	int             disfile;        /* data "subvolume" is a regular file */
	int             dcreat;         /* try to create data subvolume */
	int             lisfile;        /* log "subvolume" is a regular file */
	int             lcreat;         /* try to create log subvolume */
	int             risfile;        /* realtime "subvolume" is a reg file */
	int             rcreat;         /* try to create realtime subvolume */
	int		setblksize;	/* attempt to set device blksize */
	int		usebuflock;	/* lock xfs_buf_t's - for MT usage */
				/* output results */
	dev_t           ddev;           /* device for data subvolume */
	dev_t           logdev;         /* device for log subvolume */
	dev_t           rtdev;          /* device for realtime subvolume */
	long long       dsize;          /* size of data subvolume (BBs) */
	long long       logBBsize;      /* size of log subvolume (BBs) */
					/* (blocks allocated for use as
					 * log is stored in mount structure) */
	long long       logBBstart;     /* start block of log subvolume (BBs) */
	long long       rtsize;         /* size of realtime subvolume (BBs) */
	int		dbsize;		/* data subvolume device blksize */
	int		lbsize;		/* log subvolume device blksize */
	int		rtbsize;	/* realtime subvolume device blksize */
	int             dfd;            /* data subvolume file descriptor */
	int             logfd;          /* log subvolume file descriptor */
	int             rtfd;           /* realtime subvolume file descriptor */
	int		icache_flags;	/* cache init flags */
	int		bcache_flags;	/* cache init flags */
} libxfs_init_t;

#define LIBXFS_EXIT_ON_FAILURE	0x0001	/* exit the program if a call fails */
#define LIBXFS_ISREADONLY	0x0002	/* disallow all mounted filesystems */
#define LIBXFS_ISINACTIVE	0x0004	/* allow mounted only if mounted ro */
#define LIBXFS_DANGEROUSLY	0x0008	/* repairing a device mounted ro    */
#define LIBXFS_EXCLUSIVELY	0x0010	/* disallow other accesses (O_EXCL) */
#define LIBXFS_DIRECT		0x0020	/* can use direct I/O, not buffered */

extern char	*progname;
extern int	libxfs_init (libxfs_init_t *);
extern void	libxfs_destroy (void);
extern int	libxfs_device_to_fd (dev_t);
extern dev_t	libxfs_device_open (char *, int, int, int);
extern void	libxfs_device_zero(struct xfs_buftarg *, xfs_daddr_t, uint);
extern void	libxfs_device_close (dev_t);
extern int	libxfs_device_alignment (void);
extern void	libxfs_report(FILE *);
extern void	platform_findsizes(char *path, int fd, long long *sz, int *bsz);
extern int	platform_nproc(void);

/* check or write log footer: specify device, log size in blocks & uuid */
typedef xfs_caddr_t (libxfs_get_block_t)(xfs_caddr_t, int, void *);

extern int	libxfs_log_clear (struct xfs_buftarg *, xfs_daddr_t, uint,
				uuid_t *, int, int, int);
extern int	libxfs_log_header (xfs_caddr_t, uuid_t *, int, int, int,
				libxfs_get_block_t *, void *);


#define LIBXFS_ATTR_ROOT	0x0002	/* use attrs in root namespace */
#define LIBXFS_ATTR_SECURE	0x0008	/* use attrs in security namespace */
#define LIBXFS_ATTR_CREATE	0x0010	/* create, but fail if attr exists */
#define LIBXFS_ATTR_REPLACE	0x0020	/* set, but fail if attr not exists */

/* Shared utility routines */
extern unsigned int	libxfs_log2_roundup(unsigned int i);

extern int	libxfs_alloc_file_space (struct xfs_inode *, xfs_off_t,
				xfs_off_t, int, int);
extern int	libxfs_bmap_finish(xfs_trans_t **, xfs_bmap_free_t *, int *);

extern void 	libxfs_fs_repair_cmn_err(int, struct xfs_mount *, char *, ...);
extern void	libxfs_fs_cmn_err(int, struct xfs_mount *, char *, ...);

/* XXX: this is messy and needs fixing */
#ifndef __LIBXFS_INTERNAL_XFS_H__
extern void cmn_err(int, char *, ...);
enum ce { CE_DEBUG, CE_CONT, CE_NOTE, CE_WARN, CE_ALERT, CE_PANIC };
#endif


extern int		libxfs_nproc(void);
extern unsigned long	libxfs_physmem(void);	/* in kilobytes */

#include <xfs/xfs_ialloc.h>

#include <xfs/xfs_attr_leaf.h>
#include <xfs/xfs_attr_remote.h>
#include <xfs/xfs_trans_space.h>

#define XFS_INOBT_IS_FREE_DISK(rp,i)		\
			((be64_to_cpu((rp)->ir_free) & XFS_INOBT_MASK(i)) != 0)

/*
 * public xfs kernel routines to be called as libxfs_*
 *
 * These are all present in the xfs_* namespace but we don't want that namespace
 * to be used or even exposed, and hence we declare them here explicitly if we
 * aren't directly building the libxfs code itself.
 *
 * XXX: This needs to be more formalised in the shared header files so that
 * these declarations can go away, and so there's documentation in the kernel
 * code base that certain functions are shared with userspace.
 */
#ifndef __LIBXFS_INTERNAL_XFS_H__

/* xfs_sb.h */
void	libxfs_log_sb(struct xfs_trans *tp);
void	libxfs_sb_from_disk(struct xfs_sb *, struct xfs_dsb *);
void	libxfs_sb_to_disk(struct xfs_dsb *, struct xfs_sb *);
void	libxfs_sb_quota_from_disk(struct xfs_sb *sbp);

/* xfs_bmap.h */
int	libxfs_bmapi_write(struct xfs_trans *tp, struct xfs_inode *ip,
		xfs_fileoff_t bno, xfs_filblks_t len, int flags,
		xfs_fsblock_t *firstblock, xfs_extlen_t total,
		struct xfs_bmbt_irec *mval, int *nmap,
		struct xfs_bmap_free *flist);
int	libxfs_bunmapi(struct xfs_trans *tp, struct xfs_inode *ip,
		xfs_fileoff_t bno, xfs_filblks_t len, int flags,
		xfs_extnum_t nexts, xfs_fsblock_t *firstblock,
		struct xfs_bmap_free *flist, int *done);
void	libxfs_bmap_cancel(struct xfs_bmap_free *flist);
int	libxfs_bmap_last_offset(struct xfs_inode *ip,
		xfs_fileoff_t *unused, int whichfork);
int	libxfs_bmap_finish(struct xfs_trans **tp, struct xfs_bmap_free *flist,
		int *committed);

/* xfs_dir2.h */
int	libxfs_dir_init(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_inode *pdp);
int	libxfs_dir_createname(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name, xfs_ino_t inum,
				xfs_fsblock_t *first,
				struct xfs_bmap_free *flist, xfs_extlen_t tot);
int	libxfs_dir_lookup(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name, xfs_ino_t *inum,
				struct xfs_name *ci_name);
int	libxfs_dir_replace(struct xfs_trans *tp, struct xfs_inode *dp,
				struct xfs_name *name, xfs_ino_t inum,
				xfs_fsblock_t *first,
				struct xfs_bmap_free *flist, xfs_extlen_t tot);

int	libxfs_dir2_isblock(struct xfs_da_args *args, int *r);
int	libxfs_dir2_isleaf(struct xfs_da_args *args, int *r);
int	libxfs_dir2_shrink_inode(struct xfs_da_args *args, xfs_dir2_db_t db,
				struct xfs_buf *bp);

void	libxfs_dir2_data_freescan(struct xfs_da_geometry *geo,
		const struct xfs_dir_ops *ops,
		struct xfs_dir2_data_hdr *hdr, int *loghead);

void	libxfs_dir2_data_log_entry(struct xfs_da_args *args,
		struct xfs_buf *bp, struct xfs_dir2_data_entry *dep);
void	libxfs_dir2_data_log_header(struct xfs_da_args *args,
		struct xfs_buf *bp);
void	libxfs_dir2_data_log_unused(struct xfs_da_args *args,
		struct xfs_buf *bp, struct xfs_dir2_data_unused *dup);
void	libxfs_dir2_data_make_free(struct xfs_da_args *args,
		struct xfs_buf *bp, xfs_dir2_data_aoff_t offset,
		xfs_dir2_data_aoff_t len, int *needlogp, int *needscanp);
void	libxfs_dir2_data_use_free(struct xfs_da_args *args,
		struct xfs_buf *bp, struct xfs_dir2_data_unused *dup,
		xfs_dir2_data_aoff_t offset, xfs_dir2_data_aoff_t len,
		int *needlogp, int *needscanp);

/* xfs_da_btree.h */
uint	libxfs_da_hashname(const __uint8_t *name_string, int name_length);
int	libxfs_da_shrink_inode(xfs_da_args_t *args, xfs_dablk_t dead_blkno,
					  struct xfs_buf *dead_buf);
int	libxfs_da_read_buf(struct xfs_trans *trans, struct xfs_inode *dp,
			       xfs_dablk_t bno, xfs_daddr_t mappedbno,
			       struct xfs_buf **bpp, int whichfork,
			       const struct xfs_buf_ops *ops);


/* xfs_inode_buf.h */
void	libxfs_dinode_calc_crc(struct xfs_mount *, struct xfs_dinode *);

/* xfs_inode_fork.h */
void	libxfs_idata_realloc(struct xfs_inode *, int, int);

/* xfs_symlink_remote.h */
int	libxfs_symlink_blocks(struct xfs_mount *mp, int pathlen);
bool	libxfs_symlink_hdr_ok(xfs_ino_t ino, uint32_t offset, uint32_t size,
			      struct xfs_buf *bp);

/* xfs_bit.h */
/* XXX: these are special as they are static inline functions in the header */
#define libxfs_highbit32	xfs_highbit32
#define libxfs_highbit64	xfs_highbit64

/* xfs_alloc.c */
int libxfs_alloc_fix_freelist(xfs_alloc_arg_t *, int);

/* xfs_attr.c */
int libxfs_attr_get(struct xfs_inode *, const unsigned char *,
					unsigned char *, int *, int);
int libxfs_attr_set(struct xfs_inode *, const unsigned char *,
					unsigned char *, int, int);
int libxfs_attr_remove(struct xfs_inode *, const unsigned char *, int);

/* xfs_bmap.c */
xfs_bmbt_rec_host_t *xfs_bmap_search_extents(struct xfs_inode *, xfs_fileoff_t,
				int, int *, xfs_extnum_t *, xfs_bmbt_irec_t *,
				xfs_bmbt_irec_t *);
void libxfs_bmbt_get_all(struct xfs_bmbt_rec_host *r, struct xfs_bmbt_irec *s);

static inline void
libxfs_bmbt_disk_get_all(
	struct xfs_bmbt_rec	*rp,
	struct xfs_bmbt_irec	*irec)
{
	struct xfs_bmbt_rec_host hrec;

	hrec.l0 = be64_to_cpu(rp->l0);
	hrec.l1 = be64_to_cpu(rp->l1);
	libxfs_bmbt_get_all(&hrec, irec);
}

void libxfs_dinode_from_disk(struct xfs_icdinode *,
			     struct xfs_dinode *);
bool libxfs_dinode_verify(struct xfs_mount *mp, xfs_ino_t ino,
		       struct xfs_dinode *dip);

/* this file */
/* XXX: this needs cleanup  like the xfs_bit.h stuff */
#define libxfs_verify_cksum	xfs_verify_cksum
#define libxfs_buf_verify_cksum	xfs_buf_verify_cksum
#define libxfs_buf_update_cksum	xfs_buf_update_cksum

#endif /* __LIBXFS_INTERNAL_XFS_H__ */

/* XXX: this is clearly a bug - a shared header needs to export this */
/* xfs_rtalloc.c */
int libxfs_rtfree_extent(struct xfs_trans *, xfs_rtblock_t, xfs_extlen_t);

#endif	/* __LIBXFS_H__ */
