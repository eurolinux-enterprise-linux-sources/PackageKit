/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
 * Copyright (C) 2008 Shishir Goel <crazyontheedge@gmail.com>
 *
 * Licensed under the GNU General Public License Version 2
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <fcntl.h>

#ifdef HAVE_ARCHIVE_H
#include <archive.h>
#include <archive_entry.h>
#endif /* HAVE_ARCHIVE_H */

#include <glib.h>
#include <glib/gstdio.h>

#include <packagekit-glib/pk-obj-list.h>
#include <packagekit-glib/pk-common.h>
#include <packagekit-glib/pk-client.h>
#include <packagekit-glib/pk-package-ids.h>
#include <packagekit-glib/pk-service-pack.h>

#include "egg-debug.h"
#include "egg-string.h"

#define PK_SERVICE_PACK_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_SERVICE_PACK, PkServicePackPrivate))

typedef enum {
	PK_SERVICE_PACK_TYPE_UPDATE,
	PK_SERVICE_PACK_TYPE_INSTALL,
	PK_SERVICE_PACK_TYPE_UNKNOWN
} PkServicePackType;

struct PkServicePackPrivate
{
	PkPackageList		*exclude_list;
	gchar			*filename;
	gchar			*directory;
	PkClient		*client;
	PkServicePackType	 type;
};

typedef enum {
	PK_SERVICE_PACK_PACKAGE,
	PK_SERVICE_PACK_PERCENTAGE,
	PK_SERVICE_PACK_STATUS,
	PK_SERVICE_PACK_LAST_SIGNAL
} PkSignals;

static guint signals [PK_SERVICE_PACK_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (PkServicePack, pk_service_pack, G_TYPE_OBJECT)

/**
 * pk_service_pack_error_quark:
 *
 * Return value: Our personal error quark.
 **/
GQuark
pk_service_pack_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("pk_service_pack_error");
	return quark;
}

/**
 * pk_service_pack_error_get_type:
 **/
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
pk_service_pack_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (PK_SERVICE_PACK_ERROR_FAILED_SETUP, "FailedSetup"),
			ENUM_ENTRY (PK_SERVICE_PACK_ERROR_FAILED_DOWNLOAD, "FailedDownload"),
			ENUM_ENTRY (PK_SERVICE_PACK_ERROR_FAILED_EXTRACTION, "FailedExtraction"),
			ENUM_ENTRY (PK_SERVICE_PACK_ERROR_FAILED_CREATE, "FailedCreate"),
			ENUM_ENTRY (PK_SERVICE_PACK_ERROR_NOTHING_TO_DO, "NothingToDo"),
			ENUM_ENTRY (PK_SERVICE_PACK_ERROR_NOT_COMPATIBLE, "NotCompatible"),
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("PkServicePackError", values);
	}
	return etype;
}

/**
 * pk_service_pack_check_metadata_file:
 **/
