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
static GDBusProxy *g_proxy_dbus;
static int g_timeout_value = 10;
static int g_users_clients_limit = 5;
static GHashTable *g_connected_users;
struct abrt_problems2_object *g_problems2_object;
static PolkitAuthority *g_polkit_authority;

PolkitAuthority *abrt_problems2_polkit_authority(void)
{
    return g_polkit_authority;
}

struct p2_user_info
{
    unsigned sessions;
    unsigned new_problems;
    time_t new_problem_last;
};

void p2_user_info_free(struct p2_user_info *info)
{
    if (info == NULL)
        return;

    info->sessions = (unsigned)-1;

    free(info);
}

struct abrt_problems2_object_type
{
    GDBusNodeInfo *node;
    GDBusInterfaceInfo *iface;
    GDBusInterfaceVTable *vtable;
    GHashTable *objects;
} g_problems2_type, g_problems2_session_type, g_problems2_entry_type;

static void abrt_problems2_object_type_init(struct abrt_problems2_object_type *type,
        const char *xml_node,
        GDBusInterfaceVTable *vtable)
{
    GError *error = NULL;
    type->node = g_dbus_node_info_new_for_xml(xml_node, &error);
    if (error != NULL)
        error_msg_and_die("Could not parse interface: %s", error->message);

    type->iface = type->node->interfaces[0];
    type->vtable = vtable;
    type->objects = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);
}

static void abrt_problems2_object_type_destroy(struct abrt_problems2_object_type *type)
{
    g_hash_table_destroy(type->objects);
    type->objects = (void *)0xDEADBEEF;

    g_dbus_node_info_unref(type->node);
    type->node = (void *)0xDEADBEEF;
}

struct abrt_problems2_object
{
    char *path;
    guint regid;
    void *node;
    void (*destructor)(struct abrt_problems2_object *);
    struct abrt_problems2_object_type *type;
};

void abrt_problems2_object_free(struct abrt_problems2_object *obj)
{
    if (obj == NULL)
        return;

    if (obj->destructor)
        obj->destructor(obj);

    g_hash_table_remove(obj->type->objects, obj->path);

    obj->node = (void *)0xDEADBEAF;
    obj->destructor = (void *)0xDEADBEAF;

    free(obj->path);
    obj->path = (void *)0xDEADBEAF;

    obj->regid = (guint)-1;

    free(obj);
}

void *abrt_problems2_object_get_node(struct abrt_problems2_object *object)
{
    return object->node;
}

void abrt_problems2_object_destroy(struct abrt_problems2_object *object,
        GDBusConnection *connection)
{
    log_debug("Unregistering object: %s", object->path);
    g_dbus_connection_unregister_object(connection, object->regid);
}

static int register_object(GDBusConnection *connection,
        struct abrt_problems2_object_type *type,
        char *path,
        void *node,
        void (*destructor)(struct abrt_problems2_object *),
        struct abrt_problems2_object **object)
{
    GError *error = NULL;
    struct abrt_problems2_object *obj = NULL;
    obj = xzalloc(sizeof(*obj));
    obj->path = path;
    obj->node = node;
    obj->destructor = destructor;
    obj->type = type;

    /* Register the interface parsed from a XML file */
    log_debug("Registering PATH %s iface %s", path, type->iface->name);
    guint registration_id = g_dbus_connection_register_object(connection,
            path,
            type->iface,
            type->vtable,
            obj,
            (GDestroyNotify)abrt_problems2_object_free,
            &error);

    if (registration_id == 0)
    {
        error_msg("Could not register object '%s': %s", path, error->message);
        g_error_free(error);

        if (obj != NULL)
            abrt_problems2_object_free(obj);

        return -1;
    }

    log_debug("Registered object: %d", registration_id);

    obj->regid = registration_id;

    g_hash_table_insert(obj->type->objects, path, obj);

    if (object != NULL)
        *object = obj;

    return 0;
}

