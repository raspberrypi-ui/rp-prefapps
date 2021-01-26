/*
Copyright (c) 2018 Raspberry Pi (Trading) Ltd.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define _GNU_SOURCE
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#define I_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
#include <packagekit-glib2/packagekit.h>

#include <X11/Xlib.h>

#include <libintl.h>

/* Columns in packages and categories list stores */

#define PACK_ICON           0
#define PACK_CELL_TEXT      1
#define PACK_INSTALLED      2
#define PACK_CATEGORY       3
#define PACK_PACKAGE_NAME   4
#define PACK_PACKAGE_ID     5
#define PACK_CELL_NAME      6
#define PACK_CELL_DESC      7
#define PACK_SIZE           8
#define PACK_DESCRIPTION    9
#define PACK_SUMMARY        10
#define PACK_RPACKAGE_NAME  11
#define PACK_RPACKAGE_ID    12
#define PACK_INIT_INST      13
#define PACK_ADD_NAMES      14
#define PACK_ADD_IDS        15
#define PACK_REBOOT         16
#define PACK_ARCH           17
#define PACK_RPDESC         18

#define CAT_ICON            0
#define CAT_NAME            1
#define CAT_DISP_NAME       2

/* Controls */

static GtkWidget *main_dlg, *cat_tv, *pack_tv, *close_btn, *apply_btn, *search_te;
static GtkWidget *msg_dlg, *msg_msg, *msg_pb, *msg_btn, *msg_cancel, *msg_pbv;
static GtkWidget *err_dlg, *err_msg, *err_btn;

/* Data stores for tree views */

GtkListStore *categories, *packages;

/* Data stores and counters for packages to install and remove */

guint n_inst, n_uninst;
gchar **pinst, **puninst;

char *lang, *lang_loc;
gboolean needs_reboot, first_read, no_update = FALSE, is_pi = TRUE;
int calls;
gchar *sel_cat;

/*----------------------------------------------------------------------------*/
/* Prototypes                                                                 */
/*----------------------------------------------------------------------------*/

static char *name_from_id (const gchar *id);
static void progress (PkProgress *progress, PkProgressType *type, gpointer data);
static PkResults *error_handler (PkTask *task, GAsyncResult *res, char *desc, gboolean silent, gboolean terminal);
static gboolean update_self (gpointer data);
static void refresh_cache_done (PkTask *task, GAsyncResult *res, gpointer data);
static gboolean filter_fn (PkPackage *package, gpointer user_data);
static void resolve_1_done (PkTask *task, GAsyncResult *res, gpointer data);
static void update_done (PkTask *task, GAsyncResult *res, gpointer data);
static void read_data_file (PkTask *task);
static gboolean match_pid (char *name, const char *pid);
static gboolean match_arch (char *arch);
static void resolve_2_done (PkTask *task, GAsyncResult *res, gpointer data);
static void details_done (PkTask *task, GAsyncResult *res, gpointer data);
static void install_handler (GtkButton* btn, gpointer ptr);
static void install_done (PkTask *task, GAsyncResult *res, gpointer data);
static void remove_done (PkTask *task, GAsyncResult *res, gpointer data);
static gboolean reload (GtkButton *button, gpointer data);
static gboolean quit (GtkButton *button, gpointer data);
static void error_box (char *msg, gboolean terminal);
static void message (char *msg, int wait, int prog);
static gboolean clock_synced (void);
static void resync (void);
static gboolean ntp_check (gpointer data);
static char *get_shell_string (char *cmd);
static gboolean net_available (void);
static const char *cat_icon_name (char *category);
static gboolean match_category (GtkTreeModel *model, GtkTreeIter *iter, gpointer data);
static gboolean packs_in_cat (GtkTreeModel *model, GtkTreeIter *iter, gpointer data);
static void category_selected (GtkTreeView *tv, gpointer ptr);
static void install_toggled (GtkCellRendererToggle *cell, gchar *path, gpointer user_data);
static void close_handler (GtkButton* btn, gpointer ptr);
static gboolean search_update (GtkEditable *editable, gpointer userdata);
static void get_locales (void);

/*----------------------------------------------------------------------------*/
/* Helper functions for async operations                                      */
/*----------------------------------------------------------------------------*/

static char *name_from_id (const gchar *id)
{
    GtkTreeIter iter;
    gboolean valid;
    gchar *tid, *name;

    valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (packages), &iter);
    while (valid)
    {
        gtk_tree_model_get (GTK_TREE_MODEL (packages), &iter, PACK_PACKAGE_ID, &tid, PACK_CELL_NAME, &name, -1);
        if (!g_strcmp0 (id, tid)) return name;
        valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (packages), &iter);
    }
    return NULL;
}

static void progress (PkProgress *progress, PkProgressType *type, gpointer data)
{
    char *buf, *name;
    int role = pk_progress_get_role (progress);
    int status = pk_progress_get_status (progress);

    //printf ("progress %d %d %d %d %s\n", role, type, status, pk_progress_get_percentage (progress), pk_progress_get_package_id (progress));

    if (msg_dlg)
    {
        switch (role)
        {
            case PK_ROLE_ENUM_REFRESH_CACHE :       if (status == PK_STATUS_ENUM_LOADING_CACHE)
                                                        message (_("Updating package data - please wait..."), 0, pk_progress_get_percentage (progress));
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;

            case PK_ROLE_ENUM_RESOLVE :             if (status == PK_STATUS_ENUM_LOADING_CACHE)
                                                        message (_("Finding packages - please wait..."), 0, pk_progress_get_percentage (progress));
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;

            case PK_ROLE_ENUM_UPDATE_PACKAGES :     if (status == PK_STATUS_ENUM_LOADING_CACHE)
                                                        message (_("Updating application - please wait..."), 0, pk_progress_get_percentage (progress));
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;

            case PK_ROLE_ENUM_GET_DETAILS :         if (status == PK_STATUS_ENUM_LOADING_CACHE)
                                                        message (_("Reading package details - please wait..."), 0, pk_progress_get_percentage (progress));
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;

            case PK_ROLE_ENUM_INSTALL_PACKAGES :    if (status == PK_STATUS_ENUM_DOWNLOAD || status == PK_STATUS_ENUM_INSTALL)
                                                    {
                                                        name = name_from_id (pk_progress_get_package_id (progress));
                                                        buf = g_strdup_printf (_("%s %s - please wait..."), status == PK_STATUS_ENUM_INSTALL ? _("Installing") : _("Downloading"),
                                                            name ? name : _("packages"));
                                                        message (buf, 0, pk_progress_get_percentage (progress));
                                                    }
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;

            case PK_ROLE_ENUM_REMOVE_PACKAGES :     if (status == PK_STATUS_ENUM_REMOVE)
                                                    {
                                                        name = name_from_id (pk_progress_get_package_id (progress));
                                                        if (name)
                                                        {
                                                            buf = g_strdup_printf (_("Removing %s - please wait..."), name);
                                                            message (buf, 0, pk_progress_get_percentage (progress));
                                                        }
                                                        else
                                                            message (_("Removing packages - please wait..."), 0, pk_progress_get_percentage (progress));
                                                   }
                                                    else
                                                        gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
                                                    break;
        }
    }
}