static gboolean
pk_service_pack_check_metadata_file (const gchar *full_path, GError **error)
{
	GKeyFile *file;
	gboolean ret;
	GError *error_local = NULL;
	gchar *type = NULL;
	gchar *distro_id = NULL;
	gchar *distro_id_us = NULL;

	/* load the file */
	file = g_key_file_new ();
	ret = g_key_file_load_from_file (file, full_path, G_KEY_FILE_NONE, &error_local);
	if (!ret) {
		*error = g_error_new (1, 0, "failed to load file: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* read the value */
	distro_id = g_key_file_get_string (file, PK_SERVICE_PACK_GROUP_NAME, "distro_id", &error_local);
	if (distro_id == NULL) {
		*error = g_error_new (1, 0, "failed to get value: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* read the value */
	type = g_key_file_get_string (file, PK_SERVICE_PACK_GROUP_NAME, "type", &error_local);
	if (type == NULL) {
		*error = g_error_new (1, 0, "failed to get type: %s", error_local->message);
		g_error_free (error_local);
		ret = FALSE;
		goto out;
	}

	/* check the types we support */
	if (g_strcmp0 (type, "update") != 0 && g_strcmp0 (type, "install") != 0) {
		*error = g_error_new (1, 0, "does not have correct type key: %s", type);
		ret = FALSE;
		goto out;
	}

	/* get this system id */
	distro_id_us = pk_get_distro_id ();

	/* do we match? */
	ret = (g_strcmp0 (distro_id_us, distro_id) == 0);
	if (!ret)
		*error = g_error_new (1, 0, "distro id did not match %s == %s", distro_id_us, distro_id);

out:
	g_key_file_free (file);
	g_free (type);
	g_free (distro_id);
	g_free (distro_id_us);
	return ret;
}

/**
 * pk_service_pack_extract:
 * @directory: the directory to unpack into
 * @error: a valid %GError
 *
 * Decompress a tar file
 *
 * Return value: %TRUE if the file was decompressed
 **/
#ifdef HAVE_ARCHIVE_H
static gboolean
pk_service_pack_extract (const gchar *filename, const gchar *directory, GError **error)
{
	gboolean ret = FALSE;
	struct archive *arch = NULL;
	struct archive_entry *entry;
	int r;
	int retval;
	gchar *retcwd;
	gchar buf[PATH_MAX];

	/* save the PWD as we chdir to extract */
	retcwd = getcwd (buf, PATH_MAX);
	if (retcwd == NULL) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed to get cwd");
		goto out;
	}

	/* we can only read tar achives */
	arch = archive_read_new ();
	archive_read_support_format_tar (arch);

	/* open the tar file */
	r = archive_read_open_file (arch, filename, 10240);
	if (r) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_EXTRACTION,
				      "cannot open: %s", archive_error_string (arch));
		goto out;
	}

	/* switch to our destination directory */
	retval = chdir (directory);
	if (retval != 0) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed chdir to %s", directory);
		goto out;
	}

	/* decompress each file */
	for (;;) {
		r = archive_read_next_header (arch, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r != ARCHIVE_OK) {
			*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_EXTRACTION,
					      "cannot read header: %s", archive_error_string (arch));
			goto out;
		}
		r = archive_read_extract (arch, entry, 0);
		if (r != ARCHIVE_OK) {
			*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_EXTRACTION,
					      "cannot extract: %s", archive_error_string (arch));
			goto out;
		}
	}

	/* completed all okay */
	ret = TRUE;
out:
	/* close the archive */
	if (arch != NULL) {
		archive_read_close (arch);
		archive_read_finish (arch);
	}

	/* switch back to PWD */
	retval = chdir (buf);
	if (retval != 0)
		egg_warning ("cannot chdir back!");

	return ret;
}
#else /* HAVE_ARCHIVE_H */
static gboolean
pk_service_pack_extract (const gchar *filename, const gchar *directory, GError **error)
{
	*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_EXTRACTION,
			      "The service pack %s cannot be extracted as PackageKit was not built with libarchive support", filename);
	return FALSE;
}
#endif /* HAVE_ARCHIVE_H */

/**
 * pk_service_pack_get_random:
 **/
static gchar *
pk_service_pack_get_random (const gchar *prefix, guint length)
{
	guint32 n;
	gchar *str;
	guint i;
	guint prefix_len;

	/* make a string to hold both parts */
	prefix_len = egg_strlen (prefix, 28);
	str = g_strnfill (length + prefix_len, 'X');

	/* copy over prefix */
	for (i=0; i<prefix_len; i++)
		str[i] = prefix[i];

	/* use random string */
	for (i=prefix_len; i<length+prefix_len; i++) {
		n = g_random_int_range (97, 122);
		str[i] = (gchar) n;
	}
	return str;
}

/**
 * pk_service_pack_create_temporary_directory:
 **/
