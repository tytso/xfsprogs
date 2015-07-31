/*
 * Copyright (c) 2000-2002,2005 Silicon Graphics, Inc.
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
#ifndef __XFS_ARCH_H__
#define __XFS_ARCH_H__

#if __BYTE_ORDER == __BIG_ENDIAN
#define	XFS_NATIVE_HOST	1
#else
#undef XFS_NATIVE_HOST
#endif

#ifdef XFS_NATIVE_HOST
#define cpu_to_be16(val)	((__force __be16)(__u16)(val))
#define cpu_to_be32(val)	((__force __be32)(__u32)(val))
#define cpu_to_be64(val)	((__force __be64)(__u64)(val))
#define be16_to_cpu(val)	((__force __u16)(__be16)(val))
#define be32_to_cpu(val)	((__force __u32)(__be32)(val))
#define be64_to_cpu(val)	((__force __u64)(__be64)(val))

#define cpu_to_le32(val)	((__force __be32)__swab32((__u32)(val)))
#define le32_to_cpu(val)	(__swab32((__force __u32)(__le32)(val)))

#define __constant_cpu_to_le32(val)	\
	((__force __le32)___constant_swab32((__u32)(val)))
#define __constant_cpu_to_be32(val)	\
	((__force __be32)(__u32)(val))
#else
#define cpu_to_be16(val)	((__force __be16)__swab16((__u16)(val)))
#define cpu_to_be32(val)	((__force __be32)__swab32((__u32)(val)))
#define cpu_to_be64(val)	((__force __be64)__swab64((__u64)(val)))
#define be16_to_cpu(val)	(__swab16((__force __u16)(__be16)(val)))
#define be32_to_cpu(val)	(__swab32((__force __u32)(__be32)(val)))
#define be64_to_cpu(val)	(__swab64((__force __u64)(__be64)(val)))

#define cpu_to_le32(val)	((__force __le32)(__u32)(val))
#define le32_to_cpu(val)	((__force __u32)(__le32)(val))

#define __constant_cpu_to_le32(val)	\
	((__force __le32)(__u32)(val))
#define __constant_cpu_to_be32(val)	\
	((__force __be32)___constant_swab32((__u32)(val)))
#endif

static inline void be16_add_cpu(__be16 *a, __s16 b)
{
	*a = cpu_to_be16(be16_to_cpu(*a) + b);
}

static inline void be32_add_cpu(__be32 *a, __s32 b)
{
	*a = cpu_to_be32(be32_to_cpu(*a) + b);
}

static inline void be64_add_cpu(__be64 *a, __s64 b)
{
	*a = cpu_to_be64(be64_to_cpu(*a) + b);
}

#endif	/* __XFS_ARCH_H__ */