static PkResults *error_handler (PkTask *task, GAsyncResult *res, char *desc, gboolean silent, gboolean terminal)
{
    PkResults *results;
    PkError *pkerror;
    GError *error = NULL;
    gchar *buf;

    results = pk_task_generic_finish (task, res, &error);
    if (error != NULL)
    {
        if (silent) return NULL;
        buf = g_strdup_printf (_("Error %s - %s"), desc, error->message);
        error_box (buf, terminal);
        g_free (buf);
        return NULL;
    }

    pkerror = pk_results_get_error_code (results);
    if (pkerror != NULL)
    {
        if (silent) return NULL;
        buf = g_strdup_printf (_("Error %s - %s"), desc, pk_error_get_details (pkerror));
        error_box (buf, terminal);
        g_free (buf);
        return NULL;
    }

    return results;
}

/*----------------------------------------------------------------------------*/
/* Handlers for asynchronous initialisation sequence at start                 */
/*----------------------------------------------------------------------------*/

static gboolean update_self (gpointer data)
{
    PkTask *task;

    message (_("Updating package data - please wait..."), 0 , -1);

    task = pk_task_new ();
    if (no_update)
    {
        read_data_file (task);
        return FALSE;
    }
    pk_client_refresh_cache_async (PK_CLIENT (task), TRUE, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) refresh_cache_done, NULL);
    return FALSE;
}

static void refresh_cache_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    gchar *pkg[2] = { "rp-prefapps", NULL };

    if (!error_handler (task, res, _("updating package data"), FALSE, TRUE)) return;

    message (_("Finding packages - please wait..."), 0 , -1);

    pk_client_resolve_async (PK_CLIENT (task), 0, pkg, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) resolve_1_done, NULL);
}

static gboolean filter_fn (PkPackage *package, gpointer user_data)
{
    if (is_pi) return TRUE;
    if (strstr (pk_package_get_arch (package), "amd64")) return FALSE;
    return TRUE;
}

static void resolve_1_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    PkResults *results;
    PkPackageSack *sack, *fsack;
    gchar **ids;

    results = error_handler (task, res, _("finding packages"), TRUE, FALSE);

    // Ignore errors here - if the update failed, carry on with existing data...
    if (results)
    {
        sack = pk_results_get_package_sack (results);
        fsack = pk_package_sack_filter (sack, filter_fn, NULL);

        ids = pk_package_sack_get_ids (fsack);
        if (*ids)
        {
            message (_("Updating application - please wait..."), 0 , -1);
            pk_task_update_packages_async (task, ids, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) update_done, NULL);
            g_strfreev (ids);
            g_object_unref (sack);
            g_object_unref (fsack);
        }
        else
        {
            g_object_unref (sack);
            g_object_unref (fsack);
            read_data_file (task);
        }
    }
    else read_data_file (task);
}

static void update_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    // No point handling error here - if the update failed, carry on with existing data...

    read_data_file (task);
}