static gchar *
pk_service_pack_create_temporary_directory (const gchar *prefix)
{
	gboolean ret;
	gchar *random;
	gchar *directory = NULL;

	/* ensure path does not already exist */
	do {
		/* last iter results, or NULL */
		g_free (directory);

		/* get a random path */
		random = pk_service_pack_get_random (prefix, 8);

		/* ITS4: ignore, the user has no control over the daemon envp  */
		directory = g_build_filename (g_get_tmp_dir (), random, NULL);
		g_free (random);
		ret = g_file_test (directory, G_FILE_TEST_IS_DIR);
	} while (ret);

	/* create so only user (root) has rwx access */
	g_mkdir (directory, 0700);

	return directory;
}

/**
 * pk_service_pack_check_valid:
 * @pack: a valid #PkServicePack instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Checks to see if a service pack file is valid, and usable with this system.
 *
 * Return value: %TRUE if the service pack is valid
 **/
gboolean
pk_service_pack_check_valid (PkServicePack *pack, GError **error)
{
	gboolean ret = TRUE;
	gchar *directory = NULL;
	gchar *metafile = NULL;
	GDir *dir = NULL;
	const gchar *filename_entry;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (pack->priv->filename != NULL, FALSE);

	/* create a random directory */
	directory = pk_service_pack_create_temporary_directory ("PackageKit-");
	ret = pk_service_pack_extract (pack->priv->filename, directory, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, error_local->code,
				      "failed to check %s: %s", pack->priv->filename, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the files */
	dir = g_dir_open (directory, 0, NULL);
	if (dir == NULL) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed to get directory for %s", directory);
		ret = FALSE;
		goto out;
	}

	/* find the file, and check the metadata */
	while ((filename_entry = g_dir_read_name (dir))) {
		metafile = g_build_filename (directory, filename_entry, NULL);
		if (g_strcmp0 (filename_entry, "metadata.conf") == 0) {
			ret = pk_service_pack_check_metadata_file (metafile, &error_local);
			if (!ret) {
				*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_NOT_COMPATIBLE,
						      "Service Pack %s not compatible with your distro: %s", pack->priv->filename, error_local->message);
				g_error_free (error_local);
				ret = FALSE;
				goto out;
			}
		}
		g_free (metafile);
	}
out:
	g_rmdir (directory);
	g_free (directory);
	if (dir != NULL)
		g_dir_close (dir);
	return ret;
}

/**
 * pk_service_pack_status_changed:
 **/
static void
pk_service_pack_status_changed (PkServicePack *pack, PkServicePackStatus status)
{
	egg_debug ("emit status %i", status);
	g_signal_emit (pack, signals [PK_SERVICE_PACK_STATUS], 0, status);
}

/**
 * pk_service_pack_set_filename:
 * @pack: a valid #PkServicePack instance
 * @filename: the filename to use
 *
 * Sets the filename to use when reading or writing a service pack
 *
 * Return value: %TRUE if the name was set
 **/
gboolean
pk_service_pack_set_filename (PkServicePack *pack, const gchar *filename)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_free (pack->priv->filename);
	pack->priv->filename = g_strdup (filename);
	return TRUE;
}

/**
 * pk_service_pack_set_temp_directory:
 * @pack: a valid #PkServicePack instance
 * @directory: the directory to use, or %NULL to use the default
 *
 * Sets the directory to use when decompressing the service pack
 *
 * Return value: %TRUE if the directory was set
 **/
gboolean
pk_service_pack_set_temp_directory (PkServicePack *pack, const gchar *directory)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_free (pack->priv->directory);

	/* use default */
	if (directory == NULL)
		directory = pk_service_pack_create_temporary_directory ("PackageKit-");

	pack->priv->directory = g_strdup (directory);
	return TRUE;
}

/**
 * pk_service_pack_set_exclude_list:
 * @pack: a valid #PkServicePack instance
 * @list: the list of packages to exclude
 *
 * Sets the list of packages to exclude from the dependency downloads.
 *
 * Return value: %TRUE if the list was set
 **/
