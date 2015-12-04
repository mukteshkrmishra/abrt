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

#include "abrt_problems2_task.h"

typedef struct
{
    gint32 p2t_status;
    GVariant *p2t_details;
    GVariant *p2t_results;
    GCancellable *p2t_cancellable;
} AbrtP2TaskPrivate;

struct _AbrtP2Task
{
    GObject parent_instance;
    AbrtP2TaskPrivate *pv;
};

G_DEFINE_TYPE_WITH_PRIVATE(AbrtP2Task, abrt_p2_task, G_TYPE_OBJECT)

static void abrt_p2_task_finalize(GObject *gobject)
{
    AbrtP2TaskPrivate *pv = abrt_p2_task_get_instance_private(ABRT_P2_TASK(gobject));
    g_variant_unref(pv->p2t_details);
    g_variant_unref(pv->p2t_results);
}

static void abrt_p2_task_class_init(AbrtP2TaskClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = abrt_p2_task_finalize;
}

static void abrt_p2_task_init(AbrtP2Task *self)
{
    self->pv = abrt_p2_task_get_instance_private(self);
}

AbrtP2Task *abrt_p2_task_new(void)
{
    AbrtP2Task *task = g_object_new(TYPE_ABRT_P2_TASK, NULL);

    return task;
}

gint32 abrt_p2_task_status(AbrtP2Task *task)
{
    return task->pv->p2t_status;
}

gint32 abrt_p2_task_details(AbrtP2Task *task)
{
    return task->pv->p2t_details;
}

void abrt_p2_task_start(AbrtP2Task *start, GError **error)
{
}

void abrt_p2_task_cancel(AbrtP2Task *start, GError **error)
{
}

void abrt_p2_task_finish(AbrtP2Task *start, GVariant **result, gint32 *code,
            GError **error)
{
}
