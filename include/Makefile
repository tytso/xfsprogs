#
# Copyright (c) 2000-2006 Silicon Graphics, Inc.
# All Rights Reserved.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it would be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write the Free Software Foundation,
# Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#

TOPDIR = ..
include $(TOPDIR)/include/builddefs

QAHFILES = libxfs.h \
	libxlog.h \
	atomic.h \
	bitops.h \
	cache.h \
	hlist.h \
	kmem.h \
	list.h \
	parent.h \
	radix-tree.h \
	swab.h \
	xfs_arch.h \
	xfs_btree_trace.h \
	xfs_inode.h \
	xfs_log_recover.h \
	xfs_metadump.h \
	xfs_mount.h \
	xfs_trace.h \
	xfs_trans.h

HFILES = handle.h \
	jdm.h \
	$(PKG_PLATFORM).h \
	platform_defs.h \
	xfs.h \
	xqm.h

PHFILES = darwin.h freebsd.h irix.h linux.h gnukfreebsd.h
DKHFILES = volume.h fstyp.h dvh.h
LIBHFILES = command.h input.h path.h project.h
LSRCFILES = $(shell echo $(PHFILES) | sed -e "s/$(PKG_PLATFORM).h//g")
LSRCFILES += platform_defs.h.in builddefs.in buildmacros buildrules install-sh
LSRCFILES += $(DKHFILES) $(LIBHFILES)
LDIRT = disk
LDIRDIRT = xfs

default install: disk

disk:
	@echo "    [LN]     $@"
	$(Q)$(LN_S) . $@

# set up include/xfs header directory
headers: $(addsuffix -hdrs, $(HFILES) $(QAHFILES) $(PHFILES) $(DKHFILES) $(LIBHFILES))

%-hdrs: xfs
	$(Q)$(LN_S) -f $(PWD)/include/$* xfs/$*

xfs:
	@mkdir -p xfs


include $(BUILDRULES)

install-dev: default
	$(INSTALL) -m 755 -d $(PKG_INC_DIR)
	$(INSTALL) -m 644 $(HFILES) $(PKG_INC_DIR)

install-qa: install-dev
	$(INSTALL) -m 644 $(QAHFILES) $(PKG_INC_DIR)

