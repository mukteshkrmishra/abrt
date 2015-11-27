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

#include <glib-object.h>
#include <gio/gio.h>
#include "libabrt.h"
#include "problem_api.h"
#include "abrt_problems2_generated_interfaces.h"
#include "abrt_problems2_service.h"
#include "abrt_problems2_node.h"
#include "abrt_problems2_session_node.h"
#include "abrt_problems2_entry_node.h"

PolkitAuthority *g_polkit_authority;

/*
 * DBus object type
 */
struct problems2_object_type
{
    GDBusNodeInfo *node;
    GDBusInterfaceInfo *iface;
    GDBusInterfaceVTable *vtable;
    GHashTable *objects;
};

static int problems2_object_type_init(struct problems2_object_type *type,
        const char *xml_node,
        GDBusInterfaceVTable *vtable)
{
    GError *local_error = NULL;
    type->node = g_dbus_node_info_new_for_xml(xml_node, &local_error);
    if (local_error != NULL)
    {
        log_info("Failed to parse XML interface file: %s", local_error->message);
        g_error_free(local_error);
        return -1;
    }

    type->iface = type->node->interfaces[0];
    type->vtable = vtable;
    type->objects = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);

    return 0;
}

static void problems2_object_type_destroy(struct problems2_object_type *type)
{
    if (type->objects != NULL)
    {
        g_hash_table_destroy(type->objects);
        type->objects = NULL;
    }

    if (type->node != NULL)
    {
        g_dbus_node_info_unref(type->node);
        type->node = NULL;
    }
}

/*
 * User details
 */
struct user_info
{
    GList *sessions;
    long unsigned problems;
    unsigned new_problems;
    time_t new_problem_last;
};

static struct user_info *user_info_new(void)
{
    struct user_info *user = xzalloc(sizeof(*user));
    return user;
}

static void user_info_free(struct user_info *info)
{
    if (info == NULL)
        return;

    g_list_free(info->sessions);
    info->sessions = (void *)0xDAEDBEEF;

    free(info);
}

/*
 * AbrtP2Service GObject Type
 */
typedef struct
{
    GDBusConnection *p2srv_dbus;
    GDBusProxy      *p2srv_proxy_dbus;
    GHashTable      *p2srv_connected_users;
    PolkitAuthority *p2srv_pk_authority;

    struct problems2_object_type p2srv_p2_type;
    struct problems2_object_type p2srv_p2_entry_type;
    struct problems2_object_type p2srv_p2_session_type;

    struct abrt_p2_object *p2srv_p2_object;
} AbrtP2ServicePrivate;

static void abrt_p2_service_private_destroy(AbrtP2ServicePrivate *pv)
{
    if (pv->p2srv_connected_users != NULL)
    {
        g_hash_table_destroy(pv->p2srv_connected_users);
        pv->p2srv_connected_users = NULL;
    }

    problems2_object_type_destroy(&(pv->p2srv_p2_type));
    problems2_object_type_destroy(&(pv->p2srv_p2_session_type));
    problems2_object_type_destroy(&(pv->p2srv_p2_entry_type));

    if (pv->p2srv_proxy_dbus != NULL)
    {
        g_object_unref(pv->p2srv_proxy_dbus);
        pv->p2srv_proxy_dbus = NULL;
    }

    if (pv->p2srv_pk_authority != NULL)
    {
        g_object_unref(pv->p2srv_pk_authority);
        pv->p2srv_pk_authority = NULL;
    }
}

static int abrt_p2_service_private_init(AbrtP2ServicePrivate *pv, GError **unused)
{
    int r = 0;
    r = problems2_object_type_init(&(pv->p2srv_p2_type),
            g_org_freedesktop_Problems2_xml, abrt_p2_vtable());
    if (r != 0)
    {
        log_notice("Failed to initialize org.freedesktop.Problems2 type");
        goto error_return;
    }

    r = problems2_object_type_init(&(pv->p2srv_p2_session_type),
            g_org_freedesktop_Problems2_Session_xml, abrt_p2_session_vtable());
    if (r != 0)
    {
        log_notice("Failed to initialize org.freedesktop.Problems2.Session type");
        goto error_return;
    }

    r = problems2_object_type_init(&(pv->p2srv_p2_entry_type),
            g_org_freedesktop_Problems2_Entry_xml, abrt_p2_entry_vtable());
    if (r != 0)
    {
        log_notice("Failed to initialize org.freedesktop.Problems2.Entry type");
        goto error_return;
    }

    pv->p2srv_connected_users = g_hash_table_new_full(g_direct_hash,
                                                      g_direct_equal,
                                                      NULL,
                                                      (GDestroyNotify)user_info_free);

    if (g_polkit_authority != NULL)
    {
        g_object_ref(g_polkit_authority);
        pv->p2srv_pk_authority = g_polkit_authority;
        return 0;
    }

    GError *local_error = NULL;
    pv->p2srv_pk_authority = polkit_authority_get_sync(NULL, &local_error);
    if (pv->p2srv_pk_authority == NULL)
    {
        r = -1;
        log_notice("Failed to get PolkitAuthority: %s", local_error->message);
        g_error_free(local_error);
        goto error_return;
    }

    return 0;

error_return:
    abrt_p2_service_private_destroy(pv);
    return r;
}

