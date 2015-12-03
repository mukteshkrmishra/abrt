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

#include <glib-object.h>
#include <gio/gio.h>

#define ABRT_P2_BUS "org.freedesktop.problems"
#define ABRT_P2_PATH "/org/freedesktop/Problems2"
#define ABRT_P2_NS "org.freedesktop.Problems2"
#define ABRT_P2_NS_MEMBER(name) ABRT_P2_NS"."name

/*
 * Service administration
 */

#define TYPE_ABRT_P2_SERVICE abrt_p2_service_get_type ()
G_DECLARE_FINAL_TYPE(AbrtP2Service, abrt_p2_service, ABRT_P2, SERVICE, GObject)

AbrtP2Service *abrt_p2_service_new(GError **error);

int abrt_p2_service_register_objects(AbrtP2Service *service,
            GDBusConnection *connection, GError **error);

const char *abrt_p2_service_session_path(AbrtP2Service *service,
            const char *caller, GError **error);

uid_t abrt_p2_service_caller_uid(AbrtP2Service *service,
            const char *caller, GError **error);

uid_t abrt_p2_service_caller_real_uid(AbrtP2Service *service,
            const char *caller, GError **error);

char *abrt_p2_service_save_problem(AbrtP2Service *service,
            GVariant *problem_info, GUnixFDList *fd_list, uid_t caller_uid,
            GError **error);

int abrt_p2_service_remove_problem(AbrtP2Service *service,
            const char *entry_path, uid_t caller_uid, GError **error);

GVariant *abrt_p2_service_entry_problem_data(AbrtP2Service *service,
            const char *entry_path, uid_t caller_uid, GError **error);

GList *abrt_p2_service_get_problems_nodes(AbrtP2Service *service, uid_t uid);

int abrt_p2_service_user_can_create_new_problem(AbrtP2Service *service,
            uid_t uid);

GVariant *abrt_p2_service_new_problem(AbrtP2Service *service,
            GVariant *problem_info, gint32 flags, uid_t caller_uid,
            GUnixFDList *fd_list, GError **error);

void abrt_p2_service_new_problem_async(AbrtP2Service *service,
                   GVariant *problem_info, gint32 flags, uid_t caller_uid,
                   GUnixFDList *fd_list,
                   GCancellable *cancellable, GAsyncReadyCallback callback,
                   gpointer user_data);

GVariant *abrt_p2_service_new_problem_finish(AbrtP2Service *service,
                   GAsyncResult *result, GError **error);

GVariant *abrt_p2_service_callers_session(AbrtP2Service *service,
            const char *caller, GError **error);

GVariant *abrt_p2_service_get_problems(AbrtP2Service *service, uid_t caller_uid,
            gint32 flags, GError **error);

GVariant *abrt_p2_service_delete_problems(AbrtP2Service *service,
            GVariant *entries, uid_t caller_uid, GError **error);

/*
 * Configuration and limits
 */
unsigned abrt_p2_service_user_clients_limit(AbrtP2Service *service, uid_t uid);

unsigned abrt_p2_service_elements_limit(AbrtP2Service *service, uid_t uid);

off_t abrt_p2_service_data_size_limit(AbrtP2Service *service, uid_t uid);

unsigned abrt_p2_service_user_problems_limit(AbrtP2Service *service, uid_t uid);

unsigned abrt_p2_service_new_problem_throtling_magnitude(AbrtP2Service *service, uid_t uid);

unsigned abrt_p2_service_new_problems_batch(AbrtP2Service *service, uid_t uid);

#if 0
/*
 * D-Bus object representation
 */
struct abrt_p2_object;

AbrtP2Service *abrt_p2_object_service(struct abrt_p2_object *object);

void *abrt_p2_object_get_node(struct abrt_p2_object *object);

void abrt_p2_object_destroy(struct abrt_p2_object *object);

void abrt_p2_object_emit_signal(struct abrt_p2_object *object,
            const char *member, GVariant *parameters);
#endif

#endif/*ABRT_PROBLEMS2_SERVICE_H*/
