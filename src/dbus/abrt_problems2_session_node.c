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

#include "abrt_problems2_session_node.h"
#include "abrt_problems2_service.h"

#include "libabrt.h"

#include <assert.h>

struct check_auth_cb_params
{
    struct abrt_problems2_object *obj;
    GDBusConnection *connection;
    GCancellable *cancellable;
};

struct p2s_node
{
    char   *p2s_caller;
    uid_t   p2s_uid;
    int     p2s_state;
    time_t  p2s_stamp;
    struct check_auth_cb_params *p2s_auth_rq;
};

enum
{
    P2S_STATE_INIT,
    P2S_STATE_PENDING,
    P2S_STATE_AUTH,
};

static void change_state(struct abrt_problems2_object *obj, int new_state, GDBusConnection *connection)
{
    struct p2s_node *node = abrt_problems2_object_get_node(obj);
    if (node->p2s_state == new_state)
        return;

    int value = -1;
    int old_state = node->p2s_state;
    node->p2s_state = new_state;

    if      (old_state == P2S_STATE_INIT    && new_state == P2S_STATE_PENDING)
    {
        log_debug("Authorization request is pending");
        value = 1;
    }
    else if (old_state == P2S_STATE_PENDING && new_state == P2S_STATE_AUTH)
    {
        log_debug("Authorization has been acquired");
        value = 0;
    }
    else if (old_state == P2S_STATE_AUTH    && new_state == P2S_STATE_INIT)
    {
        log_debug("Authorization request has been lost");
        value = 2;
    }
    else if (old_state == P2S_STATE_PENDING && new_state == P2S_STATE_INIT)
    {
        log_debug("Authorization request has failed");
        value = 3;
    }
    else
        goto forgotten_state;

    GVariant *parameters = g_variant_new("(i)", value);
    abrt_problems2_object_emit_signal(obj, "AuthorizationChanged", parameters, connection);
    return;

forgotten_state:
    error_msg("BUG: unsupported state, current : %d, new : %d", node->p2s_state, new_state);
}

void authorization_request_destroy(struct abrt_problems2_object *obj)
{
    struct p2s_node *node = abrt_problems2_object_get_node(obj);

    g_object_unref(node->p2s_auth_rq->cancellable);
    node->p2s_auth_rq->cancellable = (void *)0xDEADBEEF;

    free(node->p2s_auth_rq);
    node->p2s_auth_rq = NULL;
}

void check_authorization_callback(GObject *source, GAsyncResult *res, gpointer user_data)
{
    GError *error = NULL;
    PolkitAuthorizationResult *result = NULL;
    result = polkit_authority_check_authorization_finish(POLKIT_AUTHORITY(source), res, &error);

    int new_state = P2S_STATE_INIT;
    if (result == NULL)
    {
       error_msg("Polkit authorization failed: %s", error->message);
       g_error_free(error);
    }
    else if (polkit_authorization_result_get_is_authorized(result))
        new_state = P2S_STATE_AUTH;
    else
        log_debug("Not authorized");

    g_object_unref(result);

    struct check_auth_cb_params *params = (struct check_auth_cb_params *)user_data;
    change_state(params->obj, new_state, params->connection);

    /* Invalidates args/params !!! */
    authorization_request_destroy(params->obj);
}

