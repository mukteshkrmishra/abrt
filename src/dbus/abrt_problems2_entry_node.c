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

#include "libabrt.h"
#include "abrt_problems2_entry_node.h"
#include "abrt_problems2_service.h"

struct p2e_node
{
    char *p2e_dirname;
};

struct p2e_node *abrt_problems2_entry_node_new(char *dirname)
{
    struct p2e_node *entry = xmalloc(sizeof(*entry));
    entry->p2e_dirname = dirname;

    return entry;
}

void abrt_problems2_entry_node_free(struct p2e_node *entry)
{
    if (entry == NULL)
        return;

    free(entry->p2e_dirname);
    entry->p2e_dirname = (void *)0xDEADBEEF;
}

int abrt_problems2_entry_node_accessible_by_uid(struct p2e_node *entry, uid_t uid, struct dump_dir **dd)
{
    struct dump_dir *tmp = dd_opendir(entry->p2e_dirname,   DD_OPEN_FD_ONLY
                                                          | DD_FAIL_QUIETLY_ENOENT
                                                          | DD_FAIL_QUIETLY_EACCES);
    if (tmp == NULL)
    {
        VERB2 perror_msg("can't open problem directory '%s'", entry->p2e_dirname);
        return -ENOTDIR;
    }

    int ret = dd_accessible_by_uid(tmp, uid) ? 0 : -EACCES;

    if (dd == NULL)
        dd_close(tmp);
    else
        *dd = tmp;

    return ret;
}

int  abrt_problems2_entry_node_remove(struct p2e_node *entry, uid_t caller_uid, GError **error)
{
    struct dump_dir *dd = NULL;
    int ret = abrt_problems2_entry_node_accessible_by_uid(entry, caller_uid, &dd);
    if (ret != 0)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                    "You are not authorized to delete the problem");
        return ret;
    }

    dd = dd_fdopendir(dd, DD_DONT_WAIT_FOR_LOCK);
    if (dd == NULL)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "Cannot lock the problem. Check system logs.");
        return -EWOULDBLOCK;
    }

    ret = dd_delete(dd);

    if (ret != 0)
    {
        dd_close(dd);
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "Failed to remove problem data. Check system logs.");
    }

    return ret;
}

problem_data_t *abrt_problems2_entry_node_problem_data(struct p2e_node *node, uid_t caller_uid, GError **error)
{
    struct dump_dir *dd = NULL;

    if (abrt_problems2_entry_node_accessible_by_uid(node, caller_uid, &dd) != 0)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                    "You are not authorized to access the problem");
        return NULL;
    }

    dd = dd_fdopendir(dd, DD_OPEN_READONLY);
    if (dd == NULL)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "Cannot lock the problem. Check system logs.");
        return NULL;
    }

    problem_data_t *pd = create_problem_data_from_dump_dir(dd);
    dd_close(dd);

    return pd;
}

static struct p2e_node *get_entry(GDBusConnection *connection,
                          const gchar *caller,
                          const gchar *object_path,
                          struct dump_dir **dd,
                          GError **error)
{
    uid_t caller_uid = abrt_problems2_service_caller_uid(connection, caller, error);
    if (caller_uid == (uid_t)-1)
        return NULL;

    struct p2e_node *node = abrt_problems2_service_get_node(object_path);
    if (node == NULL)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_BAD_ADDRESS,
                    "Requested path does not exist");
        return NULL;
    }

    if (0 != abrt_problems2_entry_node_accessible_by_uid(node, caller_uid, dd))
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                    "You are not authorized to access the problem");
        return NULL;
    }

    return node;
}

/* D-Bus method handler
 */
static void dbus_method_call(GDBusConnection *connection,
                        const gchar *caller,
                        const gchar *object_path,
                        const gchar *interface_name,
                        const gchar *method_name,
                        GVariant    *parameters,
                        GDBusMethodInvocation *invocation,
                        gpointer    user_data)
{
    log_debug("Problems2.Entry method : %s", method_name);

    GError *error = NULL;
    struct dump_dir *dd;
    struct p2e_node *node = get_entry(connection, caller, object_path, &dd, &error);
    if (node == NULL)
    {
        g_dbus_method_invocation_return_gerror(invocation, error);
        g_error_free(error);
        return;
    }

    if (strcmp(method_name, "GetSemanticElement") == 0);
    {
        return;
    }

    if (strcmp(method_name, "SetSemanticElement") == 0);
    {
        return;
    }

    if (strcmp(method_name, "ReadElements") == 0);
    {
        return;
    }

    if (strcmp(method_name, "SaveElements") == 0);
    {
        return;
    }

    if (strcmp(method_name, "DeleteElements") == 0);
    {
        return;
    }

    error_msg("BUG: org.freedesktop.Problems2.Entry does not have method: %s", method_name);
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
            "The method has to be implemented");
}

#define GET_PLAIN_TEXT_PROPERTY(name, element) \
        if (strcmp(name, property_name) == 0) \
        { \
            char *tmp_value = dd_load_text(dd, element); \
            retval = g_variant_new_string(tmp_value ? tmp_value : ""); \
            free(tmp_value); \
            goto return_property_value; \
        }

