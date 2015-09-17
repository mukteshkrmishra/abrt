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
static GDBusNodeInfo *g_problems2_node;
static GDBusNodeInfo *g_problems2_session_node;
static GDBusNodeInfo *g_problems2_entry_node;
static GHashTable *g_problems2_entries;

struct p2_object
{
    guint regid;
    void *node;
    void (*destructor)(void *);
};

void p2_object_free(struct p2_object *obj)
{
    if (obj == NULL)
        return;

    if (obj->destructor)
        obj->destructor(obj->node);

    free(obj);
}

static struct p2s_node *register_session_object(GDBusConnection *connection,
        const char *caller,
        uid_t caller_uid)
{
    /* Static counter */
    static unsigned long session_counter = 1;

    /* Hopefully, there will not be a system running so long that the above
     * limit will be reached */
    if (session_counter == ULONG_MAX)
        error_msg_and_die("Reached the limit of opened sessions");

    char *path = xasprintf(ABRT_P2_PATH"/Session/%lu", session_counter);
    session_counter++;

    GError *error = NULL;

    /* Register the interface parsed from a XML file */
    log("Registering PATH %s iface %s", path, g_problems2_session_node->interfaces[0]->name);
    guint registration_id = g_dbus_connection_register_object(connection,
            path,
            g_problems2_session_node->interfaces[0],
            abrt_problems2_session_node_vtable(),
            /*user data*/NULL,
            /*destroy notify*/NULL,
            &error);

    if (registration_id == 0)
    {
        error_msg("Could not register object '%s': %s", path, error->message);
        g_error_free(error);

        return NULL;
    }

    char *dup_caller = xstrdup(caller);
    struct p2s_node *session = abrt_problems2_session_new_node(path, dup_caller, caller_uid, registration_id);
    if (session == NULL)
        error_msg_and_die("Failed to create new Session node");

    return session;
}

static struct p2s_node *get_session_for_caller(GDBusConnection *connection,
        const char *caller,
        uid_t caller_uid,
        GError **error)
{
    struct p2s_node *session = abrt_problems2_session_find_node(caller);
    if (session == NULL)
        session = register_session_object(connection, caller, caller_uid);

    return session;
}

const char *abrt_problems2_get_session_path(GDBusConnection *connection,
        const char *caller,
        GError **error)
{
    uid_t caller_uid = abrt_problems2_service_caller_real_uid(connection, caller, error);
    if (caller_uid == (uid_t)-1)
        return NULL;

    struct p2s_node *session = get_session_for_caller(connection, caller, caller_uid, error);
    if (session == NULL)
        return NULL;

    return abrt_problems2_session_node_path(session);
}

uid_t abrt_problems2_service_caller_uid(GDBusConnection *connection,
        const char *caller,
        GError **error)
{
    uid_t caller_uid = abrt_problems2_service_caller_real_uid(connection, caller, error);
    if (caller_uid == (uid_t)-1)
        return (uid_t)-1;

    struct p2s_node *session = get_session_for_caller(connection, caller, caller_uid, error);
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

    if (result == NULL)
        return (uid_t) -1;

    g_variant_get(result, "(u)", &caller_uid);
    g_variant_unref(result);

    log_info("Caller uid: %i", caller_uid);
    return caller_uid;
}

void *abrt_problems2_service_get_node(const char *path)
{
    struct p2_object *obj = g_hash_table_lookup(g_problems2_entries, path);
    return obj == NULL ? NULL : obj->node;
}

static const char *register_dump_dir_entry_node(GDBusConnection *connection, const char *dd_dirname)
{
    char hash_str[SHA1_RESULT_LEN*2 + 1];
    str_to_sha1str(hash_str, dd_dirname);
    char *path = xasprintf(ABRT_P2_PATH"/Entry/%s", hash_str);

    GError *error = NULL;

    /* Register the interface parsed from a XML file */
    log("Registering PATH %s iface %s", path, g_problems2_entry_node->interfaces[0]->name);
    guint registration_id = g_dbus_connection_register_object(connection,
            path,
            g_problems2_entry_node->interfaces[0],
            abrt_problems2_entry_node_vtable(),
            /*user data*/NULL,
            /*destroy notify*/NULL,
            &error);

    if (registration_id == 0)
    {
        error_msg("Could not register object '%s': %s", path, error->message);
        free(path);
        g_error_free(error);

        return NULL;
    }

    char *const dup_dirname = xstrdup(dd_dirname);
    struct p2e_node *entry = abrt_problems2_entry_node_new(dup_dirname);
    struct p2_object *obj = xmalloc(sizeof(*obj));
    obj->regid = registration_id,
    obj->node = (void *)entry,
    obj->destructor = (void (*)(void *))abrt_problems2_entry_node_free,

    /* the hash table takes ownership of path and entry */
    g_hash_table_insert(g_problems2_entries, path, obj);

    return path;
}

const char *abrt_problems2_service_save_problem(GDBusConnection *connection, problem_data_t *pd, char **problem_id)
{
    char *new_problem_id = problem_data_save(pd);

    if (new_problem_id == NULL)
        return NULL;

    const char *entry_node_path = register_dump_dir_entry_node(connection, new_problem_id);

    if (problem_id != NULL)
        *problem_id = new_problem_id;
    else
        free(new_problem_id);

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
    GError *error = NULL;
    /* Register the interface parsed from a XML file */
    log("Registering PATH %s iface %s", ABRT_P2_PATH, g_problems2_node->interfaces[0]->name);
    guint registration_id = g_dbus_connection_register_object(connection,
            ABRT_P2_PATH,
            g_problems2_node->interfaces[0],
            abrt_problems2_node_vtable(),
            /*user data*/NULL,
            /*destroy notify*/NULL,
            &error);

    if (registration_id == 0)
    {
        error_msg("Could not register object '%s': %s", ABRT_P2_PATH, error->message);
        g_error_free(error);
    }

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

    g_problems2_entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)p2_object_free);

    owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
                              ABRT_P2_BUS,
                              G_BUS_NAME_OWNER_FLAGS_NONE,
                              on_bus_acquired,
                              on_name_acquired,
                              on_name_lost,
                              NULL,
                              (GDestroyNotify)NULL);

    g_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(g_loop);

    log_notice("Cleaning up");

    g_bus_unown_name(owner_id);

    g_dbus_node_info_unref(g_problems2_node);

    free_abrt_conf_data();

    return 0;
}