void session_object_destructor(struct abrt_problems2_object *obj)
{
    struct p2s_node *session = (struct p2s_node *)obj->node;

    uid_t uid = abrt_problems2_session_uid(session);
    struct p2_user_info *user = g_hash_table_lookup(g_connected_users,
                                            (gconstpointer)(gint64)uid);

    user->sessions -= 1;
    if (user->sessions == 0)
        g_hash_table_remove(g_connected_users, (gconstpointer)(gint64)uid);

    abrt_problems2_session_node_free(session);
}

static struct abrt_problems2_object *register_session_object(GDBusConnection *connection,
        char *path,
        const char *caller,
        uid_t caller_uid,
        GError **error)
{
    struct p2_user_info *user = g_hash_table_lookup(g_connected_users,
                                        (gconstpointer)(gint64)caller_uid);

    if (user != NULL && user->sessions >= g_users_clients_limit)
    {
        log_warning("User %lu reached the limit of opened sessions (%d)", (long unsigned)caller_uid, g_users_clients_limit);
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                    "Too many sessions opened");
        free(path);
        return NULL;
    }

    char *dup_caller = xstrdup(caller);
    struct p2s_node *session = abrt_problems2_session_node_new(dup_caller, caller_uid);
    struct abrt_problems2_object *obj;

    const int r = register_object(connection,
                                  &g_problems2_session_type,
                                  path,
                                  session,
                                  session_object_destructor,
                                  &obj);

    if (r != 0)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                    "Cannot register Session object");
        return NULL;
    }

    if (user == NULL)
    {
        user = xzalloc(sizeof(*user));
        g_hash_table_insert(g_connected_users, (gpointer)(gint64)caller_uid, user);
    }

    ++user->sessions;

    return obj;
}

static char *abrt_problems2_caller_to_session_path(const char *caller)
{
    char hash_str[SHA1_RESULT_LEN*2 + 1];
    str_to_sha1str(hash_str, caller);
    return xasprintf(ABRT_P2_PATH"/Session/%s", hash_str);
}

static struct abrt_problems2_object *get_session_for_caller(GDBusConnection *connection,
        const char *caller,
        uid_t caller_uid,
        GError **error)
{
    char *session_path = abrt_problems2_caller_to_session_path(caller);

    struct abrt_problems2_object *obj = g_hash_table_lookup(g_problems2_session_type.objects, session_path);
    if (obj == NULL)
    {
        log_debug("Caller does not have Session: %s", caller);
        return register_session_object(connection, session_path, caller, caller_uid, error);
    }

    free(session_path);

    struct p2s_node *session = abrt_problems2_object_get_node(obj);
    if (abrt_problems2_session_check_sanity(session, caller, caller_uid, error) != 0)
    {
        log_debug("Cannot return session because the existing one did not pass sanity check.");
        return NULL;
    }

    return obj;
}

const char *abrt_problems2_service_session_path(GDBusConnection *connection,
        const char *caller,
        GError **error)
{
    uid_t caller_uid = abrt_problems2_service_caller_real_uid(connection, caller, error);
    if (caller_uid == (uid_t)-1)
        return NULL;

    struct abrt_problems2_object *obj = get_session_for_caller(connection, caller, caller_uid, error);
    return obj == NULL ? NULL : obj->path;
}

uid_t abrt_problems2_service_caller_uid(GDBusConnection *connection,
        const char *caller,
        GError **error)
{
    uid_t caller_uid = abrt_problems2_service_caller_real_uid(connection, caller, error);
    if (caller_uid == (uid_t)-1)
        return (uid_t)-1;

    struct abrt_problems2_object *obj = get_session_for_caller(connection, caller, caller_uid, error);
    if (obj == NULL)
        return (uid_t) -1;

    struct p2s_node *session = abrt_problems2_object_get_node(obj);
    if (abrt_problems2_session_is_authorized(session))
        return 0;

    return caller_uid;
}

