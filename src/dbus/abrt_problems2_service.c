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

#include <gio/gio.h>
#include "libabrt.h"
#include "problem_api.h"
#include "abrt_problems2_generated_interfaces.h"
#include "abrt_problems2_service.h"
#include "abrt_problems2_node.h"
#include "abrt_problems2_session_node.h"
#include "abrt_problems2_entry_node.h"

static GMainLoop *g_loop;
static int g_timeout_value = 10;
static int g_users_clients_limit = 5;
static GDBusNodeInfo *g_problems2_node;
static GDBusNodeInfo *g_problems2_session_node;
static GDBusNodeInfo *g_problems2_entry_node;
static GHashTable *g_problems2_entries;
static GHashTable *g_problems2_sessions;
static GHashTable *g_connected_users;

struct p2_user_info
{
    GList *sessions;
};

void p2_user_info_free(struct p2_user_info *info)
{
    if (info == NULL)
        return;

    g_list_free_full(info->sessions, NULL);
    free(info);
}

struct p2_object
{
    guint regid;
    void *node;
    void (*destructor)(void *);

    void *destroy_notify_args;
    void (*destroy_notify)(struct p2_object *);
};

void p2_object_free(struct p2_object *obj)
{
    if (obj == NULL)
        return;

    if (obj->destroy_notify)
        obj->destroy_notify(obj);

    if (obj->destructor)
        obj->destructor(obj->node);

    free(obj);
}

static int register_object(GDBusConnection *connection,
            GDBusInterfaceInfo *interface,
            char *path,
            GDBusInterfaceVTable *vtable,
            void *node,
            void (*node_destructor)(void *),
            GHashTable *table,
            struct p2_object **object)
{
    GError *error = NULL;

    /* Register the interface parsed from a XML file */
    log_debug("Registering PATH %s iface %s", path, interface->name);
    guint registration_id = g_dbus_connection_register_object(connection,
            path,
            interface,
            vtable,
            /*user data*/NULL,
            /*destroy notify*/NULL,
            &error);

    if (registration_id == 0)
    {
        error_msg("Could not register object '%s': %s", path, error->message);
        g_error_free(error);

        if (node != NULL)
        {
            free(path);
            node_destructor(node);
        }

        return -1;
    }

    if (node == NULL)
        return 0;

    struct p2_object *obj = xzalloc(sizeof(*obj));

    obj->regid = registration_id;
    obj->node = node;
    obj->destructor = node_destructor;

    g_hash_table_insert(table, path, obj);
    if (object != NULL)
        *object = obj;

    return 0;
}

static struct p2_object *register_session_object(GDBusConnection *connection,
        char *path,
        const char *caller,
        uid_t caller_uid)
{
    char *dup_caller = xstrdup(caller);
    struct p2s_node *session = abrt_problems2_session_node_new(dup_caller, caller_uid);
    struct p2_object *obj;

    const int r = register_object(connection,
                                  g_problems2_session_node->interfaces[0],
                                  path,
                                  abrt_problems2_session_node_vtable(),
                                  session,
                                  (void (*)(void *))abrt_problems2_session_node_free,
                                  g_problems2_sessions,
                                  &obj);
    if (r != 0)
        return NULL;

    return obj;
}

static char *abrt_problems2_caller_to_session_path(const char *caller)
{
    char hash_str[SHA1_RESULT_LEN*2 + 1];
    str_to_sha1str(hash_str, caller);
    return xasprintf(ABRT_P2_PATH"/Session/%s", hash_str);
}

void remove_session_from_users_list(struct p2_object *obj)
{
    struct p2_user_info *user = (struct p2_user_info *)obj->destroy_notify_args;
    user->sessions = g_list_remove(user->sessions, obj->node);
}

