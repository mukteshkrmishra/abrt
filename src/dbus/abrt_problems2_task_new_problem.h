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
#ifndef ABRT_P2_TASK_NEW_PROBLEM_H
#define ABRT_P2_TASK_NEW_PROBLEM_H

#include "abrt_problems2_task.h"
#include "abrt_problems2_service.h"

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define TYPE_ABRT_P2_TASK_NEW_PROBLEM abrt_p2_task_new_problem_get_type ()
G_DECLARE_FINAL_TYPE(AbrtP2TaskNewProblem, abrt_p2_task_new_problem, ABRT_P2_TASK, NEW_PROBLEM, AbrtP2Task)

typedef enum {
    ABRT_P2_TASK_NEW_PROBLEM_ACCEPTED,
    ABRT_P2_TASK_NEW_PROBLEM_FAILED,
    ABRT_P2_TASK_NEW_PROBLEM_DUPLICATE,
    ABRT_P2_TASK_NEW_PROBLEM_DROPPED,
    ABRT_P2_TASK_NEW_PROBLEM_INVALID_DATA,
} AbrtP2TaskNewProblemCodes;

AbrtP2TaskNewProblem *abrt_p2_task_new_problem_new(AbrtP2Service *service,
            GVariant *problem_info, uid_t caller_uid, GUnixFDList *fd_list);

void abrt_p2_task_new_problem_autonomous_run(AbrtP2TaskNewProblem *task);

void abrt_p2_task_new_problem_wait_before_notify(AbrtP2TaskNewProblem *task,
            bool value);

G_END_DECLS

#endif/*ABRT_P2_TASK_NEW_PROBLEM_H*/
