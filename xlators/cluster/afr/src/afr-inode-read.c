/*
  Copyright (c) 2007-2011 Gluster, Inc. <http://www.gluster.com>
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


#include <libgen.h>
#include <unistd.h>
#include <fnmatch.h>
#include <sys/time.h>
#include <stdlib.h>
#include <signal.h>

#ifndef _CONFIG_H
#define _CONFIG_H
#include "config.h"
#endif

#include "glusterfs.h"
#include "afr.h"
#include "dict.h"
#include "xlator.h"
#include "hashfn.h"
#include "logging.h"
#include "stack.h"
#include "list.h"
#include "call-stub.h"
#include "defaults.h"
#include "common-utils.h"
#include "compat-errno.h"
#include "compat.h"

/**
 * Common algorithm for inode read calls:
 *
 * - Try the fop on the first child that is up
 * - if we have failed due to ENOTCONN:
 *     try the next child
 *
 * Applicable to: access, stat, fstat, readlink, getxattr
 */

/* {{{ access */

int32_t
afr_access_cbk (call_frame_t *frame, void *cookie,
                xlator_t *this, int32_t op_ret, int32_t op_errno)
{
        afr_private_t * priv            = NULL;
        afr_local_t *   local           = NULL;
        xlator_t **     children        = NULL;
        int             unwind          = 1;
        int32_t         *last_index     = NULL;
        int32_t         next_call_child = -1;
        int32_t         read_child      = -1;
        int32_t         *fresh_children  = NULL;

        priv     = this->private;
        children = priv->children;

        local = frame->local;

        read_child = (long) cookie;

        if (op_ret == -1) {
                last_index = &local->cont.access.last_index;
                fresh_children = local->fresh_children;
                next_call_child = afr_next_call_child (fresh_children,
                                                       local->child_up,
                                                       priv->child_count,
                                                       last_index, read_child);
                if (next_call_child < 0)
                        goto out;

                unwind = 0;

                STACK_WIND_COOKIE (frame, afr_access_cbk,
                                   (void *) (long) read_child,
                                   children[next_call_child],
                                   children[next_call_child]->fops->access,
                                   &local->loc, local->cont.access.mask);
        }

out:
        if (unwind) {
                AFR_STACK_UNWIND (access, frame, op_ret, op_errno);
        }

        return 0;
}


int32_t
afr_access (call_frame_t *frame, xlator_t *this, loc_t *loc, int32_t mask)
{
        afr_private_t   *priv      = NULL;
        xlator_t        **children = NULL;
        int             call_child = 0;
        afr_local_t     *local     = NULL;
        int32_t         op_errno   = 0;
        int32_t         read_child = -1;
        int             ret        = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv     = this->private;
        VALIDATE_OR_GOTO (priv->children, out);

        children = priv->children;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->fresh_children = afr_children_create (priv->child_count);
        if (!local->fresh_children) {
                op_errno = ENOMEM;
                goto out;
        }


        read_child = afr_inode_get_read_ctx (this, loc->inode,
                                             local->fresh_children);
        ret = afr_get_call_child (this, local->child_up, read_child,
                                     local->fresh_children,
                                     &call_child,
                                     &local->cont.access.last_index);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        loc_copy (&local->loc, loc);
        local->cont.access.mask = mask;

        STACK_WIND_COOKIE (frame, afr_access_cbk,
                           (void *) (long) call_child,
                           children[call_child],
                           children[call_child]->fops->access,
                           loc, mask);

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (access, frame, -1, op_errno);
        return 0;
}


/* }}} */

/* {{{ stat */

int32_t
afr_stat_cbk (call_frame_t *frame, void *cookie,
              xlator_t *this, int32_t op_ret, int32_t op_errno,
              struct iatt *buf)
{
        afr_private_t * priv            = NULL;
        afr_local_t *   local           = NULL;
        xlator_t **     children        = NULL;
        int             unwind          = 1;
        int32_t         *last_index     = NULL;
        int32_t         next_call_child = -1;
        int32_t         read_child      = -1;
        int32_t         *fresh_children  = NULL;

        priv     = this->private;
        children = priv->children;

        read_child = (long) cookie;

        local = frame->local;

