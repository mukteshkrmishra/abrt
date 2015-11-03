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
#include <gio/gunixfdlist.h>

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

    const int ret = dd_accessible_by_uid(tmp, uid) ? 0 : -EACCES;

    if (ret == 0 && dd != NULL)
        *dd = tmp;
    else
        dd_close(tmp);

    return ret;
}

int abrt_problems2_entry_node_remove(struct p2e_node *entry, uid_t caller_uid, GError **error)
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
                          struct abrt_problems2_object *object,
                          uid_t caller_uid,
                          const gchar *object_path,
                          struct dump_dir **dd,
                          GError **error)
{
    struct p2e_node *node = abrt_problems2_object_get_node(object);
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

static GVariant *handle_ReadElements(struct dump_dir *dd, gint flags,
                                     GVariant *elements, GUnixFDList *fd_list)
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

    gchar *name = NULL;
    GVariantIter iter;
    g_variant_iter_init(&iter, elements);
    /* No need to free 'name' unless breaking out of the loop */
    while (g_variant_iter_loop(&iter, "s", &name))
    {
        log_debug("Reading element: %s", name);
        if (!str_is_correct_filename(name))
        {
            error_msg("Attempt to read prohibited data: '%s'", name);
            continue;
        }

        int elem_type = 0;
        char *data = NULL;
        int fd = -1;
        int r = problem_data_load_dump_dir_element(dd, name, &data, &elem_type, &fd);
        if (r < 0)
        {
            if (r == -ENOENT)
                log_debug("Element does not exist: %s", name);
            else
                error_msg("Failed to open %s: %s", name, strerror(-r));
            continue;
        }

        if (   ((flags & 0x04) && !(elem_type & CD_FLAG_TXT))
            || ((flags & 0x08) && !(elem_type & CD_FLAG_BIGTXT))
            || ((flags & 0x10) && !(elem_type & CD_FLAG_BIN))
           )
        {
            log_debug("Element is not of the requested type: %s", name);
            free(data);
            close(fd);
            continue;
        }

        if (   (flags & 0x1)
            || (elem_type & CD_FLAG_BIGTXT)
            || (elem_type & CD_FLAG_BIN))
        {
            free(data);
            if (lseek(fd, 0, SEEK_SET))
            {
                perror_msg("Failed to rewind file descriptor of %s", name);
                close(fd);
                continue;
            }

            GError *error = NULL;
            gint pos = g_unix_fd_list_append(fd_list, fd, &error);
            close(fd);
            if (error != NULL)
            {
                error_msg("Failed to add file descriptor of %s: %s", name, error->message);
                g_error_free(error);
                continue;
            }

            log_debug("Adding new Unix FD at position: %d",  pos);
            g_variant_builder_add(&builder, "{sv}", name, g_variant_new("h", pos));
            continue;
        }

        log_debug("Adding element text data");
        g_variant_builder_add(&builder, "{sv}", name, g_variant_new_string(data));
        free(data);
        close(fd);
    }

    GVariant *retval_body[1];
    retval_body[0] = g_variant_builder_end(&builder);
    return  g_variant_new_tuple(retval_body, ARRAY_SIZE(retval_body));
}

