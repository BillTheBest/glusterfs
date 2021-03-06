/*
  Copyright (c) 2010-2011 Gluster, Inc. <http://www.gluster.com>
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

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "string.h"

#include "inode.h"
#include "nfs.h"
#include "nfs-inodes.h"
#include "nfs-fops.h"
#include "xlator.h"

#include <libgen.h>

#define inodes_nfl_to_prog_data(nflocal, pcbk, fram)                    \
        do {                                                            \
                nflocal = fram->local;                                  \
                fram->local = nflocal->proglocal;                       \
                pcbk = nflocal->progcbk;                                \
                nfs_fop_local_wipe (nflocal->nfsx, nflocal);            \
        } while (0)                                                     \

void
nfl_inodes_init (struct nfs_fop_local *nfl, inode_t *inode, inode_t *parent,
                 inode_t *newparent, const char *name, const char *newname)
{
        if (!nfl)
                return;

        if (inode)
                nfl->inode = inode_ref (inode);

        if (parent)
                nfl->parent = inode_ref (parent);

        if (newparent)
                nfl->newparent = inode_ref (newparent);

        if (name)
                strcpy (nfl->path, name);

        if (newname)
                strcpy (nfl->newpath, newname);

        return;
}


int32_t
nfs_inode_create_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, fd_t *fd, inode_t *inode
                      , struct iatt *buf, struct iatt *preparent,
                      struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = frame->local;
        fop_create_cbk_t        progcbk = NULL;
        inode_t                 *linked_inode = NULL;

        if (op_ret == -1)
                goto do_not_link;

        linked_inode = inode_link (inode, nfl->parent, nfl->path, buf);

do_not_link:
        /* NFS does not need it, upper layers should not expect the pointer to
         * be a valid fd.
         */
        fd_unref (fd);

        inodes_nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, fd, inode, buf,
                         preparent, postparent);

        if (linked_inode) {
                inode_lookup (linked_inode);
                inode_unref (linked_inode);
        }

        return 0;
}


int
nfs_inode_create (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu,
                  loc_t *pathloc, int flags, int mode, fop_create_cbk_t cbk,
                  void *local)
{
        struct nfs_fop_local    *nfl = NULL;
        int                     ret = -EFAULT;
        fd_t                    *newfd = NULL;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        nfs_fop_handle_local_init (NULL, nfsx, nfl, cbk, local, ret, err);

        newfd = fd_create (pathloc->inode, 0);
        if (!newfd) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to create new fd");
                ret = -ENOMEM;
                goto wipe_nfl;
        }

        /* The parent and base name will be needed to link the new inode
         * into the inode table.
         */
        nfl_inodes_init (nfl, pathloc->inode, pathloc->parent, NULL,
                         pathloc->name, NULL);
        ret = nfs_fop_create (nfsx, xl, nfu, pathloc, flags, mode, newfd,
                              nfs_inode_create_cbk, nfl);
wipe_nfl:
        if (ret < 0)
                nfs_fop_local_wipe (xl, nfl);

err:
        return ret;
}


int32_t
nfs_inode_mkdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = frame->local;
        fop_mkdir_cbk_t         progcbk = NULL;
        inode_t                 *linked_inode = NULL;

        if (op_ret == -1)
                goto do_not_link;

        linked_inode = inode_link (inode, nfl->parent, nfl->path, buf);

do_not_link:
        inodes_nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, inode, buf,
                         preparent, postparent);

        if (linked_inode) {
                inode_lookup (linked_inode);
                inode_unref (linked_inode);
        }

        return 0;
}


int
nfs_inode_mkdir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                 int mode, fop_mkdir_cbk_t cbk, void *local)
{
        struct nfs_fop_local    *nfl = NULL;
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        nfs_fop_handle_local_init (NULL, nfsx, nfl, cbk, local, ret, err);
        nfl_inodes_init (nfl, pathloc->inode, pathloc->parent, NULL,
                         pathloc->name, NULL);
        ret = nfs_fop_mkdir (nfsx, xl, nfu, pathloc, mode, nfs_inode_mkdir_cbk,
                             nfl);
        if (ret < 0)
                nfs_fop_local_wipe (nfsx, nfl);

err:
        return ret;
}


int32_t
nfs_inode_open_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, fd_t *fd)
{

        struct nfs_fop_local    *nfl = NULL;
        fop_open_cbk_t          progcbk = NULL;

        if ((op_ret == -1) && (fd))
                fd_unref (fd);
        /* Not needed here since the fd is cached in higher layers and the bind
         * must happen atomically when the fd gets added to the fd LRU.
         */
/*        else
                fd_bind (fd);
*/
        inodes_nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, fd);
        return 0;
}


