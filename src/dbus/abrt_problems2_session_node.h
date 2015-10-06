/*
  Copyright (C) 2015  ABRT team

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License along
  with this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#ifndef ABRT_PROBLEMS2_SESSION_NODE
#define ABRT_PROBLEMS2_SESSION_NODE

#include <gio/gio.h>

struct p2s_node;

struct p2s_node *abrt_problems2_session_node_new(char *caller, uid_t uid);
void abrt_problems2_session_node_free(struct p2s_node *session);

uid_t abrt_problems2_session_uid(struct p2s_node *session);
int abrt_problems2_session_is_authorized(struct p2s_node *session);
int abrt_problems2_session_check_sanity(struct p2s_node *session, const char *caller, uid_t caller_uid, GError **error);

GDBusInterfaceVTable *abrt_problems2_session_node_vtable(void);

#endif/*ABRT_PROBLEMS2_SESSION_NODE*/