#define GET_INTEGER_PROPERTY(name, element, S) \
        if (strcmp(name, property_name) == 0) \
        { \
            uint##S##_t tmp_value = 0; \
            dd_load_uint##S (dd, element, &tmp_value); \
            retval = g_variant_new_uint##S (tmp_value); \
            goto return_property_value; \
        }

#define GET_UINT32_PROPERTY(name, element) GET_INTEGER_PROPERTY(name, element, 32)

#define GET_UINT64_PROPERTY(name, element) GET_INTEGER_PROPERTY(name, element, 64)

static GVariant *dbus_get_property(GDBusConnection *connection,
                        const gchar *caller,
                        const gchar *object_path,
                        const gchar *interface_name,
                        const gchar *property_name,
                        GError      **error,
                        gpointer    user_data)
{
    log_debug("Problems2.Entry get property : %s", property_name);

    GVariant *retval;
    struct dump_dir *dd;
    struct p2e_node *node = get_entry(connection, caller, object_path, &dd, error);
    if (node == NULL)
        return NULL;

    if (strcmp("id", property_name) == 0)
    {
        retval = g_variant_new_string(dd->dd_dirname);
        goto return_property_value;
    }

    GET_PLAIN_TEXT_PROPERTY("user", FILENAME_USERNAME)
    GET_PLAIN_TEXT_PROPERTY("hostname", FILENAME_HOSTNAME)
    GET_PLAIN_TEXT_PROPERTY("type", FILENAME_TYPE)
    GET_PLAIN_TEXT_PROPERTY("executable", FILENAME_EXECUTABLE)
    GET_PLAIN_TEXT_PROPERTY("command_line_arguments", FILENAME_CMDLINE)
    GET_PLAIN_TEXT_PROPERTY("component", FILENAME_COMPONENT)
    GET_PLAIN_TEXT_PROPERTY("uuid", FILENAME_UUID)
    GET_PLAIN_TEXT_PROPERTY("duphash", FILENAME_DUPHASH)
    GET_PLAIN_TEXT_PROPERTY("reason", FILENAME_REASON)
    GET_PLAIN_TEXT_PROPERTY("technical_details", FILENAME_NOT_REPORTABLE)

    GET_UINT32_PROPERTY("uid", FILENAME_UID)
    GET_UINT32_PROPERTY("count", FILENAME_COUNT)

    GET_UINT64_PROPERTY("first_occurrence", FILENAME_TIME)
    GET_UINT64_PROPERTY("last_occurrence", FILENAME_LAST_OCCURRENCE)


    if (strcmp("package", property_name) == 0)
    {
       return NULL;
    }

    if (strcmp("reports", property_name) == 0)
    {
       return NULL;
    }

    if (strcmp("solutions", property_name) == 0)
    {
       return NULL;
    }

    if (strcmp("desktop_file", property_name) == 0)
    {
       return NULL;
    }

    if (strcmp("elements", property_name) == 0)
    {
       return NULL;
    }

    if (strcmp("semantic_elements", property_name) == 0)
    {
       return NULL;
    }

    if (strcmp("is_reported", property_name) == 0)
    {
       return NULL;
    }

    if (strcmp("can_be_reported", property_name) == 0)
    {
       return NULL;
    }

    if (strcmp("is_remote", property_name) == 0)
    {
       return NULL;
    }

    dd_close(dd);
    error_msg("Unknown property %s", property_name);
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
            "BUG: the property getter has to be implemented");
    return NULL;

return_property_value:
    dd_close(dd);
    return retval;
}

#ifdef PROBLEMS2_PROPERTY_SET
static gboolean dbus_set_property(GDBusConnection *connection,
                        const gchar *caller,
                        const gchar *object_path,
                        const gchar *interface_name,
                        const gchar *property_name,
                        GVariant    *args,
                        GError      **error,
                        gpointer    user_data)
{
    log_debug("Problems2.Entry set property : %s", property_name);

    struct dump_dir *dd;
    struct p2e_node *node = get_entry(connection, caller, object_path, &dd, error);
    if (node == NULL)
        return FALSE;

    if (strcmp("id", property_name) == 0)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_PROPERTY_READ_ONLY);
        return FALSE;
    }

    if (strcmp("uid", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("user", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("hostname", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("type", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("first_occurrence", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("last_occurrence", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("count", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("executable", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("command_line_arguments", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("component", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("package", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("uuid", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("duphash", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("reports", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("reason", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("solutions", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("technical_details", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("desktop_file", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("elements", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("semantic_elements", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("is_reported", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("can_be_reported", property_name) == 0)
    {
        return FALSE;
    }

    if (strcmp("is_remote", property_name) == 0)
    {
        return FALSE;
    }

    error_msg("Unknown property %s", property_name);
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY,
            "BUG: the property setter has to be implemented");
    return FALSE;
}
#endif/*PROBLEMS2_PROPERTY_SET*/

GDBusInterfaceVTable *abrt_problems2_entry_node_vtable(void)
{
    static GDBusInterfaceVTable default_vtable =
    {
        .method_call = dbus_method_call,
        .get_property = dbus_get_property,
#if PROBLEMS2_PROPERTY_SET
        .set_property = dbus_set_property,
#else/*PROBLEMS2_PROPERTY_SET*/
        .set_property = NULL,
#endif/*PROBLEMS2_PROPERTY_SET*/

    };

    return &default_vtable;
}