gboolean
pk_service_pack_set_exclude_list (PkServicePack *pack, PkPackageList *list)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (list != NULL, FALSE);
	if (pack->priv->exclude_list != NULL)
		g_object_unref (pack->priv->exclude_list);
	pack->priv->exclude_list = g_object_ref (list);
	return TRUE;
}

/**
 * pk_service_pack_download_package_ids:
 **/
static gboolean
pk_service_pack_download_package_ids (PkServicePack *pack, gchar **package_ids, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (pack->priv->directory != NULL, FALSE);

	egg_debug ("download+ %s", package_ids[0]);
	ret = pk_client_reset (pack->priv->client, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed to reset: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
	ret = pk_client_download_packages (pack->priv->client, package_ids, pack->priv->directory, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_DOWNLOAD,
				      "failed to download: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	return ret;
}

/**
 * pk_service_pack_exclude_packages:
 **/
static gboolean
pk_service_pack_exclude_packages (PkServicePack *pack, PkPackageList *list)
{
	guint i;
	guint length;
	gboolean found;
	const PkPackageObj *obj;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (pack->priv->exclude_list != NULL, FALSE);

	/* do not just download everything, uselessly */
	length = pk_package_list_get_size (pack->priv->exclude_list);
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (pack->priv->exclude_list, i);
		/* will just ignore if the obj is not there */
		found = pk_obj_list_remove (PK_OBJ_LIST(list), obj);
		if (found)
			egg_debug ("removed %s", obj->id->name);
	}
	return TRUE;
}

#ifdef HAVE_ARCHIVE_H
/**
 * pk_service_pack_create_metadata_file:
 **/
static gboolean
pk_service_pack_create_metadata_file (PkServicePack *pack, const gchar *filename)
{
	gboolean ret = FALSE;
	gchar *distro_id = NULL;
	gchar *iso_time = NULL;
	GError *error = NULL;
	GKeyFile *file = NULL;
	gchar *data = NULL;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (filename != NULL, FALSE);
	g_return_val_if_fail (pack->priv->type != PK_SERVICE_PACK_TYPE_UNKNOWN, FALSE);

	file = g_key_file_new ();

	/* get needed data */
	distro_id = pk_get_distro_id ();
	if (distro_id == NULL)
		goto out;
	iso_time = pk_iso8601_present ();
	if (iso_time == NULL)
		goto out;

	g_key_file_set_string (file, PK_SERVICE_PACK_GROUP_NAME, "distro_id", distro_id);
	g_key_file_set_string (file, PK_SERVICE_PACK_GROUP_NAME, "created", iso_time);

	if (pack->priv->type == PK_SERVICE_PACK_TYPE_INSTALL)
		g_key_file_set_string (file, PK_SERVICE_PACK_GROUP_NAME, "type", "install");
	else if (pack->priv->type == PK_SERVICE_PACK_TYPE_UPDATE)
		g_key_file_set_string (file, PK_SERVICE_PACK_GROUP_NAME, "type", "update");

	/* convert to text */
	data = g_key_file_to_data (file, NULL, &error);
	if (data == NULL) {
		egg_warning ("failed to convert to text: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* save contents */
	ret = g_file_set_contents (filename, data, -1, &error);
	if (!ret) {
		egg_warning ("failed to save file: %s", error->message);
		g_error_free (error);
		goto out;
	}

out:
	g_key_file_free (file);
	g_free (data);
	g_free (distro_id);
	g_free (iso_time);
	return ret;
}

/**
 * pk_service_pack_archive_add_file:
 **/
static gboolean
pk_service_pack_archive_add_file (struct archive *arch, const gchar *filename, GError **error)
{
	int retval;
	int len;
	int fd = -1;
	int wrote;
	gboolean ret = FALSE;
	gchar *filename_basename = NULL;
	struct archive_entry *entry = NULL;
	struct stat st;
	gchar buff[8192];

	/* stat file */
	retval = stat (filename, &st);
	if (retval != 0) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_CREATE,
				      "file not found %s", filename);
		goto out;
	}
	egg_debug ("stat(%s), size=%lu bytes\n", filename, (glong) st.st_size);

	/* create new entry */
	entry = archive_entry_new ();
	archive_entry_copy_stat (entry, &st);
	filename_basename = g_path_get_basename (filename);
	archive_entry_set_pathname (entry, filename_basename);

	/* ._BIG FAT BUG_. We should not have to do this, as it should be
	 * set from archive_entry_copy_stat() */
	archive_entry_set_size (entry, st.st_size);

	/* write header */
	retval = archive_write_header (arch, entry);
	if (retval != ARCHIVE_OK) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_CREATE,
				      "failed to write header: %s\n", archive_error_string (arch));
		goto out;
	}

	/* open file to copy */
	fd = open (filename, O_RDONLY);
	if (fd < 0) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_CREATE,
				      "failed to get fd for %s", filename);
		goto out;
	}

	/* ITS4: ignore, buffer statically preallocated  */
	len = read (fd, buff, sizeof (buff));
	/* write data to archive -- how come no convenience function? */
	while (len > 0) {
		wrote = archive_write_data (arch, buff, len);
		if (wrote != len)
			egg_warning("wrote %i instead of %i\n", wrote, len);
		/* ITS4: ignore, buffer statically preallocated  */
		len = read (fd, buff, sizeof (buff));
	}
	ret = TRUE;