int
nfs_inode_open (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
                int32_t flags, int32_t wbflags, fop_open_cbk_t cbk, void *local)
{
        struct nfs_fop_local    *nfl = NULL;
        fd_t                    *newfd = NULL;
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!loc) || (!nfu))
                return ret;

        newfd = fd_create (loc->inode, 0);
        if (!newfd) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to create fd");
                ret = -ENOMEM;
                goto err;
        }

        nfs_fop_handle_local_init (NULL, nfsx, nfl, cbk, local, ret, fd_err);
        ret = nfs_fop_open (nfsx, xl, nfu, loc, flags, newfd, wbflags,
                            nfs_inode_open_cbk, nfl);

        if (ret < 0)
                nfs_fop_local_wipe (xl, nfl);

fd_err:
        if (ret < 0)
                if (newfd)
                        fd_unref (newfd);

err:

        return ret;
}



int32_t
nfs_inode_rename_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *buf,
                      struct iatt *preoldparent, struct iatt *postoldparent,
                      struct iatt *prenewparent, struct iatt *postnewparent)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_rename_cbk_t        progcbk = NULL;

        nfl = frame->local;
        if (op_ret == -1)
                goto do_not_link;

        inode_rename (this->itable, nfl->parent, nfl->path, nfl->newparent,
                      nfl->newpath, nfl->inode, buf);

do_not_link:
        inodes_nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, buf,
                         preoldparent, postoldparent, prenewparent,
                         postnewparent);
        return 0;
}


int
nfs_inode_rename (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc,
                  loc_t *newloc, fop_rename_cbk_t cbk, void *local)
{
        struct nfs_fop_local    *nfl = NULL;
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!oldloc) || (!newloc))
                return ret;

        nfs_fop_handle_local_init (NULL, nfsx, nfl, cbk, local, ret, err);
        nfl_inodes_init (nfl, oldloc->inode, oldloc->parent, newloc->parent,
                         oldloc->name, newloc->name);
        ret = nfs_fop_rename (nfsx,xl, nfu, oldloc, newloc, nfs_inode_rename_cbk
                              , nfl);

err:
        if (ret < 0)
                nfs_fop_local_wipe (xl, nfl);

        return ret;
}


int32_t
nfs_inode_link_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                    int32_t op_ret, int32_t op_errno, inode_t *inode,
                    struct iatt *buf, struct iatt *preparent,
                    struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_link_cbk_t          progcbk = NULL;
        inode_t                 *linked_inode = NULL;

        if (op_ret == -1)
                goto do_not_link;

        nfl = frame->local;
        linked_inode = inode_link (inode, nfl->newparent, nfl->path, buf);

do_not_link:
        inodes_nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, inode, buf,
                         preparent, postparent);

        if (linked_inode) {
                inode_lookup (linked_inode);
                inode_unref (linked_inode);
        }

        return 0;
}


int
nfs_inode_link (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *oldloc,
                loc_t *newloc, fop_link_cbk_t cbk, void *local)
{
        struct nfs_fop_local            *nfl = NULL;
        int                             ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!oldloc) || (!newloc) || (!nfu))
                return -EFAULT;

        nfs_fop_handle_local_init (NULL, nfsx, nfl, cbk, local, ret, err);
        nfl_inodes_init (nfl, NULL, NULL, newloc->parent, newloc->name, NULL);
        ret = nfs_fop_link (nfsx, xl, nfu, oldloc, newloc, nfs_inode_link_cbk,
                            nfl);

err:
        if (ret < 0)
                nfs_fop_local_wipe (xl, nfl);

        return ret;
}


int32_t
nfs_inode_unlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                      int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                      struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_unlink_cbk_t        progcbk = NULL;

        nfl = frame->local;

        if (op_ret == -1)
                goto do_not_unlink;

        inode_unlink (nfl->inode, nfl->parent, nfl->path);
        inode_forget (nfl->inode, 0);

do_not_unlink:
        inodes_nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, preparent,
                         postparent);
        return 0;
}


int
nfs_inode_unlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                  fop_unlink_cbk_t cbk, void *local)
{
        struct nfs_fop_local            *nfl = NULL;
        int                             ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return -EFAULT;

        nfs_fop_handle_local_init (NULL, nfsx, nfl, cbk, local, ret, err);
        nfl_inodes_init (nfl, pathloc->inode, pathloc->parent, NULL,
                         pathloc->name, NULL);
        ret = nfs_fop_unlink (nfsx, xl, nfu, pathloc, nfs_inode_unlink_cbk,nfl);

err:
        if (ret < 0)
                nfs_fop_local_wipe (xl, nfl);

        return ret;
}


int32_t
nfs_inode_rmdir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, struct iatt *preparent,
                     struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_rmdir_cbk_t         progcbk = NULL;

        nfl = frame->local;

        if (op_ret == -1)
                goto do_not_unlink;

        inode_unlink (nfl->inode, nfl->parent, nfl->path);
        inode_forget (nfl->inode, 0);

do_not_unlink:
        inodes_nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, preparent,
                        postparent);

        return 0;
}