static void read_data_file (PkTask *task)
{
    GtkTreeIter entry, cat_entry;
    GdkPixbuf *icon;
    GtkIconInfo *iinfo;
    GKeyFile *kf;
    gchar **groups, **pnames;
    gchar *buf, *cat, *name, *desc, *iname, *loc, *pack, *rpack, *adds, *add, *addspl, *arch;
    gboolean new, reboot, rpdesc;
    int pcount = 0, gcount = 0;

    loc = setlocale (0, "");
    strtok (loc, "_. ");
    buf = g_strdup_printf ("%s/prefapps_%s.conf", PACKAGE_DATA_DIR, loc);

    pnames = malloc (sizeof (gchar *));

    kf = g_key_file_new ();
    if (g_key_file_load_from_file (kf, buf, G_KEY_FILE_NONE, NULL) ||
        g_key_file_load_from_file (kf, PACKAGE_DATA_DIR "/prefapps.conf", G_KEY_FILE_NONE, NULL))
    {
        g_free (buf);
        if (first_read)
        {
            gtk_list_store_append (GTK_LIST_STORE (categories), &cat_entry);
            icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), "rpi", 32, 0, NULL);
            gtk_list_store_set (categories, &cat_entry, CAT_ICON, icon, CAT_NAME, "All Programs", CAT_DISP_NAME, _("All Programs"), -1);
            if (icon) g_object_unref (icon);
        }
        groups = g_key_file_get_groups (kf, NULL);

        while (groups[gcount])
        {
            cat = g_key_file_get_value (kf, groups[gcount], "category", NULL);
            name = g_key_file_get_value (kf, groups[gcount], "name", NULL);
            desc = g_key_file_get_value (kf, groups[gcount], "description", NULL);
            iname = g_key_file_get_value (kf, groups[gcount], "icon", NULL);
            pack = g_key_file_get_value (kf, groups[gcount], "package", NULL);
            rpack = g_key_file_get_value (kf, groups[gcount], "rpackage", NULL);
            adds = g_key_file_get_value (kf, groups[gcount], "additional", NULL);
            reboot = g_key_file_get_boolean (kf, groups[gcount], "reboot", NULL);
            arch = g_key_file_get_value (kf, groups[gcount], "arch", NULL);
            rpdesc = g_key_file_get_boolean (kf, groups[gcount], "rpdesc", NULL);

            // create array of package names
            pnames = realloc (pnames, (pcount + 1 + (rpack ? 2 : 1)) * sizeof (gchar *));
            pnames[pcount++] = g_strdup (pack);
            if (rpack) pnames[pcount++] = g_strdup (rpack);
            pnames[pcount] = NULL;

            // add additional packages to array of names
            if (adds && *adds)
            {
                // additional packages separated by commas
                addspl = g_strdup (adds);
                add = strtok (addspl, ",");
                while (add)
                {
                    if (strchr (add, '%'))
                    {
                        // substitute %s with locale strings
                        if (*lang)
                        {
                            pnames = realloc (pnames, (pcount + 2) * sizeof (gchar *));
                            pnames[pcount++] = g_strdup_printf (add, lang);
                        }
                        if (*lang_loc)
                        {
                            pnames = realloc (pnames, (pcount + 2) * sizeof (gchar *));
                            pnames[pcount++] = g_strdup_printf (add, lang_loc);
                        }
                    }
                    else
                    {
                        pnames = realloc (pnames, (pcount + 2) * sizeof (gchar *));
                        pnames[pcount++] = g_strdup (add);
                    }
                    add = strtok (NULL, ",");
                }
                g_free (addspl);
            }

            // add unique entries to category list
            if (first_read)
            {
                new = TRUE;
                gtk_tree_model_get_iter_first (GTK_TREE_MODEL (categories), &cat_entry);
                while (gtk_tree_model_iter_next (GTK_TREE_MODEL (categories), &cat_entry))
                {
                    gtk_tree_model_get (GTK_TREE_MODEL (categories), &cat_entry, CAT_NAME, &buf, -1);
                    if (!g_strcmp0 (cat, buf))
                    {
                        new = FALSE;
                        g_free (buf);
                        break;
                    }
                    g_free (buf);
                }

                if (new)
                {
                    icon = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (), cat_icon_name (cat), 32, 0, NULL);
                    gtk_list_store_append (categories, &cat_entry);
                    gtk_list_store_set (categories, &cat_entry, CAT_ICON, icon, CAT_NAME, cat, CAT_DISP_NAME, _(cat), -1);
                    if (icon) g_object_unref (icon);
                }
            }

            // create the entry for the packages list
            iinfo = gtk_icon_theme_lookup_icon (gtk_icon_theme_get_default (), iname, 32, GTK_ICON_LOOKUP_FORCE_SIZE);
            if (iinfo)
            {
                icon = gtk_icon_info_load_icon (iinfo, NULL);
                g_object_unref (iinfo);
            }
            if (!icon)
            {
                iinfo = gtk_icon_theme_lookup_icon (gtk_icon_theme_get_default (), "application-x-executable", 32, GTK_ICON_LOOKUP_FORCE_SIZE);
                if (iinfo)
                {
                    icon = gtk_icon_info_load_icon (iinfo, NULL);
                    g_object_unref (iinfo);
                }
            }
            gtk_list_store_append (packages, &entry);
            buf = g_strdup_printf (_("<b>%s</b>\n%s"), name, desc);
            gtk_list_store_set (packages, &entry,
                PACK_ICON, icon,
                PACK_CELL_TEXT, buf,
                PACK_INSTALLED, FALSE,
                PACK_INIT_INST, FALSE,
                PACK_CATEGORY, cat,
                PACK_PACKAGE_NAME, pack,
                PACK_PACKAGE_ID, "none",
                PACK_RPACKAGE_NAME, rpack,
                PACK_RPACKAGE_ID, "none",
                PACK_CELL_NAME, name,
                PACK_CELL_DESC, desc,
                PACK_ADD_NAMES, adds,
                PACK_ADD_IDS, "none",
                PACK_REBOOT, reboot,
                PACK_ARCH, arch ? arch : "any",
                PACK_RPDESC, rpdesc,
                -1);
            if (icon) g_object_unref (icon);

            g_free (buf);
            g_free (cat);
            g_free (name);
            g_free (desc);
            g_free (iname);
            g_free (pack);
            g_free (rpack);
            g_free (adds);
            g_free (arch);

            gcount++;
        }
        g_free (groups);
    }
    else
    {
        // handle no data file here...
        g_free (buf);
        g_free (pnames);
        error_box (_("Unable to open package data file"), TRUE);
        return;
    }

    message (_("Finding packages - please wait..."), 0 , -1);

    pk_client_resolve_async (PK_CLIENT (task), 0, pnames, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) resolve_2_done, NULL);
    g_free (pnames);
}


static gboolean match_pid (char *name, const char *pid)
{
    char *buf;
    gboolean ret = FALSE;

    if (name == NULL) return FALSE;
    buf = g_strdup (pid);
    g_strdelimit (buf, ";", 0);
    if (!g_strcmp0 (buf, name)) ret = TRUE;
    g_free (buf);
    return ret;
}

static gboolean match_arch (char *arch)
{
    if (!g_strcmp0 (arch, "any")) return TRUE;

    char *cmd = g_strdup_printf ("arch | grep -q %s", arch);
    FILE *fp = popen (cmd, "r");
    int res = pclose (fp);
    g_free (cmd);
    if (!res) return TRUE;
    return FALSE;
}