out:
	if (fd >= 0)
		close (fd);
	if (entry != NULL)
		archive_entry_free (entry);
	g_free (filename_basename);
	return ret;
}

/**
 * pk_service_pack_create_from_files:
 **/
static gboolean
pk_service_pack_create_from_files (PkServicePack *pack, GPtrArray *file_array, GError **error)
{
	struct archive *arch = NULL;
	gboolean ret = FALSE;
	const gchar *src;
	guint i;
	gchar *filename;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (file_array != NULL, FALSE);
	g_return_val_if_fail (error != NULL, FALSE);

	/* create a file with metadata in it */
	filename = g_build_filename (g_get_tmp_dir (), "metadata.conf", NULL);
	ret = pk_service_pack_create_metadata_file (pack, filename);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_CREATE,
				      "failed to generate metadata file %s", filename);
		goto out;
	}
	g_ptr_array_add (file_array, g_strdup (filename));

	/* we can only write tar achives */
	arch = archive_write_new ();
	archive_write_set_compression_none (arch);
	archive_write_set_format_ustar (arch);
	archive_write_open_filename (arch, pack->priv->filename);

	/* for each filename */
	for (i=0; i<file_array->len; i++) {
		src = (const gchar *) g_ptr_array_index (file_array, i);
		/* try to add to archive */
		ret = pk_service_pack_archive_add_file (arch, src, error);
		if (!ret)
			goto out;
	}

	/* completed all okay */
	ret = TRUE;
out:
	g_free (filename);
	/* delete each filename */
	for (i=0; i<file_array->len; i++) {
		src = (const gchar *) g_ptr_array_index (file_array, i);
		g_remove (src);
	}

	/* close the archive */
	if (arch != NULL) {
		archive_write_close (arch);
		archive_write_finish (arch);
	}
	return ret;
}
#else
/**
 * pk_service_pack_create_from_files:
 **/
static gboolean
pk_service_pack_create_from_files (PkServicePack *pack, GPtrArray *file_array, GError **error)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_CREATE,
			      "The service pack %s cannot be created as PackageKit was not built with libarchive support", pack->priv->filename);
	return FALSE;
}
#endif

/**
 * pk_service_pack_scan_files_in_directory:
 **/