static struct p2s_node *get_session_for_caller(GDBusConnection *connection,
        const char *caller,
        uid_t caller_uid,
        GError **error,
        const char **path)
{
    char *session_path = abrt_problems2_caller_to_session_path(caller);
    struct p2s_node *session = abrt_problems2_service_get_node(session_path);
    if (session == NULL)
    {
        struct p2_user_info *user = g_hash_table_lookup(g_connected_users,
                                            (gconstpointer)(gint64)caller_uid);

        if (user != NULL && g_list_length(user->sessions) >= g_users_clients_limit)
        {
            g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                    "Too many sessions opened");
            free(session_path);
            return NULL;
        }

        struct p2_object *obj = register_session_object(connection, session_path, caller, caller_uid);
        if (obj == NULL)
        {
            g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                    "Cannot register Session object");
            return NULL;
        }

        session = (struct p2s_node *)obj->node;

        if (user == NULL)
        {
            user = xzalloc(sizeof(*user));
            g_hash_table_insert(g_connected_users, (gpointer)(gint64)caller_uid, user);
        }

        user->sessions = g_list_append(user->sessions, session);
        obj->destroy_notify_args = user;
        obj->destroy_notify = remove_session_from_users_list;
    }
    else if (abrt_problems2_session_check_sanity(session, caller, caller_uid, error) < 0)
    {
        free(session_path);
        log_debug("Cannot return session because the existing one did not pass sanity check.");
        return NULL;
    }

    if (path != NULL)
        *path = session_path;

    return session;
}

const char *abrt_problems2_get_session_path(GDBusConnection *connection,
        const char *caller,
        GError **error)
{
    uid_t caller_uid = abrt_problems2_service_caller_real_uid(connection, caller, error);
    if (caller_uid == (uid_t)-1)
        return NULL;

    const char *session_path = NULL;
    struct p2s_node *session = get_session_for_caller(connection, caller, caller_uid, error, &session_path);
    if (session == NULL)
        return NULL;

    return session_path;
}

uid_t abrt_problems2_service_caller_uid(GDBusConnection *connection,
        const char *caller,
        GError **error)
{
    uid_t caller_uid = abrt_problems2_service_caller_real_uid(connection, caller, error);
    if (caller_uid == (uid_t)-1)
        return (uid_t)-1;

    struct p2s_node *session = get_session_for_caller(connection, caller, caller_uid, error, NULL);
    if (session == NULL)
        return (uid_t) -1;

    if (abrt_problems2_session_is_authorized(session))
        return 0;

    return caller_uid;
}

uid_t abrt_problems2_service_caller_real_uid(GDBusConnection *connection,
        const char *caller,
        GError **error)
{
    guint caller_uid;

    GDBusProxy * proxy = g_dbus_proxy_new_sync(connection,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     NULL,
                                     "org.freedesktop.DBus",
                                     "/org/freedesktop/DBus",
                                     "org.freedesktop.DBus",
                                     NULL,
                                     error);

    if (proxy == NULL)
        return (uid_t) -1;

    GVariant *result = g_dbus_proxy_call_sync(proxy,
                                     "GetConnectionUnixUser",
                                     g_variant_new ("(s)", caller),
                                     G_DBUS_CALL_FLAGS_NONE,
                                     -1,
                                     NULL,
                                     error);

    g_object_unref(proxy);
    if (result == NULL)
        return (uid_t) -1;

    g_variant_get(result, "(u)", &caller_uid);
    g_variant_unref(result);

    log_info("Caller uid: %i", caller_uid);
    return caller_uid;
}

static struct p2_object *abrt_problems2_service_get_object(const char *path, GHashTable **table)
{
    struct p2_object *obj;

    GHashTable *unused;
    if (table == NULL)
        table = &unused;

    *table = g_problems2_entries;
    obj = g_hash_table_lookup(g_problems2_entries, path);

    if (obj == NULL)
    {
        *table = g_problems2_sessions;
        obj = g_hash_table_lookup(g_problems2_sessions, path);
    }

    return obj;
}

void *abrt_problems2_service_get_node(const char *path)
{
    struct p2_object *obj = abrt_problems2_service_get_object(path, NULL);
    return obj == NULL ? NULL : obj->node;
}

void abrt_problems2_service_remove_node(GDBusConnection *connection, const char *path)
{
    GHashTable *table;
    struct p2_object *obj = abrt_problems2_service_get_object(path, &table);

    guint regid = obj->regid;
    /* Destroys the variable obj too*/
    g_hash_table_remove(table, path);

    g_dbus_connection_unregister_object(connection, regid);
}

