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
#ifndef ABRT_PROBLEMS2_NODE_H
#define ABRT_PROBLEMS2_NODE_H

G_BEGIN_DECLS

#include <glib-object.h>
#include <gio/gio.h>

#define TYPE_ABRT_P2 abrt_p2_get_type ()
G_DECLARE_FINAL_TYPE(AbrtP2, abrt_p2, ABRT, P2, GObject)

GDBusInterfaceVTable *abrt_p2_vtable(void);

G_END_DECLS

#endif/*ABRT_PROBLEMS2_NODE_H*/
