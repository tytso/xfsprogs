/*
 * Copyright (c) 2004-2006 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#ifndef __XFS_DARWIN_H__
#define __XFS_DARWIN_H__

#include <uuid/uuid.h>
#include <libgen.h>
#include <sys/vm.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <ftw.h>
#include <mach/mach_time.h>
#include <inttypes.h>
#include <stdio.h>

#include <machine/endian.h>
#define __BYTE_ORDER	BYTE_ORDER
#define __BIG_ENDIAN	BIG_ENDIAN
#define __LITTLE_ENDIAN	LITTLE_ENDIAN

#include <sys/syscall.h>
# ifndef SYS_fsctl
#  define SYS_fsctl	242
# endif
static __inline__ int xfsctl(const char *path, int fd, int cmd, void *p)
{
	return syscall(SYS_fsctl, path, cmd, p, 0);
}

static __inline__ int platform_test_xfs_fd(int fd)
{
	struct statfs buf;
	if (fstatfs(fd, &buf) < 0)
		return 0;
	return strncmp(buf.f_fstypename, "xfs", 4) == 0;
}

static __inline__ int platform_test_xfs_path(const char *path)
{
	struct statfs buf;
	if (statfs(path, &buf) < 0)
		return 0;
	return strncmp(buf.f_fstypename, "xfs", 4) == 0;
}

static __inline__ int platform_fstatfs(int fd, struct statfs *buf)
{
	return fstatfs(fd, buf);
}

static __inline__ void platform_getoptreset(void)
{
	extern int optreset;
	optreset = 0;
}

static __inline__ int platform_uuid_compare(uuid_t *uu1, uuid_t *uu2)
{
	return uuid_compare((const unsigned char *) uu1, (const unsigned char*) uu2);
}

static __inline__ void platform_uuid_unparse(uuid_t *uu, char *buffer)
{
	uuid_unparse(*uu, buffer);
}

static __inline__ int platform_uuid_parse(char *buffer, uuid_t *uu)
{
	return uuid_parse(buffer, *uu);
}

static __inline__ int platform_uuid_is_null(uuid_t *uu)
{
	return uuid_is_null(*uu);
}

static __inline__ void platform_uuid_generate(uuid_t *uu)
{
	uuid_generate(*uu);
}

static __inline__ void platform_uuid_clear(uuid_t *uu)
{
	uuid_clear(*uu);
}

static __inline__ void platform_uuid_copy(uuid_t *dst, uuid_t *src)
{
	uuid_copy(*dst, *src);
}

typedef unsigned char		__u8;
typedef signed char		__s8;
typedef unsigned short		__u16;
typedef signed short		__s16;
typedef unsigned int		__u32;
typedef signed int		__s32;
typedef unsigned long long int	__u64;
typedef signed long long int	__s64;

#define __int8_t	int8_t
#define __int16_t	int16_t
#define __int32_t	int32_t
#define __int32_t	int32_t
#define __int64_t	int64_t
#define __uint8_t	u_int8_t
#define __uint16_t	u_int16_t
#define __uint32_t	u_int32_t
#define __uint64_t	u_int64_t
#define off64_t		off_t

typedef off_t		xfs_off_t;
typedef u_int64_t	xfs_ino_t;
typedef u_int32_t	xfs_dev_t;
typedef int64_t		xfs_daddr_t;

#define stat64		stat
#define fstat64		fstat
#define lseek64		lseek
#define pread64		pread
#define pwrite64	pwrite
#define ftruncate64	ftruncate
#define fdatasync	fsync
#define memalign(a,sz)	valloc(sz)

#define O_LARGEFILE     0
#ifndef O_DIRECT
#define O_DIRECT        0
#endif
#ifndef O_SYNC
#define O_SYNC          0
#endif

#define EFSCORRUPTED	990	/* Filesystem is corrupted */
#define EFSBADCRC	991	/* Bad CRC detected */
#define constpp		char * const *

#define XATTR_SIZE_MAX 65536    /* size of an extended attribute value (64k) */
#define XATTR_LIST_MAX 65536    /* size of extended attribute namelist (64k) */

#define HAVE_FID	1

static __inline__ int
platform_discard_blocks(int fd, uint64_t start, uint64_t len)
{
	return 0;
}


/*
 * Dummy POSIX timer replacement
 */
#define CLOCK_REALTIME 1
typedef uint64_t timer_t;
typedef double   timer_c;
typedef clock_id_t clockid_t;
struct itimerspec
  {
    struct timespec it_interval;
    struct timespec it_value;
  };

static inline int timer_create (clockid_t __clock_id,
                         struct sigevent *__restrict __evp,
                         timer_t *__restrict __timerid)
{
	return 0;
}

static inline int timer_settime (timer_t __timerid, int __flags,
                          const struct itimerspec *__restrict __value,
                          struct itimerspec *__restrict __ovalue)
{
	return 0;
}

static inline int timer_delete (timer_t __timerid)
{
	return 0;
}

static inline int timer_gettime (timer_t __timerid, struct itimerspec *__value)
{
	return 0;
}

static inline int nftw64(const char *path, int (*fn)(const char *, const struct stat *ptr, int flag, struct FTW *), int depth,
         int flags)
{
	return nftw(path, fn, depth, flags);
}

#define MREMAP_FIXED 1
#define MREMAP_MAYMOVE 2
static inline void *mremap(void *old_address, size_t old_size,
                    size_t new_size, int flags, ... /* void *new_address */)
{
	return NULL;
}

/* FSR */

#define		_PATH_MOUNTED   "/etc/mtab"
#define		USE_DUMMY_XATTR

typedef int __fsblkcnt_t;
typedef int __fsfilcnt_t;
typedef long long int __fsblkcnt64_t;
typedef long long int __fsfilcnt64_t;

struct statvfs64
{
	unsigned long int f_bsize;
	unsigned long int f_frsize;
	__fsblkcnt64_t f_blocks;
	__fsblkcnt64_t f_bfree;
	__fsblkcnt64_t f_bavail;
	__fsfilcnt64_t f_files;
	__fsfilcnt64_t f_ffree;
	__fsfilcnt64_t f_favail;
	unsigned long int f_fsid;
	int __f_unused;
	unsigned long int f_flag;
	unsigned long int f_namemax;
	int __f_spare[6];
};

struct mntent
{
	char *mnt_fsname;		/* Device or server for filesystem.  */
	char *mnt_dir;		/* Directory mounted on.  */
	char *mnt_type;		/* Type of filesystem: ufs, nfs, etc.  */
	char *mnt_opts;		/* Comma-separated options for fs.  */
	int mnt_freq;		/* Dump frequency (in days).  */
	int mnt_passno;		/* Pass number for `fsck'.  */
};

static inline FILE *setmntent(const char *filename, const char *type)
{
	return NULL;
}

static inline int endmntent(FILE *fp)
{
	return 0;
}

static inline struct mntent *getmntent(FILE *fp)
{
	return NULL;
}

static inline int addmntent(FILE *fp, const struct mntent *mnt)
{
	return 0;
}

static inline char *hasmntopt(const struct mntent *mnt, const char *opt)
{
	return NULL;
}

static inline int statvfs64 (const char *__restrict __file, 
							 struct statvfs64 *__restrict __buf)
{
	return 0;
}

static inline int dummy_fsetxattr (int filedes, const char *name,
                       const void *value, size_t size, int flags)
{
	return 0;
}


#endif	/* __XFS_DARWIN_H__ */