int
nfs_inode_rmdir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                 fop_rmdir_cbk_t cbk, void *local)
{
        struct nfs_fop_local    *nfl = NULL;
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        nfs_fop_handle_local_init (NULL, nfsx, nfl, cbk, local, ret, err);
        nfl_inodes_init (nfl, pathloc->inode, pathloc->parent, NULL,
                         pathloc->name, NULL);

        ret = nfs_fop_rmdir (nfsx, xl, nfu, pathloc, nfs_inode_rmdir_cbk, nfl);

err:
        if (ret < 0)
                nfs_fop_local_wipe (xl, nfl);
        return ret;
}


int32_t
nfs_inode_mknod_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                     int32_t op_ret, int32_t op_errno, inode_t *inode,
                     struct iatt *buf, struct iatt *preparent,
                     struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_mknod_cbk_t         progcbk = NULL;
        inode_t                 *linked_inode = NULL;

        nfl = frame->local;

        if (op_ret == -1)
                goto do_not_link;

        linked_inode = inode_link (inode, nfl->parent, nfl->path, buf);

do_not_link:
        inodes_nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, inode, buf,
                         preparent, postparent);

        if (linked_inode) {
                inode_lookup (linked_inode);
                inode_unref (linked_inode);
        }

        return 0;
}


int
nfs_inode_mknod (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *pathloc,
                 mode_t mode, dev_t dev, fop_mknod_cbk_t cbk, void *local)
{
        struct nfs_fop_local    *nfl = NULL;
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!pathloc) || (!nfu))
                return ret;

        nfs_fop_handle_local_init (NULL, nfsx, nfl, cbk, local, ret, err);
        nfl_inodes_init (nfl, pathloc->inode, pathloc->parent, NULL,
                         pathloc->name, NULL);

        ret = nfs_fop_mknod (nfsx, xl, nfu, pathloc, mode, dev,
                             nfs_inode_mknod_cbk, nfl);

err:
        if (ret < 0)
                nfs_fop_local_wipe (xl, nfl);

        return ret;
}


int32_t
nfs_inode_symlink_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, inode_t *inode,
                       struct iatt *buf, struct iatt *preparent,
                       struct iatt *postparent)
{
        struct nfs_fop_local    *nfl = NULL;
        fop_symlink_cbk_t       progcbk = NULL;
        inode_t                 *linked_inode = NULL;

        nfl = frame->local;
        if (op_ret == -1)
                goto do_not_link;

        linked_inode = inode_link (inode, nfl->parent, nfl->path, buf);

do_not_link:
        inodes_nfl_to_prog_data (nfl, progcbk, frame);
        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, inode, buf,
                         preparent, postparent);

        if (linked_inode) {
                inode_lookup (linked_inode);
                inode_unref (linked_inode);
        }

        return 0;
}


int
nfs_inode_symlink (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, char *target,
                   loc_t *pathloc, fop_symlink_cbk_t cbk, void *local)
{
        struct nfs_fop_local    *nfl = NULL;
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!target) || (!pathloc) || (!nfu))
                return ret;

        nfs_fop_handle_local_init (NULL, nfsx, nfl, cbk, local, ret, err);
        nfl_inodes_init (nfl, pathloc->inode, pathloc->parent, NULL,
                         pathloc->name, NULL);
        ret = nfs_fop_symlink (nfsx, xl, nfu, target, pathloc,
                               nfs_inode_symlink_cbk, nfl);

err:
        if (ret < 0)
                nfs_fop_local_wipe (xl, nfl);

        return ret;
}

int32_t
nfs_inode_opendir_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
                       int32_t op_ret, int32_t op_errno, fd_t *fd)
{

        struct nfs_fop_local    *nfl = NULL;
        fop_open_cbk_t          progcbk = NULL;

        if ((op_ret == -1) && (fd))
                fd_unref (fd);
        else
                fd_bind (fd);

        inodes_nfl_to_prog_data (nfl, progcbk, frame);

        if (progcbk)
                progcbk (frame, cookie, this, op_ret, op_errno, fd);
        return 0;
}


int
nfs_inode_opendir (xlator_t *nfsx, xlator_t *xl, nfs_user_t *nfu, loc_t *loc,
                   fop_opendir_cbk_t cbk, void *local)
{
        struct nfs_fop_local    *nfl = NULL;
        fd_t                    *newfd = NULL;
        int                     ret = -EFAULT;

        if ((!nfsx) || (!xl) || (!loc) || (!nfu))
                return ret;

        newfd = fd_create (loc->inode, 0);
        if (!newfd) {
                gf_log (GF_NFS, GF_LOG_ERROR, "Failed to create fd");
                ret = -ENOMEM;
                goto err;
        }

        nfs_fop_handle_local_init (NULL, nfsx, nfl, cbk, local, ret, err);
        ret = nfs_fop_opendir (nfsx, xl, nfu, loc, newfd,
                               nfs_inode_opendir_cbk, nfl);

err:
        if (ret < 0) {
                if (newfd)
                        fd_unref (newfd);
                nfs_fop_local_wipe (xl, nfl);
        }

        return ret;
}