int abrt_problems2_entry_save_elements(struct dump_dir *dd, int flags,
                                GVariant *elements, GUnixFDList *fd_list,
                                uid_t caller_uid, GError **error)
{
    int retval = 0;

    gchar *name = NULL;
    GVariant *value = NULL;
    GVariantIter iter;
    g_variant_iter_init(&iter, elements);

    /* Implement global size constraints */
    const int elements_limit = abrt_problems2_service_elements_limit(caller_uid);
    const off_t dd_size_limit = abrt_problems2_service_dd_size_limit(caller_uid);

    off_t dd_size = dd_compute_size(dd, /*no flags*/0);
    if (dd_size < 0)
    {
        error_msg("Failed to get file system size of dump dir : %s", strerror(-dd_size));
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_IO_ERROR,
                "Dump directory file system size");
        return dd_size;
    }

    int dd_items = dd_get_items_count(dd);
    if (dd_items < 0)
    {
        error_msg("Failed to get count of dump dir elements: %s", strerror(-dd_items));
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_IO_ERROR,
                "Dump directory elements count");
        return dd_items;
    }

    /* No need to free 'name' and 'container' unless breaking out of the loop */
    while (g_variant_iter_loop(&iter, "{sv}", &name, &value))
    {
        log_debug("Saving element: %s", name);
        if (!str_is_correct_filename(name))
        {
            error_msg("Attempt to save prohibited data: '%s'", name);
            g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_ACCESS_DENIED,
                    "Not allowed problem element name");
            retval = -EACCES;
            goto exit_loop_on_error;
        }

        off_t item_size = 0;
        if (!dd_exist(dd, name))
        {
            if (elements_limit != 0 && dd_items >= elements_limit)
            {
                error_msg("Cannot create new element '%s': reached the limit for elements %u", name, elements_limit);
                if (flags & P2E_ELEMENTS_COUNT_LIMIT_FATAL)
                    goto exit_loop_on_too_many_elements;
                continue;
            }

            ++dd_items;
        }
        else
        {
            item_size = dd_get_item_size(dd, name);
            if (item_size < 0)
            {
                error_msg("Failed to get size of element '%s'", name);
                if (flags & P2E_IO_ERROR_FATAL)
                {
                    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_IO_ERROR,
                            "Failed to get size of underlying data");
                    retval = item_size;
                    goto exit_loop_on_error;
                }
                continue;
            }
        }

        const off_t base_size = dd_size - item_size;

        if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
        {
            log_debug("Saving text element");
            const char *data = g_variant_get_string(value, /*length*/NULL);
            const off_t data_size = strlen(data);

            if (allowed_new_user_problem_entry(caller_uid, name, data) == false)
            {
                error_msg("Not allowed for user %lu: %s = %s", (long unsigned)caller_uid, name, data);
                g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                            "You are not allowed to create element '%s' containing '%s'",
                            name, data);
                retval = -EPERM;
                goto exit_loop_on_error;
            }

            /* Do not allow dump dir growing in case it already consumes
             * more than the limit */
            if (   dd_size_limit != 0
                && data_size > item_size
                && base_size + data_size > dd_size_limit)
            {
                error_msg("Cannot save text element: problem data size limit %ld", dd_size_limit);
                if (flags & P2E_DATA_SIZE_LIMIT_FATAL)
                    goto exit_loop_on_too_big_data;
                continue;
            }

            dd_save_text(dd, name, data);
            dd_size = base_size + data_size;
        }
        else if (g_variant_is_of_type(value, G_VARIANT_TYPE_HANDLE))
        {
            log_debug("Saving data from file descriptor");

            if (problem_entry_is_post_create_condition(name))
            {
                error_msg("post-create element as file descriptor: %s", name);
                g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                            "Element '%s' must be of '%s' D-Bus type",
                            name, g_variant_type_peek_string(G_VARIANT_TYPE_STRING));
                retval = -EINVAL;
                goto exit_loop_on_error;
            }

            gint32 handle = g_variant_get_handle(value);

            int fd = g_unix_fd_list_get(fd_list, handle, error);
            if (*error != NULL)
            {
                error_msg("Failed to get file descriptor of %s: %s", name, (*error)->message);
                if (flags & P2E_IO_ERROR_FATAL)
                {
                    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_IO_ERROR,
                            "Failed to get passed file descriptor");
                    retval = -EIO;
                    goto exit_loop_on_error;
                }
                continue;
            }

            /* Do not allow dump dir growing */
            const off_t max_size = base_size > dd_size_limit ? item_size : dd_size_limit - base_size;
            const off_t r = dd_copy_fd(dd, name, fd, /*copy_flags*/0, max_size);
            close(fd);

            if (r < 0)
            {
                error_msg("Failed to save file descriptor");
                if (flags & P2E_IO_ERROR_FATAL)
                {
                    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_IO_ERROR,
                            "Failed to save data of passed file descriptor");
                    retval = r;
                    goto exit_loop_on_error;
                }
                continue;
            }

            if (r >= max_size)
            {
                error_msg("File descriptor was truncated due to size limit");
                if (flags & P2E_DATA_SIZE_LIMIT_FATAL)
                    goto exit_loop_on_too_big_data;

                /* the file has been created and its size is 'max_size' */
                dd_size = base_size + max_size;
            }
            else
                dd_size = base_size + r ;
        }
        else
        {
            error_msg("Unsupported type: %s", g_variant_get_type_string(value));
            if (flags & P2E_IO_ERROR_FATAL)
            {
                g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED,
                        "Not supported D-Bus type");
                retval = -ENOTSUP;
                goto exit_loop_on_error;
            }
        }
    }

    return 0;