struct _AbrtP2Service
{
    GObject parent_instance;
    AbrtP2ServicePrivate *pv;
};

G_DEFINE_TYPE_WITH_PRIVATE(AbrtP2Service, abrt_p2_service, G_TYPE_OBJECT)

static void abrt_p2_service_finalize(GObject *gobject)
{
    AbrtP2ServicePrivate *pv = abrt_p2_service_get_instance_private(ABRT_P2_SERVICE(gobject));
    abrt_p2_service_private_destroy(pv);
}

static void abrt_p2_service_class_init(AbrtP2ServiceClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = abrt_p2_service_finalize;
}

static void abrt_p2_service_init(AbrtP2Service *self)
{
    self->pv = abrt_p2_service_get_instance_private(self);
}

AbrtP2Service *abrt_p2_service_new(GError **error)
{
    AbrtP2Service *service = g_object_new(TYPE_ABRT_P2_SERVICE, NULL);

    if (abrt_p2_service_private_init(service->pv, error) != 0)
    {
        g_object_unref(service);
        return NULL;
    }

    return service;
}

static struct user_info *abrt_p2_service_user_lookup(AbrtP2Service *service, uid_t uid)
{
    return  g_hash_table_lookup(service->pv->p2srv_connected_users,
                                (gconstpointer)(gint64)uid);
}

static struct user_info *abrt_p2_service_user_insert(AbrtP2Service *service, uid_t uid, struct user_info *user)
{
    g_hash_table_insert(service->pv->p2srv_connected_users,
                               (gpointer)(gint64)uid, user);
    return user;
}

static struct user_info *abrt_p2_service_user_new(AbrtP2Service *service, uid_t uid)
{
    struct user_info *user = user_info_new();
    return abrt_p2_service_user_insert(service, uid, user);
}

static GDBusConnection *abrt_p2_service_dbus(AbrtP2Service *service)
{
    return service->pv->p2srv_dbus;
}

/*
 * DBus object
 */
struct abrt_p2_object
{
    AbrtP2Service *service;
    struct problems2_object_type *type;
    char *path;
    guint regid;
    void *node;
    void (*destructor)(struct abrt_p2_object *);
};

void abrt_p2_object_free(struct abrt_p2_object *obj)
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

    obj->service = NULL;

    free(obj);
}

AbrtP2Service *abrt_p2_object_service(struct abrt_p2_object *object)
{
    return object->service;
}

void *abrt_p2_object_get_node(struct abrt_p2_object *object)
{
    return object->node;
}

void abrt_p2_object_destroy(struct abrt_p2_object *object)
{
    log_debug("Unregistering object: %s", object->path);
    g_dbus_connection_unregister_object(abrt_p2_service_dbus(object->service), object->regid);
}

void abrt_p2_object_emit_signal(struct abrt_p2_object *object,
        const char *member,
        GVariant *parameters)
{
    GDBusMessage *message = g_dbus_message_new_signal(object->path, object->type->iface->name, member);
    g_dbus_message_set_sender(message, ABRT_P2_BUS);
    g_dbus_message_set_body(message, parameters);

    if (g_verbose > 2)
    {
        gchar *pstr = g_variant_print(parameters, TRUE);
        log_debug("Emitting signal '%s' : (%s)", member, pstr);
        g_free(pstr);
    }

    GError *error = NULL;
    g_dbus_connection_send_message(abrt_p2_service_dbus(object->service), message, G_DBUS_SEND_MESSAGE_FLAGS_NONE, NULL, &error);
    g_object_unref(message);
    if (error != NULL)
    {
        error_msg("Failed to emit signal '%s': %s", member, error->message);
        g_free(error);
    }
}