static GPtrArray *
pk_service_pack_scan_files_in_directory (PkServicePack *pack)
{
	gchar *src;
	GPtrArray *file_array = NULL;
	GDir *dir;
	const gchar *filename;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), NULL);
	g_return_val_if_fail (pack->priv->directory != NULL, NULL);

	/* try and open the directory */
	dir = g_dir_open (pack->priv->directory, 0, NULL);
	if (dir == NULL) {
		egg_warning ("failed to get directory for %s", pack->priv->directory);
		goto out;
	}

	/* add each file to an array */
	file_array = g_ptr_array_new ();
	while ((filename = g_dir_read_name (dir))) {
		src = g_build_filename (pack->priv->directory, filename, NULL);
		g_ptr_array_add (file_array, src);
	}
	g_dir_close (dir);
out:
	return file_array;
}

/**
 * pk_service_pack_package_cb:
 **/
static void
pk_service_pack_package_cb (PkClient *client, const PkPackageObj *obj, PkServicePack *pack)
{
	g_return_if_fail (PK_IS_SERVICE_PACK (pack));
	g_return_if_fail (obj != NULL);

	/* only shown downloading */
	if (obj->info != PK_INFO_ENUM_DOWNLOADING)
		return;

	egg_debug ("emit package %s", obj->id->name);
	g_signal_emit (pack, signals [PK_SERVICE_PACK_PACKAGE], 0, obj);
}

/**
 * pk_service_pack_progress_changed_cb:
 **/
static void
pk_service_pack_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				     guint elapsed, guint remaining, PkServicePack *pack)
{
	egg_debug ("emit percentage %i", percentage);
	g_signal_emit (pack, signals [PK_SERVICE_PACK_PERCENTAGE], 0, percentage);
}

/**
 * pk_service_pack_setup_client:
 **/
static gboolean
pk_service_pack_setup_client (PkServicePack *pack)
{
	if (pack->priv->client != NULL)
		return FALSE;
	pack->priv->client = pk_client_new ();
	g_signal_connect (pack->priv->client, "package",
			  G_CALLBACK (pk_service_pack_package_cb), pack);
	g_signal_connect (pack->priv->client, "progress-changed",
			  G_CALLBACK (pk_service_pack_progress_changed_cb), pack);
	pk_client_set_use_buffer (pack->priv->client, TRUE, NULL);
	pk_client_set_synchronous (pack->priv->client, TRUE, NULL);
	return TRUE;
}

/**
 * pk_service_pack_create_for_package_ids_internal:
 **/