static void resolve_2_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    PkResults *results;
    PkPackage *item;
    PkPackageSack *sack, *fsack;
    PkInfoEnum info;
    GPtrArray *array;
    GtkTreeIter iter;
    gchar **ids;
    gboolean valid, inst;
    gchar *pack, *rpack, *package_id, *arch;
    gchar *addpks, *addpk, *addids, *addlist;
    gchar *curr_id;
    int i;

    results = error_handler (task, res, _("finding packages"), FALSE, TRUE);
    if (!results) return;

    sack = pk_results_get_package_sack (results);
    fsack = pk_package_sack_filter (sack, filter_fn, NULL);
    array = pk_package_sack_get_array (fsack);

    // Need to loop through the array of returned IDs twice. On the first pass, only look at
    // IDs of packages which are installed; for each of those, store the ID. Need to store both
    // ID and rID (if there is one)

    for (i = 0; i < array->len; i++)
    {
        item = g_ptr_array_index (array, i);
        g_object_get (item, "info", &info, "package-id", &package_id, NULL);

        if (info == PK_INFO_ENUM_INSTALLED)
        {
            valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (packages), &iter);
            while (valid)
            {
                gtk_tree_model_get (GTK_TREE_MODEL (packages), &iter, PACK_PACKAGE_NAME, &pack, PACK_RPACKAGE_NAME, &rpack, -1);
                if (match_pid (pack, package_id))
                {
                    gtk_list_store_set (packages, &iter, PACK_PACKAGE_ID, package_id, PACK_INSTALLED, TRUE, PACK_INIT_INST, TRUE, -1);
                    break;
                }
                if (match_pid (rpack, package_id))
                {
                    gtk_list_store_set (packages, &iter, PACK_RPACKAGE_ID, package_id, PACK_INSTALLED, TRUE, PACK_INIT_INST, TRUE, -1);
                    break;
                }
                valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (packages), &iter);
                g_free (pack);
                g_free (rpack);
            }
        }

        // fill in this id for any additional packages which match it
        valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (packages), &iter);
        while (valid)
        {
            gtk_tree_model_get (GTK_TREE_MODEL (packages), &iter, PACK_ADD_NAMES, &addpks, PACK_ADD_IDS, &addids, -1);
            if (addpks && *addpks)
            {
                addpk = strtok (addpks, ",");
                while (addpk)
                {
                    gboolean matched = FALSE;
                    char *str;
                    if (strchr (addpk, '%'))
                    {
                        // substitute %s with locale strings
                        if (*lang)
                        {
                            str = g_strdup_printf (addpk, lang);
                            if (match_pid (str, package_id)) matched = TRUE;
                            g_free (str);
                        }
                        if (*lang_loc)
                        {
                            str = g_strdup_printf (addpk, lang_loc);
                            if (match_pid (str, package_id)) matched = TRUE;
                            g_free (str);
                        }
                    }
                    else if (match_pid (addpk, package_id)) matched = TRUE;

                    if (matched)
                    {
                        if (!g_strcmp0 (addids, "none"))
                            gtk_list_store_set (packages, &iter, PACK_ADD_IDS, package_id, -1);
                        else
                        {
                            // DANGER, WILL ROBINSON - if additional packages ever come in multiple architectures, this will need to be fixed,
                            // by going through each string in the existing addids array and seeing if it matches the new string except for
                            // the archicture, and replacing it with the new string if so; just appending it as now otherwise.
                            // This will be incredibly tedious, so I'm not doing it until I need to...
                            addlist = g_strdup_printf ("%s,%s", addids, package_id);
                            gtk_list_store_set (packages, &iter, PACK_ADD_IDS, addlist, -1);
                            g_free (addlist);
                        }
                        break;
                    }
                    addpk = strtok (NULL, ",");
                }
            }
            valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (packages), &iter);
            g_free (addpks);
            g_free (addids);
        }

        g_free (package_id);
    }

    // At this point, the database contains a valid package ID (and possibly an rID) for each installed package. It
    // has no IDs for uninstalled packages - so fill in the rest; only update data for an entry which does not already
    // have the installed flag set

    for (i = 0; i < array->len; i++)
    {
        item = g_ptr_array_index (array, i);
        g_object_get (item, "info", &info, "package-id", &package_id, NULL);

        if (info != PK_INFO_ENUM_INSTALLED)
        {
            valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (packages), &iter);
            while (valid)
            {
                gtk_tree_model_get (GTK_TREE_MODEL (packages), &iter, PACK_INSTALLED, &inst, PACK_PACKAGE_NAME, &pack, PACK_ARCH, &arch, PACK_PACKAGE_ID, &curr_id, -1);

                if (!inst && match_pid (pack, package_id) && match_arch (arch))
                {
                    // If this package already has a PID stored, then only overwrite it if the new version is arm64 (because the current one will then be armhf)
                    if (!g_strcmp0 (curr_id, "none") || strstr (package_id, "arm64"))
                        gtk_list_store_set (packages, &iter, PACK_PACKAGE_ID, package_id, -1);
                    break;
                }
                valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (packages), &iter);
                g_free (pack);
                g_free (arch);
            }
        }
        g_free (package_id);
    }
    g_ptr_array_unref (array);

    message (_("Reading package details - please wait..."), 0 , -1);

    ids = pk_package_sack_get_ids (fsack);
    pk_client_get_details_async (PK_CLIENT (task), ids, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) details_done, NULL);
    g_strfreev (ids);
    g_object_unref (sack);
    g_object_unref (fsack);
}

static int category_sort (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer userdata)
{
    gchar *name1, *name2;
    int ret;

    gtk_tree_model_get (model, a, CAT_DISP_NAME, &name1, -1);
    gtk_tree_model_get (model, b, CAT_DISP_NAME, &name2, -1);

    if (!g_strcmp0 (name1, _("All Programs"))) ret = -1;
    else if (!g_strcmp0 (name2, _("All Programs"))) ret = 1;
    else ret = g_strcmp0 (name1, name2);

    g_free (name1);
    g_free (name2);

    return ret;
}

