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

struct p2s_node *abrt_problems2_session_new_node(char *path, char *caller, uid_t uid, guint regid);
const char *abrt_problems2_session_node_path(struct p2s_node *session);
guint abrt_problems2_session_node_registration_id(struct p2s_node *session);
struct p2s_node *abrt_problems2_session_find_node(const char *caller);
int abrt_problems2_session_is_authorized(struct p2s_node *session);

GDBusInterfaceVTable *abrt_problems2_session_node_vtable(void);

#endif/*ABRT_PROBLEMS2_SESSION_NODE*/