static const char *register_dump_dir_entry_node(GDBusConnection *connection, const char *dd_dirname)
{
    char hash_str[SHA1_RESULT_LEN*2 + 1];
    str_to_sha1str(hash_str, dd_dirname);
    char *path = xasprintf(ABRT_P2_PATH"/Entry/%s", hash_str);

    char *const dup_dirname = xstrdup(dd_dirname);
    struct p2e_node *entry = abrt_problems2_entry_node_new(dup_dirname);

    const int r = register_object(connection,
                                  g_problems2_entry_node->interfaces[0],
                                  path,
                                  abrt_problems2_entry_node_vtable(),
                                  entry,
                                  (void (*)(void *))abrt_problems2_entry_node_free,
                                  g_problems2_entries,
                                  NULL);

    if (r != 0)
        return NULL;

    return path;
}

const char *abrt_problems2_service_save_problem(GDBusConnection *connection, problem_data_t *pd, char **problem_id)
{
    char *new_problem_id = problem_data_save(pd);

    if (new_problem_id == NULL)
        return NULL;

    const char *entry_node_path = register_dump_dir_entry_node(connection, new_problem_id);

    if (entry_node_path != NULL)
    {
        if (problem_id != NULL)
            *problem_id = new_problem_id;
        else
            free(new_problem_id);

        char *uid_str = problem_data_get_content_or_NULL(pd, FILENAME_UID);
        int uid = uid_str != NULL ? atoi(uid_str) : 0;
        GVariant *parameters = g_variant_new("(oi)", entry_node_path, uid);

        GDBusMessage *message = g_dbus_message_new_signal (ABRT_P2_PATH, ABRT_P2_NS, "Crash");
        g_dbus_message_set_sender(message, ABRT_P2_BUS);
        g_dbus_message_set_body(message, parameters);

        GError *error = NULL;
        g_dbus_connection_send_message(connection, message, G_DBUS_SEND_MESSAGE_FLAGS_NONE, NULL, &error);
        g_object_unref(message);
        if (error != NULL)
        {
            error_msg("Failed to emit 'Crash': %s", error->message);
            g_free(error);
        }
    }

    return entry_node_path;
}

int abrt_problems2_service_remove_problem(GDBusConnection *connection, const char *entry_path, uid_t caller_uid, GError **error)
{
    struct p2_object *obj = g_hash_table_lookup(g_problems2_entries, entry_path);
    if (obj == NULL)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_BAD_ADDRESS, "Requested Entry does not exist");
        return -ENOENT;
    }

    const int ret = abrt_problems2_entry_node_remove((struct p2e_node *)obj->node, caller_uid, error);
    if (ret != 0)
        return ret;

    const guint regid = obj->regid;

    /* Destroys the variable obj too*/
    g_hash_table_remove(g_problems2_entries, entry_path);

    g_dbus_connection_unregister_object(connection, regid);
    return 0;
}

problem_data_t *abrt_problems2_service_entry_problem_data(const char *entry_path, uid_t caller_uid, GError **error)
{
    struct p2_object *obj = g_hash_table_lookup(g_problems2_entries, entry_path);
    if (obj == NULL)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_BAD_ADDRESS, "Requested Entry does not exist");
        return NULL;
    }

    return abrt_problems2_entry_node_problem_data((struct p2e_node *)obj->node, caller_uid, error);
}

GList *abrt_problems2_service_get_problems_nodes(uid_t uid)
{
    GList *paths = NULL;

    GHashTableIter iter;
    g_hash_table_iter_init(&iter, g_problems2_entries);

    const char *p;
    struct p2_object *obj;
    while(g_hash_table_iter_next(&iter, (gpointer)&p, (gpointer)&obj))
    {
        if (0 == abrt_problems2_entry_node_accessible_by_uid((struct p2e_node *)obj->node, uid, NULL))
            paths = g_list_prepend(paths, (gpointer)p);
    }

    return paths;
}