        if (op_ret == -1) {
                last_index = &local->cont.stat.last_index;
                fresh_children = local->fresh_children;
                next_call_child = afr_next_call_child (fresh_children,
                                                       local->child_up,
                                                       priv->child_count,
                                                       last_index, read_child);
                if (next_call_child < 0)
                        goto out;

                unwind = 0;

                STACK_WIND_COOKIE (frame, afr_stat_cbk,
                                   (void *) (long) read_child,
                                   children[next_call_child],
                                   children[next_call_child]->fops->stat,
                                   &local->loc);
        }

out:
        if (unwind) {
                AFR_STACK_UNWIND (stat, frame, op_ret, op_errno, buf);
        }

        return 0;
}


int32_t
afr_stat (call_frame_t *frame, xlator_t *this, loc_t *loc)
{
        afr_private_t   *priv      = NULL;
        afr_local_t     *local     = NULL;
        xlator_t        **children = NULL;
        int             call_child = 0;
        int32_t         op_errno   = 0;
        int32_t         read_child = -1;
        int             ret        = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv     = this->private;
        VALIDATE_OR_GOTO (priv->children, out);

        children = priv->children;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->fresh_children = afr_children_create (priv->child_count);
        if (!local->fresh_children) {
                op_errno = ENOMEM;
                goto out;
        }

        read_child = afr_inode_get_read_ctx (this, loc->inode,
                                             local->fresh_children);
        ret = afr_get_call_child (this, local->child_up, read_child,
                                     local->fresh_children,
                                     &call_child,
                                     &local->cont.stat.last_index);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }
        loc_copy (&local->loc, loc);

        STACK_WIND_COOKIE (frame, afr_stat_cbk, (void *) (long) call_child,
                           children[call_child],
                           children[call_child]->fops->stat,
                           loc);

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (stat, frame, -1, op_errno, NULL);

        return 0;
}


/* }}} */

/* {{{ fstat */

int32_t
afr_fstat_cbk (call_frame_t *frame, void *cookie, xlator_t *this,
               int32_t op_ret, int32_t op_errno, struct iatt *buf)
{
        afr_private_t   *priv           = NULL;
        afr_local_t     *local          = NULL;
        xlator_t        **children      = NULL;
        int             unwind          = 1;
        int32_t         *last_index     = NULL;
        int32_t         next_call_child = -1;
        int32_t         read_child      = -1;
        int32_t         *fresh_children  = NULL;

        priv     = this->private;
        children = priv->children;

        local = frame->local;

        read_child = (long) cookie;

        if (op_ret == -1) {
                last_index = &local->cont.fstat.last_index;
                fresh_children = local->fresh_children;
                next_call_child = afr_next_call_child (fresh_children,
                                                       local->child_up,
                                                       priv->child_count,
                                                       last_index, read_child);
                if (next_call_child < 0)
                        goto out;

                unwind = 0;

                STACK_WIND_COOKIE (frame, afr_fstat_cbk,
                                   (void *) (long) read_child,
                                   children[next_call_child],
                                   children[next_call_child]->fops->fstat,
                                   local->fd);
        }

out:
        if (unwind) {
                AFR_STACK_UNWIND (fstat, frame, op_ret, op_errno, buf);
        }

        return 0;
}


int32_t
afr_fstat (call_frame_t *frame, xlator_t *this,
           fd_t *fd)
{
        afr_private_t   *priv      = NULL;
        afr_local_t     *local     = NULL;
        xlator_t        **children = NULL;
        int             call_child = 0;
        int32_t         op_errno   = 0;
        int32_t         read_child = 0;
        int             ret        = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (fd, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv     = this->private;
        VALIDATE_OR_GOTO (priv->children, out);

        children = priv->children;

        VALIDATE_OR_GOTO (fd->inode, out);

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->fresh_children = afr_children_create (priv->child_count);
        if (!local->fresh_children) {
                op_errno = ENOMEM;
                goto out;
        }

        read_child = afr_inode_get_read_ctx (this, fd->inode,
                                             local->fresh_children);



        ret = afr_get_call_child (this, local->child_up, read_child,
                                     local->fresh_children,
                                     &call_child,
                                     &local->cont.fstat.last_index);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        local->fd = fd_ref (fd);

        ret = afr_open_fd_fix (frame, this, _gf_false);
        if (ret) {
                op_errno = -ret;
                goto out;
        }
        STACK_WIND_COOKIE (frame, afr_fstat_cbk, (void *) (long) call_child,
                           children[call_child],
                           children[call_child]->fops->fstat,
                           fd);

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (fstat, frame, -1, op_errno, NULL);

        return 0;
}

/* }}} */

/* {{{ readlink */

int32_t
afr_readlink_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno,
                  const char *buf, struct iatt *sbuf)
{
        afr_private_t * priv                  = NULL;
        afr_local_t *   local                 = NULL;
        xlator_t **     children              = NULL;
        int             unwind                = 1;
        int32_t         *last_index           = NULL;
        int32_t         next_call_child       = -1;
        int32_t         read_child            = -1;
        int32_t         *fresh_children        = NULL;

        priv     = this->private;
        children = priv->children;

        local = frame->local;

        read_child = (long) cookie;

        if (op_ret == -1) {
                last_index = &local->cont.readlink.last_index;
                fresh_children = local->fresh_children;
                next_call_child = afr_next_call_child (fresh_children,
                                                       local->child_up,
                                                       priv->child_count,
                                                       last_index, read_child);
                if (next_call_child < 0)
                        goto out;

                unwind = 0;
                STACK_WIND_COOKIE (frame, afr_readlink_cbk,
                                   (void *) (long) read_child,
                                   children[next_call_child],
                                   children[next_call_child]->fops->readlink,
                                   &local->loc,
                                   local->cont.readlink.size);
        }

out:
        if (unwind) {
                AFR_STACK_UNWIND (readlink, frame, op_ret, op_errno, buf, sbuf);
        }

        return 0;
}