uid_t abrt_problems2_service_caller_real_uid(GDBusConnection *connection,
        const char *caller,
        GError **error)
{
    guint caller_uid;
    if (g_proxy_dbus == NULL)
        g_proxy_dbus = g_dbus_proxy_new_sync(connection,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     NULL,
                                     "org.freedesktop.DBus",
                                     "/org/freedesktop/DBus",
                                     "org.freedesktop.DBus",
                                     NULL,
                                     error);

    if (g_proxy_dbus == NULL)
        return (uid_t) -1;

    GVariant *result = g_dbus_proxy_call_sync(g_proxy_dbus,
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

void entry_object_destructor(struct abrt_problems2_object *obj)
{
    struct p2e_node *entry = (struct p2e_node *)obj->node;

    abrt_problems2_entry_node_free(entry);
}

static const char *register_dump_dir_entry_node(GDBusConnection *connection, const char *dd_dirname)
{
    char hash_str[SHA1_RESULT_LEN*2 + 1];
    str_to_sha1str(hash_str, dd_dirname);
    char *path = xasprintf(ABRT_P2_PATH"/Entry/%s", hash_str);

    char *const dup_dirname = xstrdup(dd_dirname);
    struct p2e_node *entry = abrt_problems2_entry_node_new(dup_dirname);

    const int r = register_object(connection,
                                  &g_problems2_entry_type,
                                  path,
                                  entry,
                                  entry_object_destructor,
                                  NULL);

    if (r != 0)
        return NULL;

    return path;
}

void abrt_problems2_object_emit_signal(struct abrt_problems2_object *object,
        const char *member,
        GVariant *parameters,
        GDBusConnection *connection)
{
    GDBusMessage *message = g_dbus_message_new_signal(object->path, object->type->iface->name, member);
    g_dbus_message_set_sender(message, ABRT_P2_BUS);
    g_dbus_message_set_body(message, parameters);

    log_debug("Emitting signal '%s'", member);

    GError *error = NULL;
    g_dbus_connection_send_message(connection, message, G_DBUS_SEND_MESSAGE_FLAGS_NONE, NULL, &error);
    g_object_unref(message);
    if (error != NULL)
    {
        error_msg("Failed to emit signal '%s': %s", member, error->message);
        g_free(error);
    }
}

struct save_problem_args
{
    GVariant *problem_info;
    GUnixFDList *fd_list;
    uid_t caller_uid;
    GError **error;
};

static int wrapped_abrt_problems2_entry_save_elements(struct dump_dir *dd,
        struct save_problem_args *args)
{
    return abrt_problems2_entry_save_elements(dd, P2E_ALL_FATAL, args->problem_info,
                  args->fd_list, args->caller_uid, args->error);
}

const char *abrt_problems2_service_save_problem(GDBusConnection *connection,
        const char *type_str, GVariant *problem_info, GUnixFDList *fd_list,
        uid_t caller_uid, char **problem_id, GError **error)
{
    struct save_problem_args args = {
        .problem_info = problem_info,
        .fd_list = fd_list,
        .caller_uid = caller_uid,
        .error = error,
    };

    struct dump_dir *dd = create_dump_dir(g_settings_dump_location, type_str, /*fs owner*/0,
                            (save_data_call_back)wrapped_abrt_problems2_entry_save_elements, (void *)&args);

    if (dd == NULL)
        return NULL;

    const char *entry_node_path = register_dump_dir_entry_node(connection, dd->dd_dirname);

    if (entry_node_path != NULL)
    {
        if (problem_id != NULL)
            *problem_id = xstrdup(dd->dd_dirname);

        uid_t uid = dd_get_owner(dd);
        GVariant *parameters = g_variant_new("(oi)", entry_node_path, (gint32)uid);
        abrt_problems2_object_emit_signal(g_problems2_object, "Crash", parameters, connection);
    }

    dd_close(dd);

    return entry_node_path;
}

int abrt_problems2_service_remove_problem(GDBusConnection *connection,
        const char *entry_path,
        uid_t caller_uid,
        GError **error)
{
    struct abrt_problems2_object *obj = g_hash_table_lookup(g_problems2_entry_type.objects, entry_path);
    if (obj == NULL)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_BAD_ADDRESS, "Requested Entry does not exist");
        return -ENOENT;
    }

    const int ret = abrt_problems2_entry_node_remove((struct p2e_node *)obj->node, caller_uid, error);
    if (ret != 0)
        return ret;

    abrt_problems2_object_destroy(obj, connection);
    return 0;
}

