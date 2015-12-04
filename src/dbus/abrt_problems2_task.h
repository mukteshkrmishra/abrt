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
#ifndef ABRT_P2_TASK_H
#define ABRT_P2_TASK_H

#include "libabrt.h"

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define TYPE_ABRT_P2_TASK abrt_p2_task_get_type ()
G_DECLARE_FINAL_TYPE(AbrtP2Task, abrt_p2_task, ABRT_P2, TASK, GObject)

AbrtP2Task *abrt_p2_task_new(void);

gint32 abrt_p2_task_status(AbrtP2Task *task);

gint32 abrt_p2_task_details(AbrtP2Task *task);

void abrt_p2_task_start(AbrtP2Task *start, GError **error);

void abrt_p2_task_cancel(AbrtP2Task *start, GError **error);

void abrt_p2_task_finish(AbrtP2Task *start, GVariant **result, gint32 *code,
            GError **error);

G_END_DECLS

#endif/*ABRT_P2_TASK_H*/