int32_t
afr_readlink (call_frame_t *frame, xlator_t *this,
              loc_t *loc, size_t size)
{
        afr_private_t   *priv      = NULL;
        xlator_t        **children = NULL;
        int             call_child = 0;
        afr_local_t     *local     = NULL;
        int32_t         op_errno   = 0;
        int32_t         read_child = -1;
        int             ret        = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv     = this->private;
        VALIDATE_OR_GOTO (priv->children, out);

        children = priv->children;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->fresh_children = afr_children_create (priv->child_count);
        if (!local->fresh_children) {
                op_errno = ENOMEM;
                goto out;
        }
        read_child = afr_inode_get_read_ctx (this, loc->inode,
                                             local->fresh_children);
        ret = afr_get_call_child (this, local->child_up, read_child,
                                     local->fresh_children,
                                     &call_child,
                                     &local->cont.readlink.last_index);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        loc_copy (&local->loc, loc);

        local->cont.readlink.size       = size;

        STACK_WIND_COOKIE (frame, afr_readlink_cbk,
                           (void *) (long) call_child,
                           children[call_child],
                           children[call_child]->fops->readlink,
                           loc, size);

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (readlink, frame, -1, op_errno, NULL, NULL);
        return 0;
}


/* }}} */

/* {{{ getxattr */

struct _xattr_key {
        char *key;
        struct list_head list;
};


void
__gather_xattr_keys (dict_t *dict, char *key, data_t *value,
                     void *data)
{
        struct list_head *  list  = data;
        struct _xattr_key * xkey  = NULL;

        if (!strncmp (key, AFR_XATTR_PREFIX,
                      strlen (AFR_XATTR_PREFIX))) {

                xkey = GF_CALLOC (1, sizeof (*xkey), gf_afr_mt_xattr_key);
                if (!xkey)
                        return;

                xkey->key = key;
                INIT_LIST_HEAD (&xkey->list);

                list_add_tail (&xkey->list, list);
        }
}


void
__filter_xattrs (dict_t *dict)
{
        struct list_head   keys = {0,};
        struct _xattr_key *key  = NULL;
        struct _xattr_key *tmp  = NULL;

        INIT_LIST_HEAD (&keys);

        dict_foreach (dict, __gather_xattr_keys,
                      (void *) &keys);

        list_for_each_entry_safe (key, tmp, &keys, list) {
                dict_del (dict, key->key);

                list_del_init (&key->list);

                GF_FREE (key);
        }
}



