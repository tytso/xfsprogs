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
static bool rmapbt_suspect;

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

static int
find_first_zero_bit(
	__uint64_t	mask)
{
	int		n;
	int		b = 0;

	for (n = 0; n < sizeof(mask) * NBBY && (mask & 1); n++, mask >>= 1)
		b++;

	return b;
}

static int
popcnt(
	__uint64_t	mask)
{
	int		n;
	int		b = 0;

	if (mask == 0)
		return 0;

	for (n = 0; n < sizeof(mask) * NBBY; n++, mask >>= 1)
		if (mask & 1)
			b++;

	return b;
}

/*
 * Add an allocation group's fixed metadata to the rmap list.  This includes
 * sb/agi/agf/agfl headers, inode chunks, and the log.
 */
int
add_fixed_ag_rmap_data(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno)
{
	xfs_fsblock_t		fsbno;
	xfs_agblock_t		agbno;
	ino_tree_node_t		*ino_rec;
	xfs_agino_t		agino;
	int			error;
	int			startidx;
	int			nr;

	if (!needs_rmap_work(mp))
		return 0;

	/* sb/agi/agf/agfl headers */
	error = add_ag_rmap(mp, agno, 0, XFS_BNO_BLOCK(mp),
			XFS_RMAP_OWN_FS);
	if (error)
		goto out;

	/* inodes */
	ino_rec = findfirst_inode_rec(agno);
	for (; ino_rec != NULL; ino_rec = next_ino_rec(ino_rec)) {
		if (xfs_sb_version_hassparseinodes(&mp->m_sb)) {
			startidx = find_first_zero_bit(ino_rec->ir_sparse);
			nr = XFS_INODES_PER_CHUNK - popcnt(ino_rec->ir_sparse);
		} else {
			startidx = 0;
			nr = XFS_INODES_PER_CHUNK;
		}
		nr /= mp->m_sb.sb_inopblock;
		if (nr == 0)
			nr = 1;
		agino = ino_rec->ino_startnum + startidx;
		agbno = XFS_AGINO_TO_AGBNO(mp, agino);
		if (XFS_AGINO_TO_OFFSET(mp, agino) == 0) {
			error = add_ag_rmap(mp, agno, agbno, nr,
					XFS_RMAP_OWN_INODES);
			if (error)
				goto out;
		}
	}

	/* log */
	fsbno = mp->m_sb.sb_logstart;
	if (fsbno && XFS_FSB_TO_AGNO(mp, fsbno) == agno) {
		agbno = XFS_FSB_TO_AGBNO(mp, mp->m_sb.sb_logstart);
		error = add_ag_rmap(mp, agno, agbno, mp->m_sb.sb_logblocks,
				XFS_RMAP_OWN_LOG);
		if (error)
			goto out;
	}
out:
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

/*
 * Return the number of rmap objects for an AG.
 */
size_t
rmap_record_count(
	struct xfs_mount		*mp,
	xfs_agnumber_t		agno)
{
	return slab_count(ag_rmaps[agno].ar_rmaps);
}

/*
 * Return a slab cursor that will return rmap objects in order.
 */
int
init_rmap_cursor(
	xfs_agnumber_t		agno,
	struct xfs_slab_cursor	**cur)
{
	return init_slab_cursor(ag_rmaps[agno].ar_rmaps, rmap_compare, cur);
}

/*
 * Disable the refcount btree check.
 */
void
rmap_avoid_check(void)
{
	rmapbt_suspect = true;
}

/* Look for an rmap in the rmapbt that matches a given rmap. */
static int
lookup_rmap(
	struct xfs_btree_cur	*bt_cur,
	struct xfs_rmap_irec	*rm_rec,
	struct xfs_rmap_irec	*tmp,
	int			*have)
{
	int			error;

	/* Use the regular btree retrieval routine. */
	error = -libxfs_rmap_lookup_le(bt_cur, rm_rec->rm_startblock,
				rm_rec->rm_blockcount,
				rm_rec->rm_owner, rm_rec->rm_offset,
				rm_rec->rm_flags, have);
	if (error)
		return error;
	if (*have == 0)
		return error;
	return -libxfs_rmap_get_rec(bt_cur, tmp, have);
}

/* Does the btree rmap cover the observed rmap? */
#define NEXTP(x)	((x)->rm_startblock + (x)->rm_blockcount)
#define NEXTL(x)	((x)->rm_offset + (x)->rm_blockcount)
static bool
is_good_rmap(
	struct xfs_rmap_irec	*observed,
	struct xfs_rmap_irec	*btree)
{
	/* Can't have mismatches in the flags or the owner. */
	if (btree->rm_flags != observed->rm_flags ||
	    btree->rm_owner != observed->rm_owner)
		return false;

	/*
	 * Btree record can't physically start after the observed
	 * record, nor can it end before the observed record.
	 */
	if (btree->rm_startblock > observed->rm_startblock ||
	    NEXTP(btree) < NEXTP(observed))
		return false;

	/* If this is metadata or bmbt, we're done. */
	if (XFS_RMAP_NON_INODE_OWNER(observed->rm_owner) ||
	    (observed->rm_flags & XFS_RMAP_BMBT_BLOCK))
		return true;
	/*
	 * Btree record can't logically start after the observed
	 * record, nor can it end before the observed record.
	 */
	if (btree->rm_offset > observed->rm_offset ||
	    NEXTL(btree) < NEXTL(observed))
		return false;

	return true;
}
#undef NEXTP
#undef NEXTL

/*
 * Compare the observed reverse mappings against what's in the ag btree.
 */
int
check_rmaps(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno)
{
	struct xfs_slab_cursor	*rm_cur;
	struct xfs_btree_cur	*bt_cur = NULL;
	int			error;
	int			have;
	struct xfs_buf		*agbp = NULL;
	struct xfs_rmap_irec	*rm_rec;
	struct xfs_rmap_irec	tmp;
	struct xfs_perag	*pag;		/* per allocation group data */

	if (!xfs_sb_version_hasrmapbt(&mp->m_sb))
		return 0;
	if (rmapbt_suspect) {
		if (no_modify && agno == 0)
			do_warn(_("would rebuild corrupt rmap btrees.\n"));
		return 0;
	}

	/* Create cursors to refcount structures */
	error = init_rmap_cursor(agno, &rm_cur);
	if (error)
		return error;

	error = -libxfs_alloc_read_agf(mp, NULL, agno, 0, &agbp);
	if (error)
		goto err;

	/* Leave the per-ag data "uninitialized" since we rewrite it later */
	pag = xfs_perag_get(mp, agno);
	pag->pagf_init = 0;
	xfs_perag_put(pag);

	bt_cur = libxfs_rmapbt_init_cursor(mp, NULL, agbp, agno);
	if (!bt_cur) {
		error = -ENOMEM;
		goto err;
	}

	rm_rec = pop_slab_cursor(rm_cur);
	while (rm_rec) {
		error = lookup_rmap(bt_cur, rm_rec, &tmp, &have);
		if (error)
			goto err;
		if (!have) {
			do_warn(
_("Missing reverse-mapping record for (%u/%u) %slen %u owner %"PRId64" \
%s%soff %"PRIu64"\n"),
				agno, rm_rec->rm_startblock,
				(rm_rec->rm_flags & XFS_RMAP_UNWRITTEN) ?
					_("unwritten ") : "",
				rm_rec->rm_blockcount,
				rm_rec->rm_owner,
				(rm_rec->rm_flags & XFS_RMAP_ATTR_FORK) ?
					_("attr ") : "",
				(rm_rec->rm_flags & XFS_RMAP_BMBT_BLOCK) ?
					_("bmbt ") : "",
				rm_rec->rm_offset);
			goto next_loop;
		}

		/* Compare each refcount observation against the btree's */
		if (!is_good_rmap(rm_rec, &tmp)) {
			do_warn(
_("Incorrect reverse-mapping: saw (%u/%u) %slen %u owner %"PRId64" %s%soff \
%"PRIu64"; should be (%u/%u) %slen %u owner %"PRId64" %s%soff %"PRIu64"\n"),
				agno, tmp.rm_startblock,
				(tmp.rm_flags & XFS_RMAP_UNWRITTEN) ?
					_("unwritten ") : "",
				tmp.rm_blockcount,
				tmp.rm_owner,
				(tmp.rm_flags & XFS_RMAP_ATTR_FORK) ?
					_("attr ") : "",
				(tmp.rm_flags & XFS_RMAP_BMBT_BLOCK) ?
					_("bmbt ") : "",
				tmp.rm_offset,
				agno, rm_rec->rm_startblock,
				(rm_rec->rm_flags & XFS_RMAP_UNWRITTEN) ?
					_("unwritten ") : "",
				rm_rec->rm_blockcount,
				rm_rec->rm_owner,
				(rm_rec->rm_flags & XFS_RMAP_ATTR_FORK) ?
					_("attr ") : "",
				(rm_rec->rm_flags & XFS_RMAP_BMBT_BLOCK) ?
					_("bmbt ") : "",
				rm_rec->rm_offset);
			goto next_loop;
		}
next_loop:
		rm_rec = pop_slab_cursor(rm_cur);
	}

err:
	if (bt_cur)
		libxfs_btree_del_cursor(bt_cur, XFS_BTREE_NOERROR);
	if (agbp)
		libxfs_putbuf(agbp);
	free_slab_cursor(&rm_cur);
	return 0;
}

/*
 * Compare the key fields of two rmap records -- positive if key1 > key2,
 * negative if key1 < key2, and zero if equal.
 */
__int64_t
rmap_diffkeys(
	struct xfs_rmap_irec	*kp1,
	struct xfs_rmap_irec	*kp2)
{
	__u64			oa;
	__u64			ob;
	__int64_t		d;
	struct xfs_rmap_irec	tmp;

	tmp = *kp1;
	tmp.rm_flags &= ~XFS_RMAP_REC_FLAGS;
	oa = xfs_rmap_irec_offset_pack(&tmp);
	tmp = *kp2;
	tmp.rm_flags &= ~XFS_RMAP_REC_FLAGS;
	ob = xfs_rmap_irec_offset_pack(&tmp);

	d = (__int64_t)kp1->rm_startblock - kp2->rm_startblock;
	if (d)
		return d;

	if (kp1->rm_owner > kp2->rm_owner)
		return 1;
	else if (kp2->rm_owner > kp1->rm_owner)
		return -1;

	if (oa > ob)
		return 1;
	else if (ob > oa)
		return -1;
	return 0;
}

/* Compute the high key of an rmap record. */
void
rmap_high_key_from_rec(
	struct xfs_rmap_irec	*rec,
	struct xfs_rmap_irec	*key)
{
	int			adj;

	adj = rec->rm_blockcount - 1;

	key->rm_startblock = rec->rm_startblock + adj;
	key->rm_owner = rec->rm_owner;
	key->rm_offset = rec->rm_offset;
	key->rm_flags = rec->rm_flags & XFS_RMAP_KEY_FLAGS;
	if (XFS_RMAP_NON_INODE_OWNER(rec->rm_owner) ||
	    (rec->rm_flags & XFS_RMAP_BMBT_BLOCK))
		return;
	key->rm_offset += adj;
}