exit_loop_on_too_big_data:
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_LIMITS_EXCEEDED,
            "Problem data is too big");
    return -EFBIG;

exit_loop_on_too_many_elements:
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_LIMITS_EXCEEDED,
            "Too many elements");
    return -E2BIG;

exit_loop_on_error:
    return retval;
}

static void handle_DeleteElements(struct dump_dir *dd, GVariant *elements)
{
    gchar *name = NULL;
    GVariantIter iter;
    g_variant_iter_init(&iter, elements);

    /* No need to free 'name' unless breaking out of the loop */
    while (g_variant_iter_loop(&iter, "s", &name))
    {
        log_debug("Deleting element: %s", name);
        if (!str_is_correct_filename(name))
        {
            error_msg("Attempt to remove prohibited data: '%s'", name);
            continue;
        }

        dd_delete_item(dd, name);
    }
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
    uid_t caller_uid = abrt_problems2_service_caller_uid(connection, caller, &error);
    if (caller_uid == (uid_t)-1)
    {
        g_dbus_method_invocation_return_gerror(invocation, error);
        g_error_free(error);
        return;
    }

    struct dump_dir *dd;
    struct p2e_node *node = get_entry(connection, user_data, caller_uid, object_path, &dd, &error);
    if (node == NULL)
    {
        g_dbus_method_invocation_return_gerror(invocation, error);
        g_error_free(error);
        return;
    }

    if (strcmp(method_name, "GetSemanticElement") == 0)
    {
        return;
    }

    if (strcmp(method_name, "SetSemanticElement") == 0)
    {
        return;
    }

    if (strcmp(method_name, "ReadElements") == 0)
    {
        dd = dd_fdopendir(dd, DD_OPEN_READONLY | DD_DONT_WAIT_FOR_LOCK);
        if (dd == NULL)
        {
            g_dbus_method_invocation_return_error(invocation,
                            G_DBUS_ERROR, G_DBUS_ERROR_IO_ERROR,
                            "Failed to obtain the lock");
            return;
        }

        GVariant *elements = g_variant_get_child_value(parameters, 0);

        gint flags;
        g_variant_get_child(parameters, 1, "i", &flags);

        GUnixFDList *fd_list = g_unix_fd_list_new();

        GVariant *retval = handle_ReadElements(dd, flags, elements, fd_list);
        g_dbus_method_invocation_return_value_with_unix_fd_list(invocation, retval, fd_list);

        g_variant_unref(elements);
        g_object_unref(fd_list);
        dd_close(dd);
        return;
    }

    if (strcmp(method_name, "SaveElements") == 0)
    {
        dd = dd_fdopendir(dd, DD_OPEN_READONLY | DD_DONT_WAIT_FOR_LOCK);
        if (dd == NULL)
        {
            g_dbus_method_invocation_return_error(invocation,
                            G_DBUS_ERROR, G_DBUS_ERROR_IO_ERROR,
                            "Failed to obtain the lock");
            return;
        }

        GVariant *elements = g_variant_get_child_value(parameters, 0);

        gint flags;
        g_variant_get_child(parameters, 1, "i", &flags);

        GDBusMessage *msg = g_dbus_method_invocation_get_message(invocation);
        GUnixFDList *fd_list = g_dbus_message_get_unix_fd_list(msg);

        int r = abrt_problems2_entry_save_elements(dd, flags, elements, fd_list, caller_uid, &error);
        if (r != 0)
        {
            g_dbus_method_invocation_return_gerror(invocation, error);
            g_error_free(error);
        }
        else
            g_dbus_method_invocation_return_value(invocation, NULL);

        g_variant_unref(elements);
        dd_close(dd);
        return;
    }

    if (strcmp(method_name, "DeleteElements") == 0)
    {
        dd = dd_fdopendir(dd, DD_DONT_WAIT_FOR_LOCK);
        if (dd == NULL)
        {
            g_dbus_method_invocation_return_error(invocation,
                            G_DBUS_ERROR, G_DBUS_ERROR_IO_ERROR,
                            "Failed to obtain the lock");
            return;
        }

        GVariant *elements = g_variant_get_child_value(parameters, 0);

        handle_DeleteElements(dd, elements);
        g_dbus_method_invocation_return_value(invocation, NULL);

        g_variant_unref(elements);
        dd_close(dd);
        return;
    }

    dd_close(dd);
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

    uid_t caller_uid = abrt_problems2_service_caller_uid(connection, caller, error);
    if (caller_uid == (uid_t)-1)
        return NULL;

    GVariant *retval;
    struct dump_dir *dd;
    struct p2e_node *node = get_entry(connection, user_data, caller_uid, object_path, &dd, error);
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
        const char *const elements[] = { FILENAME_PACKAGE, FILENAME_PKG_EPOCH, FILENAME_PKG_NAME,  FILENAME_PKG_VERSION, FILENAME_PKG_RELEASE };

        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("(sssss)"));
        for (size_t i = 0; i < ARRAY_SIZE(elements); ++i)
        {
            char *data = dd_load_text(dd, elements[i]);
            g_variant_builder_add(&builder, "s", data);
            free(data);
        }

        retval = g_variant_builder_end(&builder);
        goto return_property_value;
    }

    if (strcmp("reports", property_name) == 0)
    {
        GVariantBuilder top_builder;
        g_variant_builder_init(&top_builder, G_VARIANT_TYPE("a(sa{sv})"));

        GList *reports = read_entire_reported_to(dd);
        for (GList *iter = reports; iter != NULL; iter = g_list_next(iter))
        {
            GVariantBuilder value_builder;
            g_variant_builder_init(&value_builder, G_VARIANT_TYPE("a{sv}"));

            struct report_result *r = (struct report_result *)iter->data;

            if (r->url != NULL)
            {
                GVariant *data = g_variant_new_variant(g_variant_new_string(r->url));
                g_variant_builder_add(&value_builder, "{sv}", "URL", data);
            }
            if (r->msg != NULL)
            {
                GVariant *data = g_variant_new_variant(g_variant_new_string(r->msg));
                g_variant_builder_add(&value_builder, "{sv}", "MSG", data);
            }
            if (r->bthash != NULL)
            {
                GVariant *data = g_variant_new_variant(g_variant_new_string(r->bthash));
                g_variant_builder_add(&value_builder, "{sv}", "BTHASH", data);
            }

            GVariant *children[2];
            children[0] = g_variant_new_string(r->label);
            children[1] = g_variant_builder_end(&value_builder);
            GVariant *entry = g_variant_new_tuple(children, 2);

            g_variant_builder_add_value(&top_builder, entry);
        }

        g_list_free_full(reports, (GDestroyNotify)free_report_result);

        retval = g_variant_builder_end(&top_builder);

        goto return_property_value;
    }

    if (strcmp("solutions", property_name) == 0)
    {
       return NULL;
    }

    if (strcmp("elements", property_name) == 0)
    {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE("as"));
        dd_init_next_file(dd);
        char *short_name;
        while (dd_get_next_file(dd, &short_name, NULL))
        {
            g_variant_builder_add(&builder, "s", short_name);
            free(short_name);
        }
        retval = g_variant_builder_end(&builder);
        goto return_property_value;
    }

    if (strcmp("semantic_elements", property_name) == 0)
    {
       return NULL;
    }

    if (strcmp("is_reported", property_name) == 0)
    {
       retval = g_variant_new_boolean(dd_exist(dd, FILENAME_REPORTED_TO));
       goto return_property_value;
    }

    if (strcmp("can_be_reported", property_name) == 0)
    {
       retval = g_variant_new_boolean(!dd_exist(dd, FILENAME_NOT_REPORTABLE));
       goto return_property_value;
    }

    if (strcmp("is_remote", property_name) == 0)
    {
       retval = g_variant_new_boolean(dd_exist(dd, FILENAME_REMOTE));
       goto return_property_value;
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

    uid_t caller_uid = abrt_problems2_service_caller_uid(connection, caller, error);
    if (caller_uid == (uid_t)-1)
        return FALSE;

    struct dump_dir *dd;
    struct p2e_node *node = get_entry(connection, user_data, caller, object_path, &dd, error);
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
