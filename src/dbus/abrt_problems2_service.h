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

#define ABRT_P2_BUS "org.freedesktop.problems"
#define ABRT_P2_PATH "/org/freedesktop/Problems2"
#define ABRT_P2_NS "org.freedesktop.Problems2"
#define ABRT_P2_NS_MEMBER(name) ABRT_P2_NS"."name

const char *abrt_problems2_get_session_path(GDBusConnection *connection, const char *caller, GError **error);
uid_t abrt_problems2_service_caller_uid(GDBusConnection *connection, const char *caller, GError **error);
uid_t abrt_problems2_service_caller_real_uid(GDBusConnection *connection, const char *caller, GError **error);

#define ABRT_PROBLEMS2_ERROR (g_quark_from_static_string("abrt-problems2-error"))

enum
{
    ABRT_PROBLEMS2_INVALID_SESSION,
};

#endif/*ABRT_PROBLEMS2_SERVICE_H*/