struct abrt_p2_object *abrt_p2_object_new(AbrtP2Service *service,
        struct problems2_object_type *type,
        char *path,
        void *node,
        void (*destructor)(struct abrt_p2_object *),
        GError **error)
{
    struct abrt_p2_object *obj = NULL;
    obj = xzalloc(sizeof(*obj));
    obj->path = path;
    obj->node = node;
    obj->destructor = destructor;
    obj->type = type;
    obj->service = service;

    /* Register the interface parsed from a XML file */
    log_debug("Registering PATH %s iface %s", path, type->iface->name);
    guint registration_id = g_dbus_connection_register_object(abrt_p2_service_dbus(service),
            path,
            type->iface,
            type->vtable,
            obj,
            (GDestroyNotify)abrt_p2_object_free,
            error);

    if (registration_id == 0)
    {
        g_prefix_error(error, "Failed to register path:'%s', interface: %s",
                path, type->iface->name);

        abrt_p2_object_free(obj);

        return NULL;
    }

    log_debug("Registered object: %d", registration_id);

    obj->regid = registration_id;

    g_hash_table_insert(obj->type->objects, path, obj);

    return obj;
}

/*
 * /org/freedesktop/Problems2/Session/XYZ
 */
static struct problems2_object_type g_problems2_session_type;

static void session_object_destructor(struct abrt_p2_object *obj)
{
    AbrtP2Session *session = (AbrtP2Session *)obj->node;

    uid_t uid = abrt_p2_session_uid(session);

    struct user_info *user = abrt_p2_service_user_lookup(obj->service, uid);

    if (user->sessions == NULL)
    {
        error_msg("BUG: destructing session object for user who does not have session opened");
        abort();
    }

    user->sessions = g_list_remove(user->sessions, session);
    g_object_unref(session);
}

void session_object_on_authorization_changed(AbrtP2Session *session, gint32 status, gpointer object)
{
    GVariant *params = g_variant_new("(i)", status);
    abrt_p2_object_emit_signal(object, "AuthorizationChanged", params);
}

static struct abrt_p2_object *session_object_register(AbrtP2Service *service,
        char *path,
        const char *caller,
        uid_t caller_uid,
        GError **error)
{
    struct user_info *user = abrt_p2_service_user_lookup(service, caller_uid);

    if (user != NULL && g_list_length(user->sessions) >= abrt_p2_service_user_clients_limit(service, caller_uid))
    {
        log_warning("User %lu reached the limit of opened sessions (%d)",
                    (long unsigned)caller_uid,
                    abrt_p2_service_user_clients_limit(service, caller_uid));

        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                    "Too many sessions opened");

        free(path);
        return NULL;
    }

    char *dup_caller = xstrdup(caller);

    AbrtP2Session *session = abrt_p2_session_new(dup_caller, caller_uid);

    struct abrt_p2_object *obj = abrt_p2_object_new(service,
                                  &g_problems2_session_type,
                                  path,
                                  session,
                                  session_object_destructor,
                                  error);

    if (obj == NULL)
    {
        g_prefix_error(error, "Failed to register Session object for caller '%s'", caller);
        return NULL;
    }

    g_signal_connect(session, "authorization-changed", G_CALLBACK(session_object_on_authorization_changed), obj);

    if (user == NULL)
        user = abrt_p2_service_user_new(service, caller_uid);

    user->sessions = g_list_prepend(user->sessions, session);

    return obj;
}

static char *caller_to_session_path(const char *caller)
{
    char hash_str[SHA1_RESULT_LEN*2 + 1];
    str_to_sha1str(hash_str, caller);
    return xasprintf(ABRT_P2_PATH"/Session/%s", hash_str);
}

static struct abrt_p2_object *abrt_p2_service_get_session_for_caller(
        AbrtP2Service *service,
        const char *caller,
        uid_t caller_uid,
        GError **error)
{
    char *session_path = caller_to_session_path(caller);

    struct abrt_p2_object *obj = g_hash_table_lookup(service->pv->p2srv_p2_session_type.objects, session_path);
    if (obj == NULL)
    {
        log_debug("Caller does not have Session: %s", caller);
        return session_object_register(service, session_path, caller, caller_uid, error);
    }

    free(session_path);

    AbrtP2Session *session = abrt_p2_object_get_node(obj);
    if (abrt_p2_session_check_sanity(session, caller, caller_uid, error) != 0)
    {
        log_debug("Cannot return session because the existing one did not pass sanity check.");
        return NULL;
    }

    return obj;
}

