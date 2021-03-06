/*
  Copyright (c) 2008-2011 Gluster, Inc. <http://www.gluster.com>
  This file is part of GlusterFS.

  GlusterFS is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  GlusterFS is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
*/


#ifndef _GF_DIRENT_H
#define _GF_DIRENT_H

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "iatt.h"
#include "inode.h"

#define gf_dirent_size(name) (sizeof (gf_dirent_t) + strlen (name) + 1)

struct _dir_entry_t {
        struct _dir_entry_t *next;
	char                *name;
	char                *link;
	struct iatt          buf;
};


struct _gf_dirent_t {
	union {
		struct list_head             list;
		struct {
			struct _gf_dirent_t *next;
			struct _gf_dirent_t *prev;
		};
	};
	uint64_t                             d_ino;
	uint64_t                             d_off;
	uint32_t                             d_len;
	uint32_t                             d_type;
        struct iatt                          d_stat;
        dict_t                              *dict;
        inode_t                             *inode;
	char                                 d_name[0];
};


gf_dirent_t *gf_dirent_for_name (const char *name);
void gf_dirent_free (gf_dirent_t *entries);
int gf_link_inodes_from_dirent (xlator_t *this, inode_t *parent,
                                gf_dirent_t *entries);

#endif /* _GF_DIRENT_H */
