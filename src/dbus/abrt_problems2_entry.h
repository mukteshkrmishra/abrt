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
#ifndef abrt_p2_entry_H
#define abrt_p2_entry_H

#include "libabrt.h"

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define TYPE_ABRT_P2_ENTRY abrt_p2_entry_get_type ()
G_DECLARE_FINAL_TYPE(AbrtP2Entry, abrt_p2_entry, ABRT_P2, ENTRY, GObject)

AbrtP2Entry *abrt_p2_entry_new(char *dirname);

int  abrt_p2_entry_delete(AbrtP2Entry *entry, uid_t caller_uid, GError **error);

int abrt_p2_entry_accessible_by_uid(AbrtP2Entry *entry, uid_t uid,
            struct dump_dir **dd);

struct dump_dir *abrt_p2_entry_open_dump_dir(AbrtP2Entry *entry,
             uid_t caller_uid, int dd_flags, GError **error);

GVariant *abrt_p2_entry_problem_data(AbrtP2Entry *entry, uid_t caller_uid,
            GError **error);

GVariant *abrt_p2_entry_delete_elements(AbrtP2Entry *entry, uid_t caller_uid,
            GVariant *elements, GError **error);

/*
 * Read elements
 */
enum AbrtP2EntryReadElementsFlags
{
    ABRT_P2_ENTRY_READ_ALL_FD             = 0x01,
    ABRT_P2_ENTRY_READ_ALL_TYPES          = 0x02,
    ABRT_P2_ENTRY_READ_ONLY_TEXT          = 0x04,
    ABRT_P2_ENTRY_READ_ONLY_BIG_TEXT      = 0x08,
    ABRT_P2_ENTRY_READ_ONLY_BINARY        = 0x10,
    ABRT_P2_ENTRY_READ_ALL_NO_FD          = 0x20,
};

GVariant *abrt_p2_entry_read_elements(AbrtP2Entry *entry, gint32 flags,
             GVariant *elements, GUnixFDList *fd_list, uid_t caller_uid,
             GError **error);

/*
 * Save elements
 */
enum AbrP2EntrySaveElementsFlags
{
    ABRT_P2_ENTRY_IO_ERROR_FATAL             = (1 << 0),
    ABRT_P2_ENTRY_UNSUPPORTED_ERROR_FATAL    = (1 << 1),
    ABRT_P2_ENTRY_ELEMENTS_COUNT_LIMIT_FATAL = (1 << 2),
    ABRT_P2_ENTRY_DATA_SIZE_LIMIT_FATAL      = (1 << 3),

    ABRT_P2_ENTRY_ALL_FATAL =(  ABRT_P2_ENTRY_IO_ERROR_FATAL
                    | ABRT_P2_ENTRY_UNSUPPORTED_ERROR_FATAL
                    | ABRT_P2_ENTRY_ELEMENTS_COUNT_LIMIT_FATAL
                    | ABRT_P2_ENTRY_DATA_SIZE_LIMIT_FATAL),
};

typedef struct
{
    unsigned elements_count;
    off_t    data_size;
} AbrtP2EntrySaveElementsLimits;

#define ABRT_P2_ENTRY_SAVE_ELEMENTS_LIMITS_INITIALIZER(l, ec, ds) \
        do  { (l).elements_count = (ec); (l).data_size = (ds); } while (0)

#define ABRT_P2_ENTRY_SAVE_ELEMENTS_LIMITS_ON_STACK(l, ec, ds) \
        AbrtP2EntrySaveElementsLimits l; \
        ABRT_P2_ENTRY_SAVE_ELEMENTS_LIMITS_INITIALIZER(l, ec, ds);


GVariant *abrt_p2_entry_save_elements(AbrtP2Entry *entry, gint32 flags,
            GVariant *elements, GUnixFDList *fd_list, uid_t caller_uid,
            AbrtP2EntrySaveElementsLimits *limits, GError **error);

/*
 * Utility functions
 */
int abrt_p2_entry_save_elements_in_dump_dir(struct dump_dir *dd, gint32 flags,
        GVariant *elements, GUnixFDList *fd_list, uid_t caller_uid,
        AbrtP2EntrySaveElementsLimits *limits, GError **error);

G_END_DECLS

#endif/*abrt_p2_entry_H*/