int32_t
afr_getxattr_cbk (call_frame_t *frame, void *cookie,
                  xlator_t *this, int32_t op_ret, int32_t op_errno,
                  dict_t *dict)
{
        afr_private_t * priv            = NULL;
        afr_local_t *   local           = NULL;
        xlator_t **     children        = NULL;
        int             unwind          = 1;
        int32_t         *last_index     = NULL;
        int32_t         next_call_child = -1;
        int32_t         read_child      = -1;
        int32_t         *fresh_children  = NULL;

        priv     = this->private;
        children = priv->children;

        local = frame->local;

        read_child = (long) cookie;

        if (op_ret == -1) {
                last_index = &local->cont.getxattr.last_index;
                fresh_children = local->fresh_children;
                next_call_child = afr_next_call_child (fresh_children,
                                                       local->child_up,
                                                       priv->child_count,
                                                       last_index, read_child);
                if (next_call_child < 0)
                        goto out;

                unwind = 0;
                STACK_WIND_COOKIE (frame, afr_getxattr_cbk,
                                   (void *) (long) read_child,
                                   children[next_call_child],
                                   children[next_call_child]->fops->getxattr,
                                   &local->loc,
                                   local->cont.getxattr.name);
        }

out:
        if (unwind) {
                if (op_ret >= 0 && dict)
                        __filter_xattrs (dict);

                AFR_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict);
        }

        return 0;
}

int32_t
afr_getxattr_unwind (call_frame_t *frame,
                     int op_ret, int op_errno, dict_t *dict)

{
        AFR_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict);
        return 0;
}

int32_t
afr_getxattr_clrlk_cbk (call_frame_t *frame, void *cookie,
                        xlator_t *this, int32_t op_ret, int32_t op_errno,
                        dict_t *dict)
{
        afr_local_t     *local                  = NULL;
        afr_private_t   *priv                   = NULL;
        xlator_t        **children              = NULL;
        dict_t          *xattr                  = NULL;
        char            *tmp_report             = NULL;
        char            lk_summary[1024]        = {0,};
        int             serz_len                = 0;
        int32_t         callcnt                 = 0;
        long int        cky                     = 0;
        int             ret                     = 0;

        priv     = this->private;
        children = priv->children;

        local = frame->local;
        cky = (long) cookie;

        LOCK (&frame->lock);
        {
                callcnt = --local->call_count;

                if (!local->dict)
                        local->dict = dict_new ();
                if (local->dict) {
                        ret = dict_get_str (dict, local->cont.getxattr.name,
                                            &tmp_report);
                        if (ret)
                                goto unlock;
                        ret = dict_set_str (local->dict,
                                            children[cky]->name,
                                            tmp_report);
                        if (ret)
                                goto unlock;
                }
                if (op_ret == -1)
                        local->child_errno[cky] = op_errno;
        }
unlock:
        UNLOCK (&frame->lock);

        if (!callcnt) {
                xattr = dict_new ();
                if (!xattr) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        goto unwind;
                }
                ret = dict_serialize_value_with_delim (local->dict,
                                                       lk_summary,
                                                       &serz_len, '\n');
                if (ret) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        gf_log (this->name, GF_LOG_ERROR,
                                "Error serializing dictionary");
                        goto unwind;
                }
                if (serz_len == -1)
                        snprintf (lk_summary, sizeof (lk_summary),
                                  "No locks cleared.");
                ret = dict_set_dynstr (xattr, local->cont.getxattr.name,
                                       gf_strdup (lk_summary));
                if (ret) {
                        op_ret = -1;
                        op_errno = ENOMEM;
                        gf_log (this->name, GF_LOG_ERROR,
                                "Error setting dictionary");
                        goto unwind;
                }

        unwind:
                // Updating child_errno with more recent 'events'
                local->child_errno[cky] = op_errno;
                op_errno = afr_resultant_errno_get (NULL, local->child_errno,
                                                    priv->child_count);
                AFR_STACK_UNWIND (getxattr, frame, op_ret, op_errno, xattr);

                if (local->dict)
                        dict_unref (local->dict);

                if (local->child_errno)
                        GF_FREE (local->child_errno);

                if (xattr)
                        dict_unref (xattr);
        }

        return ret;
}

/**
 * node-uuid cbk uses next child querying mechanism
 */
