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
#ifndef ABRT_PROBLEMS2_SERVICE_H
#define ABRT_PROBLEMS2_SERVICE_H

#include <libreport/problem_data.h>
#include <polkit/polkit.h>

#define ABRT_P2_BUS "org.freedesktop.problems"
#define ABRT_P2_PATH "/org/freedesktop/Problems2"
#define ABRT_P2_NS "org.freedesktop.Problems2"
#define ABRT_P2_NS_MEMBER(name) ABRT_P2_NS"."name

/*
 * D-Bus object representation
 */
struct abrt_problems2_object;

void *abrt_problems2_object_get_node(struct abrt_problems2_object *object);

void abrt_problems2_object_destroy(struct abrt_problems2_object *object,
            GDBusConnection *connection);

void abrt_problems2_object_emit_signal(struct abrt_problems2_object *object,
            const char *member, GVariant *parameters,
            GDBusConnection *connection);


/*
 * Shared functionality
 */
const char *abrt_problems2_service_session_path(GDBusConnection *connection,
            const char *caller, GError **error);

uid_t abrt_problems2_service_caller_uid(GDBusConnection *connection,
            const char *caller, GError **error);

uid_t abrt_problems2_service_caller_real_uid(GDBusConnection *connection,
        const char *caller, GError **error);

const char *abrt_problems2_service_save_problem(GDBusConnection *connection,
            problem_data_t *pd, char **problem_id);

int abrt_problems2_service_remove_problem(GDBusConnection *connection,
            const char *entry_path, uid_t caller_uid, GError **error);

problem_data_t *abrt_problems2_service_entry_problem_data(const char *entry_path,
        uid_t caller_uid, GError **error);

GList *abrt_problems2_service_get_problems_nodes(uid_t uid);


unsigned abrt_problems2_service_elements_limit(uid_t uid);

off_t abrt_problems2_service_dd_size_limit(uid_t uid);

/*
 * Utilities
 */
PolkitAuthority *abrt_problems2_polkit_authority(void);

#endif/*ABRT_PROBLEMS2_SERVICE_H*/
