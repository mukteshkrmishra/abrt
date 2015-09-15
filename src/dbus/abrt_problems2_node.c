#include "libabrt.h"
#include "abrt_problems2_node.h"
#include "abrt_problems2_service.h"
#include <gio/gunixfdlist.h>

#define STRINGIZE(literal) #literal

static char *handle_new_problem(GVariant *problem_info, uid_t caller_uid, GUnixFDList *fd_list, char **error)
{
    char *problem_id = NULL;
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

            ssize_t count = safe_read(handle, real_value, sizeof(real_value));
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

    problem_id = problem_data_save(pd);
    if (problem_id)
        notify_new_path(problem_id);
    else if (error)
        *error = xasprintf("Cannot create a new problem");

finito:
    problem_data_free(pd);
    return problem_id;
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

        char *problem_id = handle_new_problem(g_variant_get_child_value(parameters, 0), caller_uid, fd_list, &err_msg);

        if (!problem_id)
        {
            g_dbus_method_invocation_return_dbus_error(invocation,
                                                      "org.freedesktop.problems.Failure",
                                                      err_msg);
            free(err_msg);
            return;
        }
        /* else */
        response = g_variant_new("(s)", problem_id);
        g_dbus_method_invocation_return_value(invocation, response);
        free(problem_id);

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
    }
    else if (strcmp("GetProblems", method_name) == 0)
    {
    }
    else if (strcmp("GetProblemData", method_name) == 0)
    {
    }
    else if (strcmp("DeleteProblmes", method_name) == 0)
    {
    }
    else
    {
    }
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