static void details_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    PkResults *results;
    PkDetails *item;
    GPtrArray *array;
    GtkTreeIter iter;
    GtkTreeModel *scateg, *fcateg, *spackages, *fpackages;
    gboolean valid, rpdesc;
    gchar *pid, *rid, *desc, *esc;
    const gchar *package_id, *sum, *pd;
    int i;

    results = error_handler (task, res, _("reading package details"), FALSE, TRUE);
    if (!results) return;

    array = pk_results_get_details_array (results);

    for (i = 0; i < array->len; i++)
    {
        item = g_ptr_array_index (array, i);
        package_id = pk_details_get_package_id (item);

        valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (packages), &iter);
        while (valid)
        {
            sum = pk_details_get_summary (item);
            pd = pk_details_get_description (item);
            if (sum && pd)
            {
                if (strcmp (sum, pd))
                    desc = g_strdup_printf ("%s\n\n%s", sum, pd);
                else
                    desc = g_strdup_printf ("%s", sum);
            }
            else if (sum)
                desc = g_strdup_printf ("%s", sum);
            else if (pd)
                desc = g_strdup_printf ("%s", pd);
            else desc = NULL;

            if (desc)
            {
                esc = g_markup_escape_text (desc, strlen (desc));
                g_free (desc);
            }
            else esc = NULL;

            gtk_tree_model_get (GTK_TREE_MODEL (packages), &iter, PACK_PACKAGE_NAME, &pid, PACK_RPACKAGE_NAME, &rid, PACK_RPDESC, &rpdesc, -1);

            if (match_pid (pid, package_id) && !rpdesc)
            {
                gtk_list_store_set (packages, &iter, PACK_DESCRIPTION, esc, -1);
                g_free (esc);
                g_free (pid);
                g_free (rid);
                break;
            }

            if (match_pid (rid, package_id) && rpdesc)
            {
                gtk_list_store_set (packages, &iter, PACK_DESCRIPTION, esc, -1);
                g_free (esc);
                g_free (pid);
                g_free (rid);
                break;
            }

            g_free (esc);
            g_free (pid);
            g_free (rid);
            valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (packages), &iter);
        }
    }

    // data now all loaded - set up filtered and sorted package list
    spackages = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (packages));
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (spackages), PACK_CELL_NAME, GTK_SORT_ASCENDING);
    fpackages = gtk_tree_model_filter_new (GTK_TREE_MODEL (spackages), NULL);
    gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (fpackages), (GtkTreeModelFilterVisibleFunc) match_category, NULL, NULL);
    gtk_tree_view_set_model (GTK_TREE_VIEW (pack_tv), GTK_TREE_MODEL (fpackages));

    // set up filtered and sorted category list
    scateg = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (categories));
    gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (scateg), CAT_NAME, category_sort, NULL, NULL);
    gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (scateg), CAT_NAME, GTK_SORT_ASCENDING);
    fcateg = gtk_tree_model_filter_new (GTK_TREE_MODEL (scateg), NULL);
    gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (fcateg), (GtkTreeModelFilterVisibleFunc) packs_in_cat, NULL, NULL);
    gtk_tree_view_set_model (GTK_TREE_VIEW (cat_tv), GTK_TREE_MODEL (fcateg));

    // select category
    gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (fcateg), &iter, sel_cat);
    gtk_tree_selection_select_iter (gtk_tree_view_get_selection (GTK_TREE_VIEW (cat_tv)), &iter);

    gtk_widget_set_sensitive (close_btn, TRUE);
    gtk_widget_set_sensitive (apply_btn, TRUE);

    gtk_widget_destroy (GTK_WIDGET (msg_dlg));
    msg_dlg = NULL;
}

/*----------------------------------------------------------------------------*/
/* Handlers for asynchronous install and remove sequence                      */
/*----------------------------------------------------------------------------*/

static void install_handler (GtkButton* btn, gpointer ptr)
{
    PkTask *task;
    GtkTreeIter iter;
    gboolean valid, state, init, reboot;
    gchar *id, *rid, *addid, *addids;

    gtk_widget_set_sensitive (close_btn, FALSE);
    gtk_widget_set_sensitive (apply_btn, FALSE);

    n_inst = 0;
    n_uninst = 0;
    pinst = malloc (sizeof (gchar *));
    pinst[n_inst] = NULL;
    pinst = malloc (sizeof (gchar *));
    pinst[n_uninst] = NULL;

    valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (packages), &iter);
    while (valid)
    {
        gtk_tree_model_get (GTK_TREE_MODEL (packages), &iter, PACK_INSTALLED, &state, PACK_INIT_INST, &init, PACK_PACKAGE_ID, &id, PACK_RPACKAGE_ID, &rid, PACK_ADD_IDS, &addid, PACK_REBOOT, &reboot, -1);
        if (!init)
        {
            if (state)
            {
                // needs install
                pinst = realloc (pinst, (n_inst + 2) * sizeof (gchar *));
                pinst[n_inst++] = g_strdup (id);
                pinst[n_inst] = NULL;
                if (reboot) needs_reboot = TRUE;

                if (g_strcmp0 (addid, "none"))
                {
                    addids = strtok (addid, ",");
                    while (addids)
                    {
                        pinst = realloc (pinst, (n_inst + 2) * sizeof (gchar *));
                        pinst[n_inst++] = g_strdup (addids);
                        pinst[n_inst] = NULL;
                        addids = strtok (NULL, ",");
                    }
                }
            }
        }
        else
        {
            if (!state)
            {
                // needs uninstall
                puninst = realloc (puninst, (n_uninst + 2) * sizeof (gchar *));
                if (rid && g_strcmp0 (rid, "none"))
                    puninst[n_uninst++] = g_strdup (rid);
                else
                    puninst[n_uninst++] = g_strdup (id);
                puninst[n_uninst] = NULL;

                if (g_strcmp0 (addid, "none"))
                {
                    addids = strtok (addid, ",");
                    while (addids)
                    {
                        puninst = realloc (puninst, (n_uninst + 2) * sizeof (gchar *));
                        puninst[n_uninst++] = g_strdup (addids);
                        puninst[n_uninst] = NULL;
                        addids = strtok (NULL, ",");
                    }
                }
            }
        }
        g_free (id);
        g_free (rid);
        valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (packages), &iter);
    }

    if (n_inst)
    {
        message (_("Installing packages - please wait..."), 0 , -1);

        task = pk_task_new ();
        pk_task_install_packages_async (task, pinst, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) install_done, NULL);
    }
    else if (n_uninst)
    {
        message (_("Removing packages - please wait..."), 0 , -1);

        task = pk_task_new ();
        pk_task_remove_packages_async (task, puninst, TRUE, TRUE, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) remove_done, NULL);
    }
    else
    {
        gtk_widget_set_sensitive (close_btn, TRUE);
        gtk_widget_set_sensitive (apply_btn, TRUE);
    }
}

static void install_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    if (!error_handler (task, res, _("installing packages"), FALSE, FALSE)) return;

    if (n_uninst)
    {
        message (_("Removing packages - please wait..."), 0 , -1);

        pk_task_remove_packages_async (task, puninst, TRUE, TRUE, NULL, (PkProgressCallback) progress, NULL, (GAsyncReadyCallback) remove_done, NULL);
    }
    else
        message (_("Installation complete"), 1, -1);
}

static void remove_done (PkTask *task, GAsyncResult *res, gpointer data)
{
    if (!error_handler (task, res, _("removing packages"), FALSE, FALSE)) return;

    if (n_inst)
        message (_("Installation and removal complete"), 1, -1);
    else
        message (_("Removal complete"), 1, -1);
}