problem_data_t *abrt_problems2_service_entry_problem_data(const char *entry_path,
        uid_t caller_uid,
        GError **error)
{
    struct abrt_problems2_object *obj = g_hash_table_lookup(g_problems2_entry_type.objects, entry_path);
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
    g_hash_table_iter_init(&iter, g_problems2_entry_type.objects);

    const char *p;
    struct abrt_problems2_object *obj;
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
                                  &g_problems2_type,
                                  (char *)ABRT_P2_PATH,
                                  /*node*/NULL,
                                  /*node destructor*/NULL,
                                  &g_problems2_object);

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


    abrt_problems2_object_type_init(&g_problems2_type,
            g_org_freedesktop_Problems2_xml, abrt_problems2_node_vtable());

    abrt_problems2_object_type_init(&g_problems2_session_type,
            g_org_freedesktop_Problems2_Session_xml, abrt_problems2_session_node_vtable());

    abrt_problems2_object_type_init(&g_problems2_entry_type,
            g_org_freedesktop_Problems2_Entry_xml, abrt_problems2_entry_node_vtable());

    g_connected_users = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)p2_user_info_free);

    owner_id = g_bus_own_name(G_BUS_TYPE_SYSTEM,
                              ABRT_P2_BUS,
                              G_BUS_NAME_OWNER_FLAGS_NONE,
                              on_bus_acquired,
                              on_name_acquired,
                              on_name_lost,
                              NULL,
                              (GDestroyNotify)NULL);

    GError *error = NULL;
    g_polkit_authority = polkit_authority_get_sync(NULL, &error);
    if (g_polkit_authority == NULL)
        error_msg_and_die(_("Failed to get PolkitAuthority: %s"), error->message);

    g_loop = g_main_loop_new(NULL, FALSE);

    signal(SIGABRT, quit_loop);

    g_main_loop_run(g_loop);
    g_main_loop_unref(g_loop);

    log_notice("Cleaning up");

    g_bus_unown_name(owner_id);

    g_hash_table_destroy(g_connected_users);

    abrt_problems2_object_type_destroy(&g_problems2_entry_type);
    abrt_problems2_object_type_destroy(&g_problems2_session_type);
    abrt_problems2_object_type_destroy(&g_problems2_type);

    if (g_proxy_dbus != NULL)
        g_object_unref(g_proxy_dbus);

    free_abrt_conf_data();

    return 0;
}

unsigned abrt_problems2_service_elements_limit(uid_t uid)
{
    return 100;
}

off_t abrt_problems2_service_dd_size_limit(uid_t uid)
{
    return uid == 0 ? 0 : 2L*1024L*1024L*1024L;
}

/* TODO: return -E2BIG when user owns too many problem directories */
int abrt_problems2_service_allowed_new_problem(uid_t uid)
{
    if (uid == 0)
        return 1;

    time_t current = time(NULL);
    if (current == (time_t)-1)
    {
        perror_msg("time");
        return -1;
    }

    struct p2_user_info *user = g_hash_table_lookup(g_connected_users,
                                            (gconstpointer)(gint64)uid);
    if (user == NULL)
    {
        error_msg("User does not have Session: uid=%lu", (long unsigned)uid);
        return -1;
    }

    if (current < user->new_problem_last)
    {
        error_msg("The last problem was created in future: uid=%lu", (long unsigned)uid);
        return -1;
    }

    /* Allows 10 new problems to be created in a batch but then allow only 1 new
     * problem per 16s.
     */
    const long unsigned off = current - user->new_problem_last;
    /* off / 16; */
    const long unsigned incr = (off >> 4);

    /* Avoid overflow. Beware of adding operation inside the condition! */
    if (   incr > 10
        || (user->new_problems += incr) > 10)
        user->new_problems = 10;

    log_debug("NewProblem limit: last %lu, current %lu, increment %lu, remaining %u",
            (long unsigned)user->new_problem_last, (long unsigned)current, incr, user->new_problems);

    if (user->new_problems == 0)
        return 0;

    user->new_problem_last = current;
    return user->new_problems--;
}