static gboolean
pk_service_pack_create_for_package_ids_internal (PkServicePack *pack, gchar **package_ids, GError **error)
{
	gchar **package_ids_deps = NULL;
	PkPackageList *list = NULL;
	guint length;
	GPtrArray *file_array = NULL;
	GError *error_local = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error != NULL, FALSE);
	g_return_val_if_fail (pack->priv->filename != NULL, FALSE);
	g_return_val_if_fail (pack->priv->directory != NULL, FALSE);

	/* don't setup by default to not block the server */
	pk_service_pack_setup_client (pack);

	/* download this package */
	pk_service_pack_status_changed (pack, PK_SERVICE_PACK_STATUS_DOWNLOAD_PACKAGES);
	ret = pk_service_pack_download_package_ids (pack, package_ids, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, error_local->code,
				      "failed to download main package: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get depends */
	ret = pk_client_reset (pack->priv->client, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed to reset: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	egg_debug ("Getting depends for %s", package_ids[0]);
	ret = pk_client_get_depends (pack->priv->client, PK_FILTER_ENUM_NONE, package_ids, TRUE, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed to get depends: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the deps */
	list = pk_client_get_package_list (pack->priv->client);

	/* remove some deps */
	pk_package_list_set_fuzzy_arch (list, TRUE);
	pk_service_pack_exclude_packages (pack, list);

	/* get the deps */
	length = pk_package_list_get_size (list);
	if (length != 0) {
		/* download additional package_ids */
		package_ids_deps = pk_package_list_to_strv (list);
		pk_service_pack_status_changed (pack, PK_SERVICE_PACK_STATUS_DOWNLOAD_DEPENDENCIES);
		ret = pk_service_pack_download_package_ids (pack, package_ids_deps, &error_local);
		g_strfreev (package_ids_deps);

		/* failed to get deps */
		if (!ret) {
			*error = g_error_new (PK_SERVICE_PACK_ERROR, error_local->code,
					      "failed to download deps of package: %s", error_local->message);
			g_error_free (error_local);
			goto out;
		}
	}

	/* find packages that were downloaded */
	file_array = pk_service_pack_scan_files_in_directory (pack);
	if (file_array == NULL) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed to scan directory: %s", pack->priv->directory);
		goto out;
	}

	/* generate pack file */
	ret = pk_service_pack_create_from_files (pack, file_array, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, error_local->code,
				      "failed to create archive: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

out:
	if (list != NULL)
		g_object_unref (list);
	if (file_array != NULL) {
		g_ptr_array_foreach (file_array, (GFunc) g_free, NULL);
		g_ptr_array_free (file_array, TRUE);
	}
	return ret;
}

/**
 * pk_service_pack_create_for_package_ids:
 * @pack: a valid #PkServicePack instance
 * @package_ids: A list of package_ids to download
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Adds the packages specified to a service pack.
 *
 * Return value: %TRUE if the service pack was created successfully
 **/
gboolean
pk_service_pack_create_for_package_ids (PkServicePack *pack, gchar **package_ids, GError **error)
{
	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);

	pack->priv->type = PK_SERVICE_PACK_TYPE_INSTALL;
	return pk_service_pack_create_for_package_ids_internal (pack, package_ids, error);
}

/**
 * pk_service_pack_create_for_package_id:
 * @pack: a valid #PkServicePack instance
 * @package_id: A single package_id to download
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Adds the package specified to a service pack.
 *
 * Return value: %TRUE if the service pack was created successfully
 **/
gboolean
pk_service_pack_create_for_package_id (PkServicePack *pack, const gchar *package_id, GError **error)
{
	gchar **package_ids;
	gboolean ret;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (error != NULL, FALSE);
	g_return_val_if_fail (pack->priv->filename != NULL, FALSE);
	g_return_val_if_fail (pack->priv->directory != NULL, FALSE);

	package_ids = pk_package_ids_from_id (package_id);
	pack->priv->type = PK_SERVICE_PACK_TYPE_INSTALL;
	ret = pk_service_pack_create_for_package_ids_internal (pack, package_ids, error);
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_service_pack_create_for_updates:
 * @pack: a valid #PkServicePack instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Adds any pending updates to a service pack.
 *
 * Return value: %TRUE if the service pack was created successfully
 **/
gboolean
pk_service_pack_create_for_updates (PkServicePack *pack, GError **error)
{
	gchar **package_ids = NULL;
	GError *error_local = NULL;
	gboolean ret = FALSE;
	PkPackageList *list;
	guint len;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);
	g_return_val_if_fail (error != NULL, FALSE);
	g_return_val_if_fail (pack->priv->filename != NULL, FALSE);
	g_return_val_if_fail (pack->priv->directory != NULL, FALSE);

	/* don't setup by default to not block the server */
	pk_service_pack_setup_client (pack);

	/* get updates */
	ret = pk_client_reset (pack->priv->client, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed to reset: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	egg_debug ("Getting updates");
	ret = pk_client_get_updates (pack->priv->client, PK_FILTER_ENUM_NONE, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed to get updates: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the updates, and download them with deps */
	list = pk_client_get_package_list (pack->priv->client);
	len = pk_package_list_get_size (list);

	/* no updates */
	if (len == 0) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_NOTHING_TO_DO,
				      "there are no updates to download");
		ret = FALSE;
		goto out;
	}

	package_ids = pk_package_list_to_strv (list);
	g_object_unref (list);

	pack->priv->type = PK_SERVICE_PACK_TYPE_UPDATE;
	ret = pk_service_pack_create_for_package_ids_internal (pack, package_ids, error);
out:
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_service_pack_cancel:
 * @pack: a valid #PkServicePack instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Cancels the pack creation
 *
 * Return value: %TRUE if we cancelled okay
 **/
gboolean
pk_service_pack_cancel (PkServicePack *pack, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_SERVICE_PACK (pack), FALSE);

	/* nothing to cancel */
	if (pack->priv->client == NULL)
		return TRUE;

	/* cancel the client */
	egg_debug ("cancelling the client");
	ret = pk_client_cancel (pack->priv->client, &error_local);
	if (!ret) {
		*error = g_error_new (PK_SERVICE_PACK_ERROR, PK_SERVICE_PACK_ERROR_FAILED_SETUP,
				      "failed to cancel: %s", error_local->message);
		g_error_free (error_local);
	}
	return ret;
}

/**
 * pk_service_pack_finalize:
 **/
static void
pk_service_pack_finalize (GObject *object)
{
	PkServicePack *pack;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_SERVICE_PACK (object));
	pack = PK_SERVICE_PACK (object);

	if (pack->priv->exclude_list != NULL)
		g_object_unref (pack->priv->exclude_list);
	if (pack->priv->client != NULL)
		g_object_unref (pack->priv->client);
	g_free (pack->priv->directory);
	g_free (pack->priv->filename);

	G_OBJECT_CLASS (pk_service_pack_parent_class)->finalize (object);
}