static gboolean clock_synced (void)
{
    if (system ("test -e /usr/sbin/ntpd") == 0)
    {
        if (system ("ntpq -p | grep -q ^\\*") == 0) return TRUE;
    }
    else
    {
        if (system ("timedatectl status | grep -q \"synchronized: yes\"") == 0) return TRUE;
    }
    return FALSE;
}

static void resync (void)
{
    if (system ("test -e /usr/sbin/ntpd") == 0)
    {
        system ("/etc/init.d/ntp stop; ntpd -gq; /etc/init.d/ntp start");
    }
    else
    {
        system ("systemctl -q stop systemd-timesyncd 2> /dev/null; systemctl -q start systemd-timesyncd 2> /dev/null");
    }
}

static gboolean ntp_check (gpointer data)
{
    gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
    if (clock_synced ())
    {
        g_idle_add (update_self, NULL);
        return FALSE;
    }
    // trigger a resync
    if (calls == 0) resync ();

    if (calls++ > 120)
    {
        error_box (_("Error synchronising clock - could not sync with time server"), TRUE);
        return FALSE;
    }

    return TRUE;
}

static char *get_shell_string (char *cmd)
{
    char *line = NULL, *res = NULL;
    size_t len = 0;
    FILE *fp = popen (cmd, "r");

    if (fp == NULL) return g_strdup ("");
    if (getline (&line, &len, fp) > 0)
    {
        g_strdelimit (line, "\n\r", 0);
        res = line;
        while (*res++) if (g_ascii_isspace (*res)) *res = 0;
        res = g_strdup (line);
    }
    pclose (fp);
    g_free (line);
    return res ? res : g_strdup ("");
}

static gboolean net_available (void)
{
    char *ip;
    gboolean val = FALSE;

    ip = get_shell_string ("hostname -I | tr ' ' \\\\n | grep \\\\. | tr \\\\n ','");
    if (ip)
    {
        if (strlen (ip)) val = TRUE;
        g_free (ip);
    }
    return val;
}

/*----------------------------------------------------------------------------*/
/* Helper functions for tree views                                            */
/*----------------------------------------------------------------------------*/

static const char *cat_icon_name (char *category)
{
    if (!g_strcmp0 (category, N_("Programming")))
        return "applications-development";

    if (!g_strcmp0 (category, N_("Office")))
        return "applications-office";

    if (!g_strcmp0 (category, N_("Internet")))
        return "applications-internet";

    if (!g_strcmp0 (category, N_("Games")))
        return "applications-games";

    if (!g_strcmp0 (category, N_("Other")))
        return "applications-other";

    if (!g_strcmp0 (category, N_("Accessories")))
        return "applications-accessories";

    if (!g_strcmp0 (category, N_("Sound & Video")))
        return "applications-multimedia";

    if (!g_strcmp0 (category, N_("System Tools")))
        return "applications-system";

    if (!g_strcmp0 (category, N_("Engineering")))
        return "applications-engineering";

    if (!g_strcmp0 (category, N_("Education")))
        return "applications-science";

    if (!g_strcmp0 (category, N_("Graphics")))
        return "applications-graphics";

    if (!g_strcmp0 (category, N_("Science & Maths")))
        return "applications-science";

    if (!g_strcmp0 (category, N_("Preferences")))
        return "preferences-desktop";

    if (!g_strcmp0 (category, N_("Universal Access")))
        return "preferences-desktop-accessibility";

    return NULL;
}

static gboolean match_category (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    GtkTreeModel *cmodel;
    GtkTreeIter citer;
    GtkTreeSelection *sel;
    char *id, *rid, *pcat, *desc, *cat;
    const gchar *search;
    gboolean res;

    // get the current category selection from the category box
    sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (cat_tv));
    if (sel && gtk_tree_selection_get_selected (sel, &cmodel, &citer))
    {
        gtk_tree_model_get (cmodel, &citer, CAT_NAME, &cat, -1);
    }
    else cat = g_strdup ("All Programs");

    // first make sure the package has a package ID - ignore if not
    gtk_tree_model_get (model, iter, PACK_PACKAGE_ID, &id, PACK_RPACKAGE_ID, &rid, PACK_CATEGORY, &pcat, PACK_CELL_DESC, &desc, -1);
    if (!g_strcmp0 (id, "none") && !g_strcmp0 (rid, "none")) res = FALSE;
    else
    {
        // check that category matches
        if (!g_strcmp0 (cat, "All Programs")) res = TRUE;
        else
        {
            if (!g_strcmp0 (cat, pcat)) res = TRUE;
            else res = FALSE;
        }

        // filter on search text
        if (res)
        {
            search = gtk_entry_get_text (GTK_ENTRY (search_te));
            if (search[0])
            {
                if (!strcasestr (desc, search)) res = FALSE;
            }
        }
    }
    g_free (id);
    g_free (rid);
    g_free (pcat);
    g_free (desc);
    g_free (cat);
    return res;
}

static gboolean packs_in_cat (GtkTreeModel *model, GtkTreeIter *iter, gpointer data)
{
    GtkTreeIter piter;
    gboolean valid;
    gchar *pcat, *tcat, *id, *rid;

	// get the category under test
    gtk_tree_model_get (model, iter, CAT_NAME, &tcat, -1);

    // always show All Programs category
    if (!g_strcmp0 (tcat, "All Programs")) return TRUE;

	// loop through all packages in database - show category only if it matches a program with a valid ID
    valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (packages), &piter);
    while (valid)
    {
        gtk_tree_model_get (GTK_TREE_MODEL (packages), &piter, PACK_CATEGORY, &pcat, PACK_PACKAGE_ID, &id, PACK_RPACKAGE_ID, &rid, -1);
        if (!g_strcmp0 (pcat, tcat) && (g_strcmp0 (id, "none") || g_strcmp0 (rid, "none")))
        {
			g_free (tcat);
			g_free (pcat);
			g_free (id);
			g_free (rid);
			return TRUE;
		}

        g_free (pcat);
		g_free (id);
		g_free (rid);
        valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (packages), &piter);
    }
    g_free (tcat);
    return FALSE;
}

/*----------------------------------------------------------------------------*/
/* Progress / error box                                                       */
/*----------------------------------------------------------------------------*/