int32_t
afr_getxattr_node_uuid_cbk (call_frame_t *frame, void *cookie,
                            xlator_t *this, int32_t op_ret, int32_t op_errno,
                            dict_t *dict)
{
        afr_private_t  *priv            = NULL;
        afr_local_t    *local           = NULL;
        xlator_t      **children        = NULL;
        int             unwind          = 1;
        int             curr_call_child = 0;

        priv = this->private;
        children = priv->children;

        local = frame->local;

        if (op_ret == -1) { /** query the _next_ child */

                /**
                 * _current_ becomes _next_
                 * If done with all childs and yet no success; give up !
                 */
                curr_call_child = (int) ((long)cookie);
                if (++curr_call_child == priv->child_count)
                        goto unwind;

                gf_log (this->name, GF_LOG_WARNING,
                        "op_ret (-1): Re-querying afr-child (%d/%d)",
                        curr_call_child, priv->child_count);

                unwind = 0;
                STACK_WIND_COOKIE (frame, afr_getxattr_node_uuid_cbk,
                                   (void *) (long) curr_call_child,
                                   children[curr_call_child],
                                   children[curr_call_child]->fops->getxattr,
                                   &local->loc,
                                   local->cont.getxattr.name);
        }

 unwind:
        if (unwind)
                AFR_STACK_UNWIND (getxattr, frame, op_ret, op_errno, dict);

        return 0;
}

int32_t
afr_getxattr_pathinfo_cbk (call_frame_t *frame, void *cookie,
                           xlator_t *this, int32_t op_ret, int32_t op_errno,
                           dict_t *dict)
{
        afr_local_t *local          = NULL;
        int32_t      callcnt        = 0;
        int          ret            = 0;
        char        *xattr          = NULL;
        char        *xattr_serz     = NULL;
        char        xattr_cky[1024] = {0,};
        dict_t      *nxattr         = NULL;
        long         cky            = 0;
        int32_t      padding        = 0;
        int32_t      tlen           = 0;

        if (!frame || !frame->local || !this) {
                gf_log ("", GF_LOG_ERROR, "possible NULL deref");
                goto out;
        }

        local = frame->local;
        cky = (long) cookie;

        LOCK (&frame->lock);
                {
                        callcnt = --local->call_count;

                        if (!dict || (op_ret < 0))
                                goto out;

                        if (!local->dict)
                                local->dict = dict_new ();

                        if (local->dict) {
                                ret = dict_get_str (dict,
                                                    local->cont.getxattr.name,
                                                    &xattr);
                                if (ret)
                                        goto out;

                                xattr = gf_strdup (xattr);

                                (void)snprintf (xattr_cky, 1024, "%s-%ld",
                                                local->cont.getxattr.name, cky);
                                ret = dict_set_dynstr (local->dict,
                                                       xattr_cky, xattr);
                                if (ret) {
                                        gf_log (this->name, GF_LOG_ERROR,
                                                "Cannot set xattr cookie key");
                                        goto out;
                                }

                                local->cont.getxattr.xattr_len += strlen (xattr) + 1;
                        }
                }
 out:
        UNLOCK (&frame->lock);

        if (!callcnt) {
                if (!local->cont.getxattr.xattr_len)
                        goto unwind;

                nxattr = dict_new ();
                if (!nxattr)
                        goto unwind;

                /* extra bytes for decorations (brackets and <>'s) */
                padding += strlen (this->name) + strlen (AFR_PATHINFO_HEADER) + 4;
                local->cont.getxattr.xattr_len += (padding + 2);

                xattr_serz = GF_CALLOC (local->cont.getxattr.xattr_len,
                                        sizeof (char), gf_common_mt_char);

                if (!xattr_serz)
                        goto unwind;

                /* the xlator info */
                (void) sprintf (xattr_serz, "(<"AFR_PATHINFO_HEADER"%s> ",
                                this->name);

                /* actual series of pathinfo */
                ret = dict_serialize_value_with_delim (local->dict,
                                                       xattr_serz + strlen (xattr_serz),
                                                       &tlen, ' ');
                if (ret) {
                        gf_log (this->name, GF_LOG_ERROR, "Error serializing"
                                " dictionary");
                        goto unwind;
                }

                /* closing part */
                *(xattr_serz + padding + tlen) = ')';
                *(xattr_serz + padding + tlen + 1) = '\0';

                ret = dict_set_dynstr (nxattr, local->cont.getxattr.name,
                                       xattr_serz);
                if (ret)
                        gf_log (this->name, GF_LOG_ERROR, "Cannot set pathinfo"
                                " key in dict");

        unwind:
                AFR_STACK_UNWIND (getxattr, frame, op_ret, op_errno, nxattr);

                if (local->dict)
                        dict_unref (local->dict);

                if (nxattr)
                        dict_unref (nxattr);
        }

        return ret;
}

