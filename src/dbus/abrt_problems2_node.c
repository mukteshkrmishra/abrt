#include "libabrt.h"
#include "abrt_glib.h"
#include "problem_api.h"

#include "abrt_problems2_node.h"
#include "abrt_problems2_service.h"
#include <gio/gunixfdlist.h>

#define STRINGIZE(literal) #literal

static const char *handle_new_problem(GDBusConnection *connection,
                                      GVariant *problem_info,
                                      uid_t caller_uid,
                                      GUnixFDList *fd_list,
                                      char **error)
{
    char *problem_id = NULL;
    const char *new_path = NULL;
    problem_data_t *pd = problem_data_new();

    GVariantIter *iter;
    g_variant_get(problem_info, "a{sv}", &iter);
    gchar *key;
    GVariant *value;
    while (g_variant_iter_loop(iter, "{sv}", &key, &value))
    {
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
        {
            log("New string: %s", key);
            const char *real_value = g_variant_get_string(value, /*ignore length*/NULL);
            if (allowed_new_user_problem_entry(caller_uid, key, real_value) == false)
            {
                *error = xasprintf("You are not allowed to create element '%s' containing '%s'", key, real_value);
                goto finito;
            }

            problem_data_add_text_editable(pd, key, real_value);
        }
        else if (g_variant_is_of_type(value, G_VARIANT_TYPE_HANDLE))
        {
            log("New file descriptor: %s", key);
            /* We need to make sure that the caller does not try to pass
             * prohibited element in form of a binary file.
             *
             * Get the first line and validate it. The firs line is enough.
             */
            char real_value[256] =  { 0 };
            GError *gerr = NULL;
            gint handle = g_unix_fd_list_get(fd_list, g_variant_get_handle(value), &gerr);
            if (gerr != NULL)
            {
                *error = xasprintf("Error in getting file descriptor '%s' : %s", key, gerr->message);
                g_error_free(gerr);
                goto finito;
            }

            if (handle < 0)
            {
                *error = xasprintf("Passed file descriptor must not be negative, '%s' : %d", key, handle);
                goto finito;
            }

            ssize_t count = safe_read(handle, real_value, sizeof(real_value) - 1);
            if (count <= 0)
            {
                *error = xasprintf("Cannot read passed file descriptor: '%s' : %d", key, handle);
                close(handle);
                goto finito;
            }

            char *new_line = strchrnul(real_value, '\n');
            *new_line = '\0';
            log("Got first line : %s", real_value);

            if (allowed_new_user_problem_entry(caller_uid, key, real_value) == false)
            {
                *error = xasprintf("You are not allowed to create element '%s' containing '%s'", key, real_value);
                close(handle);
                goto finito;
            }

            /* rewind() */
            //lseek(handle, 0, SEEK_SET);

            char fd_path[] = "/proc/self/fd/"STRINGIZE(INT_MAX);
            snprintf(fd_path, sizeof(fd_path), "/proc/self/fd/%d", handle);
            problem_data_add_file(pd, key, fd_path);
        }
    }

    if (caller_uid != 0 || problem_data_get_content_or_NULL(pd, FILENAME_UID) == NULL)
    {   /* set uid field to caller's uid if caller is not root or root doesn't pass own uid */
        log_info("Adding UID %d to problem data", caller_uid);
        char buf[sizeof(uid_t) * 3 + 2];
        snprintf(buf, sizeof(buf), "%d", caller_uid);
        problem_data_add_text_noteditable(pd, FILENAME_UID, buf);
    }

    /* At least it should generate local problem identifier UUID */
    problem_data_add_basics(pd);

    new_path = abrt_problems2_service_save_problem(connection, pd, &problem_id);

    if (problem_id)
        notify_new_path(problem_id);
    else if (error)
        *error = xasprintf("Cannot create a new problem");

finito:
    problem_data_free(pd);
    free(problem_id);
    return new_path;
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
    //abrt_p2_service *srv = (abrt_p2_srv *)user_data;

    /* Check sanity */
    if (strcmp(interface_name, "org.freedesktop.Problems2") != 0)
    {
        error_msg("Unsupported interface %s", interface_name);
        return;
    }

    uid_t caller_uid;
    GVariant *response;

    GError *error = NULL;
    caller_uid = abrt_problems2_service_caller_uid(connection, caller, &error);
    if (caller_uid == (uid_t) -1)
    {
        g_dbus_method_invocation_return_gerror(invocation, error);
        return;
    }

    if (strcmp("NewProblem", method_name) == 0)
    {
        char *err_msg = NULL;

        GDBusMessage *msg = g_dbus_method_invocation_get_message(invocation);
        GUnixFDList *fd_list = g_dbus_message_get_unix_fd_list(msg);

        const char *new_path = handle_new_problem(connection,
                                g_variant_get_child_value(parameters, 0),
                                caller_uid,
                                fd_list,
                                &err_msg);

        if (!new_path)
        {
            g_dbus_method_invocation_return_dbus_error(invocation,
                                                      "org.freedesktop.problems.Failure",
                                                      err_msg);
            free(err_msg);
            return;
        }
        /* else */
        response = g_variant_new("(o)", new_path);
        g_dbus_method_invocation_return_value(invocation, response);

        return;
    }
    else if (strcmp("GetSession", method_name) == 0)
    {
        GError *error = NULL;
        const char *session_path = abrt_problems2_get_session_path(connection, caller, &error);

        if (!session_path)
        {
            g_dbus_method_invocation_return_dbus_error(invocation,
                                                      "org.freedesktop.problems.Failure",
                                                      error->message);
            g_free(error);
            return;
        }

        response = g_variant_new("(o)", session_path);
        g_dbus_method_invocation_return_value(invocation, response);
        return;
    }
    else if (strcmp("GetProblems", method_name) == 0)
    {
        GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("ao"));

        GList *problem_nodes = abrt_problems2_service_get_problems_nodes(caller_uid);
        for (GList *p = problem_nodes; p != NULL; p = g_list_next(p))
            g_variant_builder_add(builder, "o", (char*)p->data);
        g_list_free(problem_nodes);

        response = g_variant_new("(ao)", builder);
        g_variant_builder_unref(builder);

        g_dbus_method_invocation_return_value(invocation, response);
        return;
    }
    else if (strcmp("GetProblemData", method_name) == 0)
    {
        /* Parameter tuple is (0) */
        const char *entry_path;

        g_variant_get(parameters, "(&o)", &entry_path);

        GError *error = NULL;
        problem_data_t *pd = abrt_problems2_service_entry_problem_data(entry_path, caller_uid, &error);
        if (NULL == pd)
        {
            g_dbus_method_invocation_return_gerror(invocation, error);
            g_error_free(error);
            return;
        }

        GVariantBuilder *response_builder = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);

        GHashTableIter pd_iter;
        char *element_name;
        struct problem_item *element_info;
        g_hash_table_iter_init(&pd_iter, pd);
        while (g_hash_table_iter_next(&pd_iter, (void**)&element_name, (void**)&element_info))
        {
            unsigned long size = 0;
            if (problem_item_get_size(element_info, &size) != 0)
            {
                log_notice("Can't get stat of : '%s'", element_info->content);
                continue;
            }

            log("%s: %d", element_name, element_info->flags);
            g_variant_builder_add(response_builder, "{s(its)}",
                                                    element_name,
                                                    element_info->flags,
                                                    size,
                                                    element_info->content);
        }

        GVariant *response = g_variant_new("(a{s(its)})", response_builder);
        g_variant_builder_unref(response_builder);

        problem_data_free(pd);

        g_dbus_method_invocation_return_value(invocation, response);
        return;

    }
    else if (strcmp("DeleteProblems", method_name) == 0)
    {
        GError *error = NULL;
        GVariant *array = g_variant_get_child_value(parameters, 0);

        GVariantIter *iter;
        gchar *entry_node;
        g_variant_get(array, "ao", &iter);
        while (g_variant_iter_loop(iter, "o", &entry_node))
        {
            if (abrt_problems2_service_remove_problem(connection, entry_node, caller_uid, &error) != 0)
            {
                g_dbus_method_invocation_return_gerror(invocation, error);
                g_error_free(error);
                return;
            }
        }

        g_dbus_method_invocation_return_value(invocation, NULL);
        return;
    }

    error_msg("BUG: org.freedesktop.Problems2 does not have method: %s", method_name);
    g_dbus_method_invocation_return_error(invocation, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_METHOD,
            "The method has to be implemented");
}

GDBusInterfaceVTable *abrt_problems2_node_vtable(void)
{
    static GDBusInterfaceVTable default_vtable =
    {
        .method_call = dbus_method_call,
        .get_property = NULL,
        .set_property = NULL,
    };

    return &default_vtable;
}