static gboolean reload (GtkButton *button, gpointer data)
{
    PkTask *task;

    if (msg_dlg)
    {
        gtk_widget_destroy (GTK_WIDGET (msg_dlg));
        msg_dlg = NULL;
    }
    if (err_dlg)
    {
        gtk_widget_destroy (GTK_WIDGET (err_dlg));
        err_dlg = NULL;
    }

    message (_("Updating package data - please wait..."), 0 , -1);

    gtk_list_store_clear (packages);
    gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (GTK_TREE_VIEW (pack_tv))));
    task = pk_task_new ();
    first_read = FALSE;
    read_data_file (task);
    return FALSE;
}

static gboolean quit (GtkButton *button, gpointer data)
{
    if (msg_dlg)
    {
        gtk_widget_destroy (GTK_WIDGET (msg_dlg));
        msg_dlg = NULL;
    }
    if (err_dlg)
    {
        gtk_widget_destroy (GTK_WIDGET (err_dlg));
        err_dlg = NULL;
    }

    if ((int) data == 1) system ("reboot");

    gtk_main_quit ();
    return FALSE;
}

static void error_box (char *msg, gboolean terminal)
{
    if (msg_dlg)
    {
        // clear any existing message box
        gtk_widget_destroy (GTK_WIDGET (msg_dlg));
        msg_dlg = NULL;
    }

    if (!err_dlg)
    {
        GtkBuilder *builder;

        builder = gtk_builder_new_from_file (PACKAGE_DATA_DIR "/rp_prefapps.ui");

        err_dlg = (GtkWidget *) gtk_builder_get_object (builder, "error");
        gtk_window_set_transient_for (GTK_WINDOW (err_dlg), GTK_WINDOW (main_dlg));

        err_msg = (GtkWidget *) gtk_builder_get_object (builder, "err_lbl");
        err_btn = (GtkWidget *) gtk_builder_get_object (builder, "err_btn");

        gtk_label_set_text (GTK_LABEL (err_msg), msg);

        if (terminal)
            g_signal_connect (err_btn, "clicked", G_CALLBACK (quit), (void *) 0);
        else
            g_signal_connect (err_btn, "clicked", G_CALLBACK (reload), NULL);

        gtk_widget_show_all (err_dlg);
        g_object_unref (builder);
    }
    else gtk_label_set_text (GTK_LABEL (err_msg), msg);
}


static void message (char *msg, int wait, int prog)
{
    if (err_dlg)
    {
        // clear any existing error box
        gtk_widget_destroy (GTK_WIDGET (err_dlg));
        err_dlg = NULL;
    }

    if (!msg_dlg)
    {
        GtkBuilder *builder;

        builder = gtk_builder_new_from_file (PACKAGE_DATA_DIR "/rp_prefapps.ui");

        msg_dlg = (GtkWidget *) gtk_builder_get_object (builder, "modal");
        gtk_window_set_transient_for (GTK_WINDOW (msg_dlg), GTK_WINDOW (main_dlg));

        msg_msg = (GtkWidget *) gtk_builder_get_object (builder, "modal_msg");
        msg_pb = (GtkWidget *) gtk_builder_get_object (builder, "modal_pb");
        msg_btn = (GtkWidget *) gtk_builder_get_object (builder, "modal_ok");
        msg_cancel = (GtkWidget *) gtk_builder_get_object (builder, "modal_cancel");

        gtk_label_set_text (GTK_LABEL (msg_msg), msg);

        g_object_unref (builder);
    }
    else gtk_label_set_text (GTK_LABEL (msg_msg), msg);

    if (wait)
    {
        gtk_widget_hide (msg_pb);
        if (wait > 1)
        {
            gtk_button_set_label (GTK_BUTTON (msg_btn), "_Yes");
            gtk_button_set_label (GTK_BUTTON (msg_cancel), "_No");
            g_signal_connect (msg_btn, "clicked", G_CALLBACK (quit), (void *) 1);
            g_signal_connect (msg_cancel, "clicked", G_CALLBACK (quit), (void *) 0);
            gtk_widget_show (msg_cancel);
        }
        else
        {
            g_signal_connect (msg_btn, "clicked", G_CALLBACK (reload), NULL);
            gtk_widget_hide (msg_cancel);
        }
        gtk_widget_show (msg_btn);
    }
    else
    {
        gtk_widget_hide (msg_cancel);
        gtk_widget_hide (msg_btn);
        gtk_widget_show (msg_pb);
        if (prog == -1) gtk_progress_bar_pulse (GTK_PROGRESS_BAR (msg_pb));
        else
        {
            float progress = prog / 100.0;
            gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (msg_pb), progress);
        }
    }
    gtk_widget_show (msg_dlg);
}


/*----------------------------------------------------------------------------*/
/* Handlers for main window user interaction                                  */
/*----------------------------------------------------------------------------*/

static void category_selected (GtkTreeView *tv, gpointer ptr)
{
    GtkTreeModel *model;
    GtkTreePath *path;
    GtkTreeSelection *sel;
    GtkTreeIter iter;

    // store the path of the new selection so it can be reloaded
    sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (cat_tv));
    if (sel && gtk_tree_selection_get_selected (sel, &model, &iter))
    {
        g_free (sel_cat);
        path = gtk_tree_model_get_path (model, &iter);
        sel_cat = gtk_tree_path_to_string (path);
        gtk_tree_path_free (path);
    }

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (pack_tv));
    path = gtk_tree_path_new_first ();
    gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (model));
    gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (pack_tv), path, NULL, TRUE, 0.0, 0.0);
    gtk_tree_path_free (path);
}

static void install_toggled (GtkCellRendererToggle *cell, gchar *path, gpointer user_data)
{
    GtkTreeIter iter, citer, siter;
    GtkTreeModel *model, *cmodel, *smodel;
    gboolean val, init;
    gchar *name, *desc, *buf, *state;

    model = gtk_tree_view_get_model (GTK_TREE_VIEW (pack_tv));
    gtk_tree_model_get_iter_from_string (model, &iter, path);

    cmodel = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (model));
    gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model), &citer, &iter);

    smodel = gtk_tree_model_sort_get_model (GTK_TREE_MODEL_SORT (cmodel));
    gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (cmodel), &siter, &citer);

    gtk_tree_model_get (smodel, &siter, PACK_INSTALLED, &val, PACK_INIT_INST, &init, PACK_CELL_NAME, &name, PACK_CELL_DESC, &desc, -1);

    if (!init && !val) state = g_strdup (_("   <b><small>(will be installed)</small></b>"));
    else if (init && val) state = g_strdup (_("   <b><small>(will be removed)</small></b>"));
    else state = g_strdup ("");

    buf = g_strdup_printf (_("<b>%s</b>%s\n%s"), name, state, desc);
    gtk_list_store_set (GTK_LIST_STORE (smodel), &siter, PACK_INSTALLED, 1 - val, PACK_CELL_TEXT, buf, -1);
    g_free (buf);
    g_free (state);
    g_free (name);
    g_free (desc);
}

