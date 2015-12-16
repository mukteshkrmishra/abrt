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

#ifndef ABRT_PROBLEMS2_SESSION_H
#define ABRT_PROBLEMS2_SESSION_H

#include "abrt_problems2_task.h"

#include <polkit/polkit.h>
#include <glib-object.h>
#include <gio/gio.h>
#include <inttypes.h>

G_BEGIN_DECLS

#define TYPE_ABRT_P2_SESSION abrt_p2_session_get_type ()
G_DECLARE_FINAL_TYPE(AbrtP2Session, abrt_p2_session, ABRT_P2, SESSION, GObject)

AbrtP2Session *abrt_p2_session_new(char *caller, uid_t uid);

uid_t abrt_p2_session_uid(AbrtP2Session *session);
const char *abrt_p2_session_caller(AbrtP2Session *session);
int abrt_p2_session_is_authorized(AbrtP2Session *session);

gint32 abrt_p2_session_authorize(AbrtP2Session *session, GVariant *parameters);
void abrt_p2_session_close(AbrtP2Session *session);
int abrt_p2_session_check_sanity(AbrtP2Session *session, const char *caller, uid_t caller_uid, GError **error);

const char *abrt_p2_session_locale(AbrtP2Session *session, char *locale);
void abrt_p2_session_set_locale(AbrtP2Session *session, char *locale);

uint32_t abrt_p2_session_add_task(AbrtP2Session *session, AbrtP2Task *task, GError **error);
void abrt_p2_session_remove_task(AbrtP2Session *session, AbrtP2Task *task, GError **error);
int abrt_p2_session_owns_task(AbrtP2Session *session, AbrtP2Task *task);
GList *abrt_p2_session_tasks(AbrtP2Session *session);
int abrt_p2_session_tasks_count(AbrtP2Session *session);

void abrt_p2_session_clean_tasks(AbrtP2Session *session);

/*
 * Class methods
 */
PolkitAuthority *abrt_p2_session_class_set_polkit_authority(PolkitAuthority *pk_authority);
PolkitAuthority *abrt_p2_session_class_polkit_authority(void);
PolkitAuthority *abrt_p2_session_class_release_polkit_authority(void);

G_END_DECLS

#endif/*ABRT_PROBLEMS2_SESSION_H*/