static gf_boolean_t
afr_is_special_xattr (const char *name, fop_getxattr_cbk_t *cbk)
{
        gf_boolean_t    is_spl = _gf_true;

        GF_ASSERT (cbk);
        if (!cbk) {
                is_spl = _gf_false;
                goto out;
        }

        if (!strcmp (name, GF_XATTR_PATHINFO_KEY))
                *cbk = afr_getxattr_pathinfo_cbk;

        else if (!strcmp (name, GF_XATTR_CLRLK_CMD))
                *cbk = afr_getxattr_clrlk_cbk;
        else
                is_spl = _gf_false;

out:
        return is_spl;
}

static void
afr_getxattr_frm_all_children (xlator_t *this, call_frame_t *frame,
                               const char *name, loc_t *loc,
                               fop_getxattr_cbk_t cbk)
{
        afr_private_t   *priv           = NULL;
        afr_local_t     *local          = NULL;
        xlator_t        **children      = NULL;
        int             i               = 0;

        priv     = this->private;
        children = priv->children;

        local = frame->local;
        local->call_count = priv->child_count;

        for (i = 0; i < priv->child_count; i++) {
                STACK_WIND_COOKIE (frame, cbk,
                                   (void *) (long) i,
                                   children[i], children[i]->fops->getxattr,
                                   loc, name);
        }
        return;
}

int32_t
afr_getxattr (call_frame_t *frame, xlator_t *this,
              loc_t *loc, const char *name)
{
        afr_private_t           *priv         = NULL;
        xlator_t                **children    = NULL;
        int                     call_child    = 0;
        afr_local_t             *local        = NULL;
        xlator_list_t           *trav         = NULL;
        xlator_t                **sub_volumes = NULL;
        int                     i             = 0;
        int32_t                 op_errno      = 0;
        int32_t                 read_child    = -1;
        int                     ret           = -1;
        fop_getxattr_cbk_t      cbk           = NULL;


        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv     = this->private;
        VALIDATE_OR_GOTO (priv->children, out);

        children = priv->children;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        loc_copy (&local->loc, loc);
        if (name)
                local->cont.getxattr.name = gf_strdup (name);


        if (name) {
                if (!strncmp (name, AFR_XATTR_PREFIX,
                              strlen (AFR_XATTR_PREFIX))) {
                        gf_log (this->name, GF_LOG_INFO,
                                "%s: no data present for key %s",
                                loc->path, name);
                        op_errno = ENODATA;
                        goto out;
                }
                if ((strcmp (GF_XATTR_MARKER_KEY, name) == 0)
                    && (-1 == frame->root->pid)) {

                        local->marker.call_count = priv->child_count;

                        sub_volumes = alloca ( priv->child_count * sizeof (xlator_t *));
                        for (i = 0, trav = this->children; trav ;
                             trav = trav->next, i++) {

                                *(sub_volumes + i)  = trav->xlator;
                        }

                        if (cluster_getmarkerattr (frame, this, loc, name,
                                                   local, afr_getxattr_unwind,
                                                   sub_volumes,
                                                   priv->child_count,
                                                   MARKER_UUID_TYPE,
                                                   priv->vol_uuid)) {

                                gf_log (this->name, GF_LOG_INFO,
                                        "%s: failed to get marker attr (%s)",
                                        loc->path, name);
                                op_errno = EINVAL;
                                goto out;
                        }

                        return 0;
                }

                /*
                 * if we are doing getxattr with pathinfo as the key then we
                 * collect information from all childs
                 */
                if (afr_is_special_xattr (name, &cbk)) {
                        afr_getxattr_frm_all_children (this, frame, name,
                                                       loc, cbk);
                        return 0;
                }

                if (XATTR_IS_NODE_UUID (name)) {
                        i = 0;
                        STACK_WIND_COOKIE (frame, afr_getxattr_node_uuid_cbk,
                                           (void *) (long) i,
                                           children[i],
                                           children[i]->fops->getxattr,
                                           loc, name);
                        return 0;
                }

                if (*priv->vol_uuid) {
                        if ((match_uuid_local (name, priv->vol_uuid) == 0)
                            && (-1 == frame->root->pid)) {

                                local->marker.call_count = priv->child_count;

                                sub_volumes = alloca ( priv->child_count * sizeof (xlator_t *));
                                for (i = 0, trav = this->children; trav ;
                                     trav = trav->next, i++) {

                                        *(sub_volumes + i)  = trav->xlator;

                                }

                                if (cluster_getmarkerattr (frame, this, loc,
                                                           name, local,
                                                           afr_getxattr_unwind,
                                                           sub_volumes,
                                                           priv->child_count,
                                                           MARKER_XTIME_TYPE,
                                                           priv->vol_uuid)) {
                                        gf_log (this->name, GF_LOG_INFO,
                                                "%s: failed to get marker attr (%s)",
                                                loc->path, name);
                                        op_errno = EINVAL;
                                        goto out;
                                }

                                return 0;
                        }
                }
        }

        local->fresh_children = afr_children_create (priv->child_count);
        if (!local->fresh_children) {
                op_errno = ENOMEM;
                goto out;
        }

        read_child = afr_inode_get_read_ctx (this, loc->inode, local->fresh_children);
        ret = afr_get_call_child (this, local->child_up, read_child,
                                     local->fresh_children,
                                     &call_child,
                                     &local->cont.getxattr.last_index);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        STACK_WIND_COOKIE (frame, afr_getxattr_cbk,
                           (void *) (long) call_child,
                           children[call_child],
                           children[call_child]->fops->getxattr,
                           loc, name);

        ret = 0;
out:
        if (ret < 0)
                AFR_STACK_UNWIND (getxattr, frame, -1, op_errno, NULL);
        return 0;
}