static void close_handler (GtkButton* btn, gpointer ptr)
{
    if (needs_reboot)
        message (_("An installed application requires a reboot.\nWould you like to reboot now?"), 2, -1);
    else
        gtk_main_quit ();
}

static gboolean search_update (GtkEditable *editable, gpointer userdata)
{
    gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (gtk_tree_view_get_model (GTK_TREE_VIEW (pack_tv))));
}

static void get_locales (void)
{
    char *lstring = setlocale (LC_CTYPE, NULL);
    if (lstring && *lstring)
    {
        char *lastr = strtok (lstring, "_");
        char *lostr = strtok (NULL, ". ");
        if (lastr && *lastr)
        {
            lang = g_strdup (lastr);
            if (lostr && *lostr)
            {
                char *str = g_ascii_strdown (lostr, -1);
                lang_loc = g_strdup_printf ("%s-%s", lang, str);
                g_free (str);
            }
            else lang_loc = g_strdup ("");
        }
        else
        {
            lang = g_strdup ("");
            lang_loc = g_strdup ("");
        }
    }
    else
    {
        lang = g_strdup ("");
        lang_loc = g_strdup ("");
    }
}

/*----------------------------------------------------------------------------*/
/* Main window                                                                */
/*----------------------------------------------------------------------------*/

int main (int argc, char *argv[])
{
    GtkBuilder *builder;
    GtkCellRenderer *crp, *crt, *crb;

#ifdef ENABLE_NLS
    setlocale (LC_ALL, "");
    bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
#endif

    if (system ("raspi-config nonint is_pi")) is_pi = FALSE;

    get_locales ();
    needs_reboot = FALSE;

    // GTK setup
    gtk_init (&argc, &argv);
    gtk_icon_theme_prepend_search_path (gtk_icon_theme_get_default(), PACKAGE_DATA_DIR);

    // build the UI
    builder = gtk_builder_new_from_file (PACKAGE_DATA_DIR "/rp_prefapps.ui");

    main_dlg = (GtkWidget *) gtk_builder_get_object (builder, "main_window");
    cat_tv = (GtkWidget *) gtk_builder_get_object (builder, "treeview_cat");
    pack_tv = (GtkWidget *) gtk_builder_get_object (builder, "treeview_prog");
    close_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_cancel");
    apply_btn = (GtkWidget *) gtk_builder_get_object (builder, "button_ok");
    search_te = (GtkWidget *) gtk_builder_get_object (builder, "search");

    // create list stores
    categories = gtk_list_store_new (3, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING);
    packages = gtk_list_store_new (19, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_BOOLEAN);

    // set up tree views
    crp = gtk_cell_renderer_pixbuf_new ();
    crt = gtk_cell_renderer_text_new ();
    crb = gtk_cell_renderer_toggle_new ();

    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (cat_tv), 0, "Icon", crp, "pixbuf", CAT_ICON, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (cat_tv), 1, "Category", crt, "text", CAT_DISP_NAME, NULL);

    gtk_widget_set_size_request (cat_tv, 160, -1);
    gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (cat_tv), FALSE);
    gtk_tree_view_set_tooltip_column (GTK_TREE_VIEW (pack_tv), PACK_DESCRIPTION);

    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (pack_tv), 0, "", crp, "pixbuf", PACK_ICON, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (pack_tv), 1, _("Application"), crt, "markup", PACK_CELL_TEXT, NULL);
    gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (pack_tv), 2, _("Install"), crb, "active", PACK_INSTALLED, NULL);

    gtk_tree_view_column_set_sizing (gtk_tree_view_get_column (GTK_TREE_VIEW (pack_tv), 0), GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width (gtk_tree_view_get_column (GTK_TREE_VIEW (pack_tv), 0), 45);
    gtk_tree_view_column_set_expand (gtk_tree_view_get_column (GTK_TREE_VIEW (pack_tv), 1), TRUE);
    g_object_set (crt, "wrap-mode", PANGO_WRAP_WORD, "wrap-width", 320, NULL);
    gtk_tree_view_column_set_sizing (gtk_tree_view_get_column (GTK_TREE_VIEW (pack_tv), 2), GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width (gtk_tree_view_get_column (GTK_TREE_VIEW (pack_tv), 2), 50);

    g_signal_connect (crb, "toggled", G_CALLBACK (install_toggled), NULL);
    g_signal_connect (close_btn, "clicked", G_CALLBACK (close_handler), NULL);
    g_signal_connect (apply_btn, "clicked", G_CALLBACK (install_handler), NULL);
    g_signal_connect (main_dlg, "delete_event", G_CALLBACK (close_handler), NULL);
    g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (cat_tv)), "changed", G_CALLBACK (category_selected), NULL);
    g_signal_connect (search_te, "changed", G_CALLBACK (search_update), NULL);

    gtk_widget_set_sensitive (close_btn, FALSE);
    gtk_widget_set_sensitive (apply_btn, FALSE);

    gtk_window_set_default_size (GTK_WINDOW (main_dlg), 640, 400);
    gtk_widget_show_all (main_dlg);

    // update application, load the data file and check with backend
    if (argc > 1 && !g_strcmp0 (argv[1], "noupdate")) no_update = TRUE;
    first_read = TRUE;

    if (net_available ())
    {
        if (clock_synced ()) g_idle_add (update_self, NULL);
        else
        {
            message (_("Synchronising clock - please wait..."), 0, -1);
            calls = 0;
            g_timeout_add_seconds (1, ntp_check, NULL);
        }
    }
    else error_box (_("No network connection - applications cannot be installed"), TRUE);

    sel_cat = g_strdup_printf ("0");

    gtk_main ();

    g_object_unref (builder);
    gtk_widget_destroy (main_dlg);
    return 0;
}

/* End of file                                                                */
/*----------------------------------------------------------------------------*/
