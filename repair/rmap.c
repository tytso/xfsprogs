/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <libxfs.h>
#include "btree.h"
#include "err_protos.h"
#include "libxlog.h"
#include "incore.h"
#include "globals.h"
#include "dinode.h"
#include "slab.h"
#include "rmap.h"

#undef RMAP_DEBUG

#ifdef RMAP_DEBUG
# define dbg_printf(f, a...)  do {printf(f, ## a); fflush(stdout); } while (0)
#else
# define dbg_printf(f, a...)
#endif

/* per-AG rmap object anchor */
struct xfs_ag_rmap {
	struct xfs_slab	*ar_rmaps;		/* rmap observations, p4 */
	struct xfs_slab	*ar_raw_rmaps;		/* unmerged rmaps */
};

static struct xfs_ag_rmap *ag_rmaps;

/*
 * Compare rmap observations for array sorting.
 */
static int
rmap_compare(
	const void		*a,
	const void		*b)
{
	const struct xfs_rmap_irec	*pa;
	const struct xfs_rmap_irec	*pb;
	__u64			oa;
	__u64			ob;

	pa = a; pb = b;
	oa = xfs_rmap_irec_offset_pack(pa);
	ob = xfs_rmap_irec_offset_pack(pb);

	if (pa->rm_startblock < pb->rm_startblock)
		return -1;
	else if (pa->rm_startblock > pb->rm_startblock)
		return 1;
	else if (pa->rm_owner < pb->rm_owner)
		return -1;
	else if (pa->rm_owner > pb->rm_owner)
		return 1;
	else if (oa < ob)
		return -1;
	else if (oa > ob)
		return 1;
	else
		return 0;
}

/*
 * Returns true if we must reconstruct either the reference count or reverse
 * mapping trees.
 */
bool
needs_rmap_work(
	struct xfs_mount	*mp)
{
	return xfs_sb_version_hasrmapbt(&mp->m_sb);
}

/*
 * Initialize per-AG reverse map data.
 */
void
init_rmaps(
	struct xfs_mount	*mp)
{
	xfs_agnumber_t		i;
	int			error;

	if (!needs_rmap_work(mp))
		return;

	ag_rmaps = calloc(mp->m_sb.sb_agcount, sizeof(struct xfs_ag_rmap));
	if (!ag_rmaps)
		do_error(_("couldn't allocate per-AG reverse map roots\n"));

	for (i = 0; i < mp->m_sb.sb_agcount; i++) {
		error = init_slab(&ag_rmaps[i].ar_rmaps,
				sizeof(struct xfs_rmap_irec));
		if (error)
			do_error(
_("Insufficient memory while allocating reverse mapping slabs."));
		error = init_slab(&ag_rmaps[i].ar_raw_rmaps,
				  sizeof(struct xfs_rmap_irec));
		if (error)
			do_error(
_("Insufficient memory while allocating raw metadata reverse mapping slabs."));
	}
}

/*
 * Free the per-AG reverse-mapping data.
 */
void
free_rmaps(
	struct xfs_mount	*mp)
{
	xfs_agnumber_t		i;

	if (!needs_rmap_work(mp))
		return;

	for (i = 0; i < mp->m_sb.sb_agcount; i++) {
		free_slab(&ag_rmaps[i].ar_rmaps);
		free_slab(&ag_rmaps[i].ar_raw_rmaps);
	}
	free(ag_rmaps);
	ag_rmaps = NULL;
}

/*
 * Decide if two reverse-mapping records can be merged.
 */
bool
mergeable_rmaps(
	struct xfs_rmap_irec	*r1,
	struct xfs_rmap_irec	*r2)
{
	if (r1->rm_owner != r2->rm_owner)
		return false;
	if (r1->rm_startblock + r1->rm_blockcount != r2->rm_startblock)
		return false;
	if ((unsigned long long)r1->rm_blockcount + r2->rm_blockcount >
	    XFS_RMAP_LEN_MAX)
		return false;
	if (XFS_RMAP_NON_INODE_OWNER(r2->rm_owner))
		return true;
	/* must be an inode owner below here */
	if (r1->rm_flags != r2->rm_flags)
		return false;
	if (r1->rm_flags & XFS_RMAP_BMBT_BLOCK)
		return true;
	return r1->rm_offset + r1->rm_blockcount == r2->rm_offset;
}

/*
 * Add an observation about a block mapping in an inode's data or attribute
 * fork for later btree reconstruction.
 */
int
add_rmap(
	struct xfs_mount	*mp,
	xfs_ino_t		ino,
	int			whichfork,
	struct xfs_bmbt_irec	*irec)
{
	struct xfs_slab		*rmaps;
	struct xfs_rmap_irec	rmap;
	xfs_agnumber_t		agno;
	xfs_agblock_t		agbno;

	if (!needs_rmap_work(mp))
		return 0;

	agno = XFS_FSB_TO_AGNO(mp, irec->br_startblock);
	agbno = XFS_FSB_TO_AGBNO(mp, irec->br_startblock);
	ASSERT(agno != NULLAGNUMBER);
	ASSERT(agno < mp->m_sb.sb_agcount);
	ASSERT(agbno + irec->br_blockcount <= mp->m_sb.sb_agblocks);
	ASSERT(ino != NULLFSINO);
	ASSERT(whichfork == XFS_DATA_FORK || whichfork == XFS_ATTR_FORK);