/**
 * pk_service_pack_class_init:
 **/
static void
pk_service_pack_class_init (PkServicePackClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = pk_service_pack_finalize;

	/**
	 * PkServicePack::package:
	 * @pack: the #PkServicePack instance that emitted the signal
	 * @obj: the #PkPackageObj that has just been downloaded
	 *
	 * The ::package signal is emitted when a file is being downloaded.
	 **/
	signals [PK_SERVICE_PACK_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkServicePackClass, package),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);

	/**
	 * PkServicePack::percentage:
	 * @pack: the #PkServicePack instance that emitted the signal
	 * @percentage: the percentage complete
	 *
	 * The ::package signal is emitted when a file is being downloaded.
	 **/
	signals [PK_SERVICE_PACK_PERCENTAGE] =
		g_signal_new ("percentage",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkServicePackClass, percentage),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	/**
	 * PkServicePack::status:
	 * @pack: the #PkServicePack instance that emitted the signal
	 * @status: the #PkPackageObj that has just been downloaded
	 *
	 * The %PkServicePackStatus enum describing what we are doing
	 **/
	signals [PK_SERVICE_PACK_STATUS] =
		g_signal_new ("status",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkServicePackClass, status),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);

	g_type_class_add_private (klass, sizeof (PkServicePackPrivate));
}

/**
 * pk_service_pack_init:
 **/
static void
pk_service_pack_init (PkServicePack *pack)
{
	pack->priv = PK_SERVICE_PACK_GET_PRIVATE (pack);
	pack->priv->exclude_list = NULL;
	pack->priv->filename = NULL;
	pack->priv->directory = NULL;
	pack->priv->type = PK_SERVICE_PACK_TYPE_UNKNOWN;
}

/**
 * pk_service_pack_new:
 *
 * Return value: A new service_pack class instance.
 **/
PkServicePack *
pk_service_pack_new (void)
{
	PkServicePack *pack;
	pack = g_object_new (PK_TYPE_SERVICE_PACK, NULL);
	return PK_SERVICE_PACK (pack);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_service_pack_test (EggTest *test)
{
	PkServicePack *pack;

	if (!egg_test_start (test, "PkServicePack"))
		return;

	/************************************************************/
	egg_test_title (test, "get an instance");
	pack = pk_service_pack_new ();
	egg_test_assert (test, pack != NULL);
	g_object_unref (pack);

	egg_test_end (test);
}
#endif