/* {{{ fgetxattr */


int32_t
afr_fgetxattr_cbk (call_frame_t *frame, void *cookie,
                   xlator_t *this, int32_t op_ret, int32_t op_errno,
                   dict_t *dict)
{
        afr_private_t * priv            = NULL;
        afr_local_t *   local           = NULL;
        xlator_t **     children        = NULL;
        int             unwind          = 1;
        int32_t         *last_index     = NULL;
        int32_t         next_call_child = -1;
        int32_t         read_child      = -1;
        int32_t         *fresh_children  = NULL;

        priv     = this->private;
        children = priv->children;

        local = frame->local;

        read_child = (long) cookie;

        if (op_ret == -1) {
                last_index = &local->cont.getxattr.last_index;
                fresh_children = local->fresh_children;
                next_call_child = afr_next_call_child (fresh_children,
                                                       local->child_up,
                                                       priv->child_count,
                                                       last_index, read_child);
                if (next_call_child < 0)
                        goto out;

                unwind = 0;
                STACK_WIND_COOKIE (frame, afr_fgetxattr_cbk,
                                   (void *) (long) read_child,
                                   children[next_call_child],
                                   children[next_call_child]->fops->fgetxattr,
                                   local->fd,
                                   local->cont.getxattr.name);
        }

out:
        if (unwind) {
                if (op_ret >= 0 && dict)
                        __filter_xattrs (dict);

                AFR_STACK_UNWIND (fgetxattr, frame, op_ret, op_errno, dict);
        }

        return 0;
}

int32_t
afr_fgetxattr_unwind (call_frame_t *frame,
                      int op_ret, int op_errno, dict_t *dict)

{
        AFR_STACK_UNWIND (fgetxattr, frame, op_ret, op_errno, dict);
        return 0;
}

int32_t
afr_fgetxattr (call_frame_t *frame, xlator_t *this,
               fd_t *fd, const char *name)
{
        afr_private_t   *priv         = NULL;
        xlator_t        **children    = NULL;
        int             call_child    = 0;
        afr_local_t     *local        = NULL;
        int32_t         op_ret        = -1;
        int32_t         op_errno      = 0;
        int32_t         read_child    = -1;


        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv     = this->private;
        VALIDATE_OR_GOTO (priv->children, out);

        children = priv->children;

        AFR_LOCAL_ALLOC_OR_GOTO (local, out);
        frame->local = local;

        op_ret = afr_local_init (local, priv, &op_errno);
        if (op_ret < 0) {
                op_errno = -op_ret;
                goto out;
        }

        local->fd = fd_ref (fd);
        if (name)
                local->cont.getxattr.name = gf_strdup (name);

        /* pathinfo gets handled only in getxattr() */

        local->fresh_children = afr_children_create (priv->child_count);
        if (!local->fresh_children) {
                op_errno = ENOMEM;
                goto out;
        }

        read_child = afr_inode_get_read_ctx (this, fd->inode, local->fresh_children);
        op_ret = afr_get_call_child (this, local->child_up, read_child,
                                     local->fresh_children,
                                     &call_child,
                                     &local->cont.getxattr.last_index);
        if (op_ret < 0) {
                op_errno = -op_ret;
                op_ret = -1;
                goto out;
        }

        STACK_WIND_COOKIE (frame, afr_fgetxattr_cbk,
                           (void *) (long) call_child,
                           children[call_child],
                           children[call_child]->fops->fgetxattr,
                           fd, name);

        op_ret = 0;
out:
        if (op_ret == -1) {
                AFR_STACK_UNWIND (fgetxattr, frame, op_ret, op_errno, NULL);
        }
        return 0;
}