const char *abrt_p2_service_session_path(AbrtP2Service *service, const char *caller, GError **error)
{
    uid_t caller_uid = abrt_p2_service_caller_real_uid(service, caller, error);
    if (caller_uid == (uid_t)-1)
        return NULL;

    struct abrt_p2_object *obj = abrt_p2_service_get_session_for_caller(service,
                                                                        caller,
                                                                        caller_uid,
                                                                        error);

    return obj == NULL ? NULL : obj->path;
}

uid_t abrt_p2_service_caller_uid(AbrtP2Service *service, const char *caller, GError **error)
{
    uid_t caller_uid = abrt_p2_service_caller_real_uid(service, caller, error);
    if (caller_uid == (uid_t)-1)
        return (uid_t)-1;

    struct abrt_p2_object *obj = abrt_p2_service_get_session_for_caller(service, caller, caller_uid, error);
    if (obj == NULL)
        return (uid_t) -1;

    AbrtP2Session *session = abrt_p2_object_get_node(obj);
    if (abrt_p2_session_is_authorized(session))
        return 0;

    return caller_uid;
}

/*
 * Utility functions
 */
PolkitAuthority *abrt_p2_polkit_authority(void)
{
    return g_polkit_authority;
}

uid_t abrt_p2_service_caller_real_uid(AbrtP2Service *service, const char *caller, GError **error)
{
    guint caller_uid;

    if (service->pv->p2srv_proxy_dbus == NULL)
        return (uid_t) -1;

    GVariant *result = g_dbus_proxy_call_sync(service->pv->p2srv_proxy_dbus,
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

/*
 * /org/freedesktop/Problems2/Entry/XYZ
 */
static struct problems2_object_type g_problems2_entry_type;

static void entry_object_destructor(struct abrt_p2_object *obj)
{
    AbrtP2Entry *entry = (AbrtP2Entry *)obj->node;
    g_object_unref(entry);
}

static const char *register_dump_dir_entry_node(AbrtP2Service *service,
            const char *dd_dirname, GError **error)
{
    char hash_str[SHA1_RESULT_LEN*2 + 1];
    str_to_sha1str(hash_str, dd_dirname);
    char *path = xasprintf(ABRT_P2_PATH"/Entry/%s", hash_str);

    char *const dup_dirname = xstrdup(dd_dirname);
    AbrtP2Entry *entry = abrt_p2_entry_new(dup_dirname);

    struct abrt_p2_object *obj = abrt_p2_object_new(service,
                                  &g_problems2_entry_type,
                                  path,
                                  entry,
                                  entry_object_destructor,
                                  error);

    if (obj == NULL)
    {
        g_prefix_error(error, "Failed to register Entry object for directory '%s'", dd_dirname);
        return NULL;
    }

    struct dump_dir *dd = dd_opendir(dd_dirname, DD_OPEN_FD_ONLY);
    uid_t owner = dd_get_owner(dd);
    dd_close(dd);

    struct user_info *user = abrt_p2_service_user_lookup(service, owner);

    if (user == NULL)
        user = abrt_p2_service_user_insert(service, owner, user);

    if (user->problems == ULONG_MAX)
    {
        /* Give up, we cannot recover from this. */
        error_msg_and_die("Too many problems owned by a single user: uid=%lu", (long unsigned)owner);
    }

    user->problems++;

    return path;
}

struct save_problem_args
{
    AbrtP2EntrySaveElementsLimits limits;
    GVariant *problem_info;
    GUnixFDList *fd_list;
    uid_t caller_uid;
    GError **error;
};

static int wrapped_abrt_p2_entry_save_elements(struct dump_dir *dd,
        struct save_problem_args *args)
{
    return abrt_p2_entry_save_elements(dd,
                                       ABRT_P2_ENTRY_ALL_FATAL,
                                       args->problem_info,
                                       args->fd_list,
                                       args->caller_uid,
                                       &(args->limits),
                                       args->error);
}

const char *abrt_p2_service_save_problem(
        AbrtP2Service *service,
        const char *type_str,
        GVariant *problem_info, GUnixFDList *fd_list,
        uid_t caller_uid, char **problem_id, GError **error)
{
    struct save_problem_args args = {
        .problem_info = problem_info,
        .fd_list = fd_list,
        .caller_uid = caller_uid,
        .error = error,
    };

    args.limits.elements_count = abrt_p2_service_elements_limit(service, caller_uid);
    args.limits.data_size      = abrt_p2_service_data_size_limit(service, caller_uid);

    struct dump_dir *dd = create_dump_dir(g_settings_dump_location,
                                          type_str,
                                          /*fs owner*/0,
                                          (save_data_call_back)wrapped_abrt_p2_entry_save_elements,
                                          (void *)&args);

    if (dd == NULL)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_IO_ERROR,
                "Failed to create new problem directory");
        return NULL;
    }

    const char *entry_node_path = register_dump_dir_entry_node(service,
                                                               dd->dd_dirname,
                                                               error);

    if (entry_node_path != NULL)
    {
        if (problem_id != NULL)
            *problem_id = xstrdup(dd->dd_dirname);

        uid_t uid = dd_get_owner(dd);
        GVariant *parameters = g_variant_new("(oi)", entry_node_path, (gint32)uid);
        abrt_p2_object_emit_signal(service->pv->p2srv_p2_object, "Crash", parameters);
    }

    dd_close(dd);

    return entry_node_path;
}