	rmaps = ag_rmaps[agno].ar_rmaps;
	rmap.rm_owner = ino;
	rmap.rm_offset = irec->br_startoff;
	rmap.rm_flags = 0;
	if (whichfork == XFS_ATTR_FORK)
		rmap.rm_flags |= XFS_RMAP_ATTR_FORK;
	rmap.rm_startblock = agbno;
	rmap.rm_blockcount = irec->br_blockcount;
	if (irec->br_state == XFS_EXT_UNWRITTEN)
		rmap.rm_flags |= XFS_RMAP_UNWRITTEN;
	return slab_add(rmaps, &rmap);
}

/* add a raw rmap; these will be merged later */
static int
__add_raw_rmap(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len,
	uint64_t		owner,
	bool			is_attr,
	bool			is_bmbt)
{
	struct xfs_rmap_irec	rmap;

	ASSERT(len != 0);
	rmap.rm_owner = owner;
	rmap.rm_offset = 0;
	rmap.rm_flags = 0;
	if (is_attr)
		rmap.rm_flags |= XFS_RMAP_ATTR_FORK;
	if (is_bmbt)
		rmap.rm_flags |= XFS_RMAP_BMBT_BLOCK;
	rmap.rm_startblock = agbno;
	rmap.rm_blockcount = len;
	return slab_add(ag_rmaps[agno].ar_raw_rmaps, &rmap);
}

/*
 * Add a reverse mapping for an inode fork's block mapping btree block.
 */
int
add_bmbt_rmap(
	struct xfs_mount	*mp,
	xfs_ino_t		ino,
	int			whichfork,
	xfs_fsblock_t		fsbno)
{
	xfs_agnumber_t		agno;
	xfs_agblock_t		agbno;

	if (!needs_rmap_work(mp))
		return 0;

	agno = XFS_FSB_TO_AGNO(mp, fsbno);
	agbno = XFS_FSB_TO_AGBNO(mp, fsbno);
	ASSERT(agno != NULLAGNUMBER);
	ASSERT(agno < mp->m_sb.sb_agcount);
	ASSERT(agbno + 1 <= mp->m_sb.sb_agblocks);

	return __add_raw_rmap(mp, agno, agbno, 1, ino,
			whichfork == XFS_ATTR_FORK, true);
}

/*
 * Add a reverse mapping for a per-AG fixed metadata extent.
 */
int
add_ag_rmap(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		agbno,
	xfs_extlen_t		len,
	uint64_t		owner)
{
	if (!needs_rmap_work(mp))
		return 0;

	ASSERT(agno != NULLAGNUMBER);
	ASSERT(agno < mp->m_sb.sb_agcount);
	ASSERT(agbno + len <= mp->m_sb.sb_agblocks);

	return __add_raw_rmap(mp, agno, agbno, len, owner, false, false);
}

/*
 * Merge adjacent raw rmaps and add them to the main rmap list.
 */
int
fold_raw_rmaps(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno)
{
	struct xfs_slab_cursor	*cur = NULL;
	struct xfs_rmap_irec	*prev, *rec;
	size_t			old_sz;
	int			error;

	old_sz = slab_count(ag_rmaps[agno].ar_rmaps);
	if (slab_count(ag_rmaps[agno].ar_raw_rmaps) == 0)
		goto no_raw;
	qsort_slab(ag_rmaps[agno].ar_raw_rmaps, rmap_compare);
	error = init_slab_cursor(ag_rmaps[agno].ar_raw_rmaps, rmap_compare,
			&cur);
	if (error)
		goto err;

	prev = pop_slab_cursor(cur);
	rec = pop_slab_cursor(cur);
	while (rec) {
		if (mergeable_rmaps(prev, rec)) {
			prev->rm_blockcount += rec->rm_blockcount;
			rec = pop_slab_cursor(cur);
			continue;
		}
		error = slab_add(ag_rmaps[agno].ar_rmaps, prev);
		if (error)
			goto err;
		prev = rec;
		rec = pop_slab_cursor(cur);
	}
	if (prev) {
		error = slab_add(ag_rmaps[agno].ar_rmaps, prev);
		if (error)
			goto err;
	}
	free_slab(&ag_rmaps[agno].ar_raw_rmaps);
	error = init_slab(&ag_rmaps[agno].ar_raw_rmaps,
			sizeof(struct xfs_rmap_irec));
	if (error)
		do_error(
_("Insufficient memory while allocating raw metadata reverse mapping slabs."));
no_raw:
	if (old_sz)
		qsort_slab(ag_rmaps[agno].ar_rmaps, rmap_compare);
err:
	free_slab_cursor(&cur);
	return error;
}

#ifdef RMAP_DEBUG
static void
dump_rmap(
	const char		*msg,
	xfs_agnumber_t		agno,
	struct xfs_rmap_irec	*rmap)
{
	printf("%s: %p agno=%u pblk=%llu own=%lld lblk=%llu len=%u flags=0x%x\n",
		msg, rmap,
		(unsigned int)agno,
		(unsigned long long)rmap->rm_startblock,
		(unsigned long long)rmap->rm_owner,
		(unsigned long long)rmap->rm_offset,
		(unsigned int)rmap->rm_blockcount,
		(unsigned int)rmap->rm_flags);
}
#else
# define dump_rmap(m, a, r)
#endif
