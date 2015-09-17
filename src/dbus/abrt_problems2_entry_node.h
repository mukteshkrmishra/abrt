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
#ifndef ABRT_PROBLEMS2_ENTRY_NODE_H
#define ABRT_PROBLEMS2_ENTRY_NODE_H

#include "libabrt.h"

#include <gio/gio.h>

struct p2e_node;

struct p2e_node *abrt_problems2_entry_node_new(char *dirname);
int  abrt_problems2_entry_node_remove(struct p2e_node *entry, uid_t caller_uid, GError **error);
void abrt_problems2_entry_node_free(struct p2e_node *entry);
int abrt_problems2_entry_node_accessible_by_uid(struct p2e_node *entry, uid_t uid, struct dump_dir **dd);

GDBusInterfaceVTable *abrt_problems2_entry_node_vtable(void);

#endif/*ABRT_PROBLEMS2_ENTRY_NODE_H*/