int abrt_p2_service_remove_problem(AbrtP2Service *service,
            const char *entry_path, uid_t caller_uid, GError **error)
{
    struct abrt_p2_object *obj = g_hash_table_lookup(service->pv->p2srv_p2_entry_type.objects, entry_path);
    if (obj == NULL)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_BAD_ADDRESS,
                "Requested Entry does not exist");
        return -ENOENT;
    }

    const int ret = abrt_p2_entry_remove(ABRT_P2_ENTRY(obj->node), caller_uid, error);
    if (ret != 0)
        return ret;

    abrt_p2_object_destroy(obj);
    return 0;
}

problem_data_t *abrt_p2_service_entry_problem_data(AbrtP2Service *service,
            const char *entry_path, uid_t caller_uid, GError **error)
{
    struct abrt_p2_object *obj = g_hash_table_lookup(service->pv->p2srv_p2_entry_type.objects, entry_path);
    if (obj == NULL)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_BAD_ADDRESS,
                "Requested Entry does not exist");
        return NULL;
    }

    return abrt_p2_entry_problem_data(ABRT_P2_ENTRY(obj->node), caller_uid, error);
}

GList *abrt_p2_service_get_problems_nodes(AbrtP2Service *service, uid_t uid)
{
    GList *paths = NULL;

    GHashTableIter iter;
    g_hash_table_iter_init(&iter, service->pv->p2srv_p2_entry_type.objects);

    const char *p;
    struct abrt_p2_object *obj;
    while(g_hash_table_iter_next(&iter, (gpointer)&p, (gpointer)&obj))
    {
        if (0 == abrt_p2_entry_accessible_by_uid(ABRT_P2_ENTRY(obj->node), uid, NULL))
            paths = g_list_prepend(paths, (gpointer)p);
    }

    return paths;
}

/*
 * Service functions + /org/freedesktop/Problems2
 */
static struct problems2_object_type g_problems2_type;

struct bridge_call_args
{
    AbrtP2Service *service;
    GError **error;
};

static int bridge_register_dump_dir_entry_node(struct dump_dir *dd, void *call_args)
{
    struct bridge_call_args *args = call_args;
    return NULL == register_dump_dir_entry_node(args->service, dd->dd_dirname, args->error);
}

static void on_g_signal(GDBusProxy *proxy,
        gchar      *sender_name,
        gchar      *signal_name,
        GVariant   *parameters,
        gpointer    user_data)
{
    if (0 != strcmp(signal_name, "NameOwnerChanged"))
        return;

    const gchar *bus_name = NULL;
    const gchar *old_owner = NULL;
    const gchar *new_owner = NULL;

    g_variant_get(parameters, "(&s&s&s)", &bus_name, &old_owner, &new_owner);

    if (bus_name[0] == '\0' || old_owner[0] == '\0' || new_owner[0] != '\0')
        return;

    GHashTableIter iter;
    g_hash_table_iter_init(&iter, g_problems2_session_type.objects);

    const char *p;
    struct abrt_p2_object *obj;
    while(g_hash_table_iter_next(&iter, (gpointer)&p, (gpointer)&obj))
    {
        AbrtP2Session *session = obj->node;
        if (strcmp(bus_name, abrt_p2_session_caller(session)) != 0)
            continue;

        log_debug("Caller '%s' disconnected without closing session: %s", bus_name, p);

        abrt_p2_session_close(session);
        abrt_p2_object_destroy(obj);
    }
}