static int bridge_register_dump_dir_entry_node(struct dump_dir *dd, void *connection)
{
    /* Ignore return value */
    register_dump_dir_entry_node(connection, dd->dd_dirname);
    return 0;
}

static void on_bus_acquired(GDBusConnection *connection,
                            const gchar     *name,
                            gpointer         user_data)
{
    const int r = register_object(connection,
                                  g_problems2_node->interfaces[0],
                                  (char *)ABRT_P2_PATH,
                                  abrt_problems2_node_vtable(),
                                  /*node*/NULL,
                                  /*node destructor*/NULL,
                                  /*node table*/NULL,
                                  NULL);

    if (r == 0)
        for_each_problem_in_dir(g_settings_dump_location, (uid_t)-1, bridge_register_dump_dir_entry_node, connection);
}

static void on_name_acquired(GDBusConnection *connection,
                             const gchar     *name,
                             gpointer         user_data)
{
    log_debug("Acquired the name '%s' on the system bus", name);
}

static void on_name_lost(GDBusConnection *connection,
                         const gchar     *name,
                         gpointer         user_data)
{
    log_warning(_("The name '%s' has been lost, please check if other "
              "service owning the name is not running.\n"), name);
    exit(1);
}

void quit_loop(int signo)
{
    g_main_loop_quit(g_loop);
}

int main(int argc, char *argv[])
{
    /* I18n */
    setlocale(LC_ALL, "");
#if ENABLE_NLS
    bindtextdomain(PACKAGE, LOCALEDIR);
    textdomain(PACKAGE);
#endif
    guint owner_id;

    glib_init();
    abrt_init(argv);
    load_abrt_conf();

    const char *program_usage_string = _(
        "& [options]"
    );
    enum {
        OPT_v = 1 << 0,
        OPT_t = 1 << 1,
    };
    /* Keep enum above and order of options below in sync! */
    struct options program_options[] = {
        OPT__VERBOSE(&g_verbose),
        OPT_INTEGER('t', NULL, &g_timeout_value, _("Exit after NUM seconds of inactivity")),
        OPT_END()
    };
    /*unsigned opts =*/ parse_opts(argc, argv, program_options, program_usage_string);

    export_abrt_envvars(0);

    msg_prefix = "abrt-problems2"; /* for log(), error_msg() and such */

    if (getuid() != 0)
        error_msg_and_die(_("This program must be run as root."));

    GError *error = NULL;
    g_problems2_node = g_dbus_node_info_new_for_xml(g_org_freedesktop_Problems2_xml, &error);
    if (error != NULL)
        error_msg_and_die("Could not parse the default interface: %s", error->message);

    g_problems2_session_node = g_dbus_node_info_new_for_xml(g_org_freedesktop_Problems2_Session_xml, &error);
    if (error != NULL)
        error_msg_and_die("Could not parse Session interface: %s", error->message);

    g_problems2_entry_node = g_dbus_node_info_new_for_xml(g_org_freedesktop_Problems2_Entry_xml, &error);
    if (error != NULL)
        error_msg_and_die("Could not parse Session interface: %s", error->message);

    g_connected_users = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)p2_user_info_free);
    g_problems2_entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)p2_object_free);
    g_problems2_sessions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)p2_object_free);

    owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
                              ABRT_P2_BUS,
                              G_BUS_NAME_OWNER_FLAGS_NONE,
                              on_bus_acquired,
                              on_name_acquired,
                              on_name_lost,
                              NULL,
                              (GDestroyNotify)NULL);


    g_loop = g_main_loop_new(NULL, FALSE);
    signal(SIGABRT, quit_loop);
    g_main_loop_run(g_loop);
    g_main_loop_unref(g_loop);

    log_notice("Cleaning up");

    g_bus_unown_name(owner_id);

    g_hash_table_destroy(g_problems2_sessions);
    g_hash_table_destroy(g_problems2_entries);
    g_hash_table_destroy(g_connected_users);

    g_dbus_node_info_unref(g_problems2_entry_node);
    g_dbus_node_info_unref(g_problems2_session_node);
    g_dbus_node_info_unref(g_problems2_node);

    free_abrt_conf_data();

    return 0;
}