void authorization_request_initialize(struct abrt_problems2_object *obj, GDBusConnection *connection)
{
    struct p2s_node *node = abrt_problems2_object_get_node(obj);

    struct check_auth_cb_params *auth_rq = xmalloc(sizeof(*auth_rq));
    auth_rq->obj = obj;
    auth_rq->connection = connection;
    auth_rq->cancellable = g_cancellable_new();
    node->p2s_auth_rq = auth_rq;
    change_state(obj, P2S_STATE_PENDING, connection);

    PolkitSubject *subject = polkit_system_bus_name_new(node->p2s_caller);
    polkit_authority_check_authorization(abrt_problems2_polkit_authority(),
                subject,
                "org.freedesktop.problems.getall",
                /* TODO: polkit.message */ NULL,
                POLKIT_CHECK_AUTHORIZATION_FLAGS_ALLOW_USER_INTERACTION,
                auth_rq->cancellable,
                check_authorization_callback,
                auth_rq);
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
    log_debug("Problems2.Sessions method : %s", method_name);

    /* Check sanity */
    if (strcmp(interface_name, ABRT_P2_NS_MEMBER("Session")) != 0)
    {
        error_msg("Unsupported interface %s", interface_name);
        return;
    }

    //GVariant *response;
    GError *error = NULL;

    uid_t caller_uid = abrt_problems2_service_caller_real_uid(connection, caller, &error);
    if (caller_uid == (uid_t)-1)
    {
        g_dbus_method_invocation_return_gerror(invocation, error);
        g_error_free(error);
        return;
    }

    struct p2s_node *node = abrt_problems2_object_get_node(user_data);
    if (abrt_problems2_session_check_sanity(node, caller, caller_uid, &error) != 0)
    {
        g_dbus_method_invocation_return_gerror(invocation, error);
        g_error_free(error);
        return;
    }

    if (strcmp("Authorize", method_name) == 0)
    {
        int retval = -1;

        switch(node->p2s_state)
        {
            case P2S_STATE_INIT:
                authorization_request_initialize(user_data, connection);
                retval = 1;
                break;

            case P2S_STATE_PENDING:
                retval = 2;
                break;

            case P2S_STATE_AUTH:
                retval = 0;
                break;
        }

        GVariant *response = g_variant_new("(i)", retval);
        g_dbus_method_invocation_return_value(invocation, response);
        return;
    }

    if (strcmp("Close", method_name) == 0)
    {
        switch(node->p2s_state)
        {
            case P2S_STATE_AUTH:
                change_state(user_data, P2S_STATE_INIT, connection);
                break;

            case P2S_STATE_PENDING:
                {
                    struct p2s_node *node = abrt_problems2_object_get_node(user_data);
                    g_cancellable_cancel(node->p2s_auth_rq->cancellable);

                    authorization_request_destroy(user_data);

                    change_state(user_data, P2S_STATE_INIT, connection);
                }
                break;

            case P2S_STATE_INIT:
                /* pass */
                break;
        }

        g_dbus_method_invocation_return_value(invocation, NULL);

        abrt_problems2_object_destroy(user_data, connection);
        return;
    }

    error_msg("BUG: org.freedesktop.Problems2.Session does not have method: %s", method_name);
}

static GVariant *dbus_get_property(GDBusConnection *connection,
                        const gchar *caller,
                        const gchar *object_path,
                        const gchar *interface_name,
                        const gchar *property_name,
                        GError      **error,
                        gpointer    user_data)
{
    log_debug("Problems2.Sessions get property : %s", property_name);

    if (strcmp(interface_name, "org.freedesktop.Problems2.Session") != 0)
    {
        error_msg("Unsupported interface %s", interface_name);
        return NULL;
    }

    if (strcmp("is_authorized", property_name))
    {
        error_msg("Unsupported property %s", property_name);
        return NULL;
    }

    uid_t caller_uid = abrt_problems2_service_caller_real_uid(connection, caller, error);
    if (caller_uid == (uid_t)-1)
        return NULL;

    struct p2s_node *node = abrt_problems2_object_get_node(user_data);
    if (abrt_problems2_session_check_sanity(node, caller, caller_uid, error) != 0)
        return NULL;

    return g_variant_new_boolean(abrt_problems2_session_is_authorized(node));
}

GDBusInterfaceVTable *abrt_problems2_session_node_vtable(void)
{
    static GDBusInterfaceVTable default_vtable =
    {
        .method_call = dbus_method_call,
        .get_property = dbus_get_property,
        .set_property = NULL,
    };

    return &default_vtable;
}

/* Public interface */

struct p2s_node *abrt_problems2_session_node_new(char *caller, uid_t uid)
{
    struct p2s_node *node = xmalloc(sizeof(*node));
    node->p2s_caller = caller;
    node->p2s_uid = uid;

    if (node->p2s_uid == 0)
        node->p2s_state = P2S_STATE_AUTH;
    else
        node->p2s_state = P2S_STATE_INIT;

    node->p2s_stamp = time(NULL);

    return node;
}

void abrt_problems2_session_node_free(struct p2s_node *node)
{
    if (NULL == node)
        return;

    free(node->p2s_caller);
    node->p2s_caller = (void *)0xDEADBEEF;
}

uid_t abrt_problems2_session_uid(struct p2s_node *session)
{
    return session->p2s_uid;
}

int abrt_problems2_session_is_authorized(struct p2s_node *session)
{
    return session->p2s_state == P2S_STATE_AUTH;
}

int abrt_problems2_session_check_sanity(struct p2s_node *session,
            const char *caller,
            uid_t caller_uid,
            GError **error)
{
    if (strcmp(session->p2s_caller, caller) == 0 && session->p2s_uid == caller_uid)
        /* the session node is sane */
        return 0;

    log_warning("Problems2 Session object does not belong to UID %d", caller_uid);

    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
            "Your Problems2 Session is broken. Check system logs for more details.");
    return -1;
}
