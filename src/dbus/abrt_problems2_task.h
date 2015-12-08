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

GType abrt_p2_task_get_type (void);

#define ABRT_TYPE_P2_TASK (abrt_p2_task_get_type ())
#define ABRT_P2_TASK(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), ABRT_TYPE_P2_TASK, AbrtP2Task))
#define ABRT_P2_TASK_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST ((klass), ABRT_TYPE_P2_TASK, AbrtP2TaskClass))
#define ABRT_IS_P2_TASK(obj)(G_TYPE_CHECK_INSTANCE_TYPE ((obj), ABRT_TYPE_P2_TASK))
#define ABRT_IS_P2_TASK_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), ABRT_TYPE_P2_TASK))
#define ABRT_P2_TASK_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), ABRT_TYPE_P2_TASK, AbrtP2TaskClass))

typedef struct _AbrtP2Task AbrtP2Task;
typedef struct _AbrtP2TaskClass AbrtP2TaskClass;

static inline void glib_autoptr_cleanup_AbrtP2Task(AbrtP2Task **task)
{
    glib_autoptr_cleanup_GObject((GObject **)task);
}

typedef enum {
    ABRT_P2_TASK_STATUS_NEW,
    ABRT_P2_TASK_STATUS_RUNNING,
    ABRT_P2_TASK_STATUS_STOPPED,
    ABRT_P2_TASK_STATUS_CANCELED,
    ABRT_P2_TASK_STATUS_FAILED,
    ABRT_P2_TASK_STATUS_DONE,
} AbrtP2TaskStatus;

typedef enum {
    ABRT_P2_TASK_CODE_DONE,
    ABRT_P2_TASK_CODE_STOP,
    ABRT_P2_TASK_CODE_ERROR,
} AbrtP2TaskCode;

struct _AbrtP2TaskClass
{
    GObjectClass parent_class;

    /* Abstract methods */
    AbrtP2TaskCode (* run)(AbrtP2Task *task, GError **error);

    /* Virtual methods */
    void (* start)(AbrtP2Task *task, GError **error);

    void (* cancel)(AbrtP2Task *task, GError **error);

    void (* finish)(AbrtP2Task *task, GError **error);

    /* Signals */
   void (*status_changed)(AbrtP2Task *task, gint32 status);
};

AbrtP2TaskStatus abrt_p2_task_status(AbrtP2Task *task);

GVariant *abrt_p2_task_details(AbrtP2Task *task);

void abrt_p2_task_add_detail(AbrtP2Task *task, const char *key, GVariant *value);

void abrt_p2_task_set_response(AbrtP2Task *task, GVariant *response);

void abrt_p2_task_start(AbrtP2Task *start, GError **error);

void abrt_p2_task_cancel(AbrtP2Task *start, GError **error);

void abrt_p2_task_finish(AbrtP2Task *start, GVariant **result, gint32 *code,
            GError **error);

void abrt_p2_task_autonomous_run(AbrtP2Task *task, GError **error);

G_END_DECLS

#endif/*ABRT_P2_TASK_H*/