int abrt_p2_service_register_objects(AbrtP2Service *service, GDBusConnection *connection, GError **error)
{
    if (service->pv->p2srv_dbus != NULL)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                "Problems2 service objects are already registered");
        return -EALREADY;
    }

    service->pv->p2srv_dbus = connection;

    service->pv->p2srv_p2_object = abrt_p2_object_new(service,
                                  &g_problems2_type,
                                  (char *)ABRT_P2_PATH,
                                  /*node*/NULL,
                                  /*node destructor*/NULL,
                                  error);

    if (service->pv->p2srv_p2_object == 0)
    {
        g_prefix_error(error, "Failed to register Problems2 node");
        return -1;
    }

    struct bridge_call_args args;
    args.service = service;
    args.error = error;

    for_each_problem_in_dir(g_settings_dump_location, (uid_t)-1, bridge_register_dump_dir_entry_node, &args);

    if (*args.error != NULL)
    {
        g_prefix_error(error, "Failed to register Problems objects");
        return -1;
    }

    GError *local_error = NULL;
    service->pv->p2srv_proxy_dbus = g_dbus_proxy_new_sync(connection,
                                     G_DBUS_PROXY_FLAGS_NONE,
                                     NULL,
                                     "org.freedesktop.DBus",
                                     "/org/freedesktop/DBus",
                                     "org.freedesktop.DBus",
                                     NULL,
                                     &local_error);


    if (local_error == NULL)
        g_signal_connect(service->pv->p2srv_proxy_dbus, "g-signal", G_CALLBACK(on_g_signal), NULL);
    else
    {
        error_msg("Failed to initialize proxy to DBus: %s", local_error->message);
        g_error_free(local_error);
    }

    return 0;
}

/*
 * Service configuration
 */
unsigned abrt_p2_service_user_clients_limit(AbrtP2Service *service, uid_t uid)
{
    return 5;
}

unsigned abrt_p2_service_elements_limit(AbrtP2Service *service, uid_t uid)
{
    return uid == 0 ? 0 : 100;
}

off_t abrt_p2_service_data_size_limit(AbrtP2Service *service, uid_t uid)
{
    return uid == 0 ? 0 : 2L*1024L*1024L*1024L;
}

unsigned abrt_p2_service_user_problems_limit(AbrtP2Service *service, uid_t uid)
{
    return uid == 0 ? 0 : 1000;
}

unsigned abrt_p2_service_new_problem_throtling_magnitude(AbrtP2Service *service, uid_t uid)
{
    return 4;
}

unsigned abrt_p2_service_new_problems_batch(AbrtP2Service *service, uid_t uid)
{
    return 10;
}

int abrt_p2_service_user_can_create_new_problem(AbrtP2Service *service, uid_t uid)
{
    if (uid == 0)
        return 1;

    time_t current = time(NULL);
    if (current == (time_t)-1)
    {
        perror_msg("time");
        return -1;
    }

    struct user_info *user = abrt_p2_service_user_lookup(service, uid);
    if (user == NULL)
    {
        error_msg("User does not have Session: uid=%lu", (long unsigned)uid);
        return -1;
    }

    const unsigned upl = abrt_p2_service_user_problems_limit(service, uid);
    if (upl != 0 && user->problems >= upl)
        return -E2BIG;

    if (current < user->new_problem_last)
    {
        error_msg("The last problem was created in future: uid=%lu", (long unsigned)uid);
        return -1;
    }

    /* Allows Y new problems to be created in a batch but then allow only 1 new
     * problem per Xs.
     *
     *  number of problems = minimum( ((last ts - current ts) / (2^magnitude)),
     *                                (configured number))
     */
    const long unsigned off = current - user->new_problem_last;
    const long unsigned incr = (off >> abrt_p2_service_new_problem_throtling_magnitude(service, uid));

    const unsigned npb = abrt_p2_service_new_problems_batch(service, uid);
    /* Avoid overflow. Beware of adding operation inside the condition! */
    if (   incr > npb
        || (user->new_problems += incr) > npb)
        user->new_problems = npb;

    log_debug("NewProblem limit: last %lu, current %lu, increment %lu, remaining %u",
            (long unsigned)user->new_problem_last, (long unsigned)current, incr, user->new_problems);

    if (user->new_problems == 0)
        return 0;

    user->new_problem_last = current;
    return user->new_problems--;
}