/* }}} */

/* {{{ readv */

/**
 * read algorithm:
 *
 * if the user has specified a read subvolume, use it
 * otherwise -
 *   use the inode number to hash it to one of the subvolumes, and
 *   read from there (to balance read load)
 *
 * if any of the above read's fail, try the children in sequence
 * beginning at the beginning
 */

int32_t
afr_readv_cbk (call_frame_t *frame, void *cookie,
               xlator_t *this, int32_t op_ret, int32_t op_errno,
               struct iovec *vector, int32_t count, struct iatt *buf,
               struct iobref *iobref)
{
        afr_private_t * priv            = NULL;
        afr_local_t *   local           = NULL;
        xlator_t **     children        = NULL;
        int             unwind          = 1;
        int32_t         *last_index     = NULL;
        int32_t         next_call_child = -1;
        int32_t         *fresh_children  = NULL;
        int32_t         read_child      = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);

        priv     = this->private;
        VALIDATE_OR_GOTO (priv->children, out);

        children = priv->children;

        local = frame->local;

        read_child = (long) cookie;

        if (op_ret == -1) {
                last_index = &local->cont.readv.last_index;
                fresh_children = local->fresh_children;
                next_call_child = afr_next_call_child (fresh_children,
                                                       local->child_up,
                                                       priv->child_count,
                                                       last_index, read_child);
                if (next_call_child < 0)
                        goto out;

                unwind = 0;

                STACK_WIND_COOKIE (frame, afr_readv_cbk,
                                   (void *) (long) read_child,
                                   children[next_call_child],
                                   children[next_call_child]->fops->readv,
                                   local->fd, local->cont.readv.size,
                                   local->cont.readv.offset,
                                   local->cont.readv.flags);
        }

out:
        if (unwind) {
                AFR_STACK_UNWIND (readv, frame, op_ret, op_errno,
                                  vector, count, buf, iobref);
        }

        return 0;
}


int32_t
afr_readv (call_frame_t *frame, xlator_t *this,
           fd_t *fd, size_t size, off_t offset, uint32_t flags)
{
        afr_private_t * priv       = NULL;
        afr_local_t   * local      = NULL;
        xlator_t **     children   = NULL;
        int             call_child = 0;
        int32_t         op_errno   = 0;
        int32_t         read_child = -1;
        int             ret        = -1;

        VALIDATE_OR_GOTO (frame, out);
        VALIDATE_OR_GOTO (this, out);
        VALIDATE_OR_GOTO (this->private, out);
        VALIDATE_OR_GOTO (fd, out);

        priv     = this->private;
        children = priv->children;

        AFR_LOCAL_ALLOC_OR_GOTO (frame->local, out);
        local = frame->local;

        ret = afr_local_init (local, priv, &op_errno);
        if (ret < 0)
                goto out;

        local->fresh_children = afr_children_create (priv->child_count);
        if (!local->fresh_children) {
                op_errno = ENOMEM;
                goto out;
        }

        read_child = afr_inode_get_read_ctx (this, fd->inode, local->fresh_children);
        ret = afr_get_call_child (this, local->child_up, read_child,
                                     local->fresh_children,
                                     &call_child,
                                     &local->cont.readv.last_index);
        if (ret < 0) {
                op_errno = -ret;
                goto out;
        }

        local->fd                    = fd_ref (fd);

        local->cont.readv.size       = size;
        local->cont.readv.offset     = offset;
        local->cont.readv.flags      = flags;

        ret = afr_open_fd_fix (frame, this, _gf_false);
        if (ret) {
                op_errno = -ret;
                goto out;
        }
        STACK_WIND_COOKIE (frame, afr_readv_cbk,
                           (void *) (long) call_child,
                           children[call_child],
                           children[call_child]->fops->readv,
                           fd, size, offset, flags);

        ret = 0;
out:
        if (ret < 0) {
                AFR_STACK_UNWIND (readv, frame, -1, op_errno, NULL, 0, NULL,
                                  NULL);
        }
        return 0;
}

/* }}} */
