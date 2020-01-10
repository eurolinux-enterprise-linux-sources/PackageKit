/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Grzegorz Dąbrowski <gdx@o2.pl>
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

#include <gmodule.h>
#include <glib.h>
#include <string.h>
#include <unistd.h>
#include <pk-backend.h>
#include <egg-debug.h>

#include <sqlite3.h>
#include <libbox/libbox-db.h>
#include <libbox/libbox-db-utils.h>
#include <libbox/libbox-db-repos.h>
#include <libbox/libbox-repos.h>
#include <libbox/libbox.h>

#define ROOT_DIRECTORY "/"

enum PkgSearchType {
	SEARCH_TYPE_NAME = 0,
	SEARCH_TYPE_DETAILS = 1,
	SEARCH_TYPE_FILE = 2,
	SEARCH_TYPE_RESOLVE = 3
};

enum DepsType {
	DEPS_TYPE_DEPENDS = 0,
	DEPS_TYPE_REQUIRES = 1
};

enum DepsBehaviour {
	DEPS_ALLOW = 0,
	DEPS_NO_ALLOW = 1
};

static sqlite3*
db_open()
{
	sqlite3 *db;

	db = box_db_open(ROOT_DIRECTORY);
	box_db_attach_repos(db, ROOT_DIRECTORY);
	box_db_repos_init(db);

	return db;
}

static void
db_close(sqlite3 *db)
{
	box_db_detach_repos(db);
	box_db_close(db);
}

static void
common_progress(int value, gpointer user_data)
{
	PkBackend* backend = (PkBackend *) user_data;
	pk_backend_set_percentage (backend, value);
}

static void
add_packages_from_list (PkBackend *backend, GList *list, gboolean updates)
{
	PackageSearch *package = NULL;
	GList *li = NULL;
	gchar *pkg_string = NULL;
	PkInfoEnum info;

	for (li = list; li != NULL; li = li->next) {
		package = (PackageSearch*)li->data;
		pkg_string = pk_package_id_build(package->package, package->version, package->arch, package->reponame);
		if (updates == TRUE)
			info = PK_INFO_ENUM_NORMAL;
		else if (package->installed)
			info = PK_INFO_ENUM_INSTALLED;
		else
			info = PK_INFO_ENUM_AVAILABLE;
		pk_backend_package (backend, info, pkg_string, package->description);
		g_free(pkg_string);
	}
}

static gboolean
backend_find_packages_thread (PkBackend *backend)
{
	PkBitfield filters;
	const gchar *search;
	guint mode;
	GList *list = NULL;
	sqlite3 *db = NULL;
	gint filter_box = 0;

	filters = pk_backend_get_uint (backend, "filters");
	mode = pk_backend_get_uint (backend, "mode");
	search = pk_backend_get_string (backend, "search");

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
		filter_box = filter_box | PKG_INSTALLED;
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
		filter_box = filter_box | PKG_AVAILABLE;
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_DEVELOPMENT)) {
		filter_box = filter_box | PKG_DEVEL;
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_DEVELOPMENT)) {
		filter_box = filter_box | PKG_NON_DEVEL;
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_GUI)) {
		filter_box = filter_box | PKG_GUI;
	}
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_GUI)) {
		filter_box = filter_box | PKG_TEXT;
	}
	if (mode == SEARCH_TYPE_DETAILS) {
		filter_box = filter_box | PKG_SEARCH_DETAILS;
	}

	pk_backend_set_percentage (backend, PK_BACKEND_PERCENTAGE_INVALID);

	db = db_open();

	if (mode == SEARCH_TYPE_FILE) {
		list = box_db_repos_search_file_with_filter (db, search, filter_box);
		add_packages_from_list (backend, list, FALSE);
		box_db_repos_package_list_free (list);
	} else if (mode == SEARCH_TYPE_RESOLVE) {
		list = box_db_repos_packages_search_one (db, (gchar *)search);
		add_packages_from_list (backend, list, FALSE);
		box_db_repos_package_list_free (list);
	} else {
		if ((pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) &&
		     pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) ||
		    (!pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) &&
		     !pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED))) {
			list = box_db_repos_packages_search_all(db, (gchar *)search, filter_box);
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED)) {
			list = box_db_repos_packages_search_installed(db, (gchar *)search, filter_box);
		} else if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED)) {
			list = box_db_repos_packages_search_available(db, (gchar *)search, filter_box);
		}
		add_packages_from_list (backend, list, FALSE);
		box_db_repos_package_list_free (list);
	}

	db_close(db);
	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
backend_get_updates_thread (PkBackend *backend)
{
	GList *list = NULL;
	sqlite3 *db = NULL;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	db = db_open ();

	list = box_db_repos_packages_for_upgrade (db);
	add_packages_from_list (backend, list, TRUE);
	box_db_repos_package_list_free (list);

	db_close (db);
	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
backend_update_system_thread (PkBackend *backend)
{
	/* FIXME: support only_trusted */

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	box_upgrade_dist(ROOT_DIRECTORY, common_progress, backend);
	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
backend_install_packages_thread (PkBackend *backend)
{
	gboolean result;
	PkPackageId *pi;
	gchar **package_ids;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	/* FIXME: support only_trusted */

	package_id = pk_backend_get_string (backend, "package_id");
	pi = pk_package_id_new_from_string (package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_package_id_free (pi);
		return FALSE;
	}
	result = box_package_install(pi->name, ROOT_DIRECTORY, common_progress, backend, FALSE);

	pk_backend_finished (backend);

	return result;
}

static gboolean
backend_update_packages_thread (PkBackend *backend)
{
	gboolean result = TRUE;
	gchar **package_ids;
	PkPackageId *pi;
	gint i;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);
	/* FIXME: support only_trusted */
	package_ids = pk_backend_get_strv (backend, "package_ids");

	for (i = 0; i < g_strv_length (package_ids); i++)
	{
		pi = pk_package_id_new_from_string (package_ids[i]);
		if (pi == NULL) {
			pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
			pk_package_id_free (pi);
			g_strfreev (package_ids);

			return FALSE;
		}
		result |= box_package_install(pi->name, ROOT_DIRECTORY, common_progress, backend, FALSE);
	}

	pk_backend_finished (backend);
	return result;
}

static gboolean
backend_install_files_thread (PkBackend *backend)
{
	gboolean result;
	gchar **full_paths;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	full_paths = pk_backend_get_strv (backend, "full_paths");
	result = box_package_install(full_paths[0], ROOT_DIRECTORY, common_progress, backend, FALSE);

	pk_backend_finished (backend);

	return result;
}

static gboolean
backend_get_details_thread (PkBackend *backend)
{
	PkPackageId *pi;
	PackageSearch *ps;
	GList *list;
	sqlite3 *db;
	gchar **package_ids;

	package_id = pk_backend_get_string (backend, "package_id");
	db = db_open();

	pi = pk_package_id_new_from_string (package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_package_id_free (pi);
		db_close (db);

		return FALSE;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	/* only one element is returned */
	list = box_db_repos_packages_search_by_data(db, pi->name, pi->version);

	if (list == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "cannot find package by id");
		pk_package_id_free (pi);
		db_close (db);
		return FALSE;
	}
	ps = (PackageSearch*) list->data;

	pk_backend_details (backend, package_id, "unknown", PK_GROUP_ENUM_OTHER, ps->description, "", 0);

	pk_package_id_free (pi);
	box_db_repos_package_list_free (list);

	db_close(db);
	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
backend_get_files_thread (PkBackend *backend)
{
	PkPackageId *pi;
	gchar *files;
	sqlite3 *db;
	gchar **package_ids;

	db = db_open();
	package_id = pk_backend_get_string (backend, "package_id");

	pi = pk_package_id_new_from_string (package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		pk_package_id_free (pi);
		db_close (db);

		return FALSE;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	files = box_db_repos_get_files_string (db, pi->name, pi->version);
	pk_backend_files (backend, package_id, files);

	pk_package_id_free (pi);
	db_close(db);
	g_free (files);

	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
backend_get_depends_requires_thread (PkBackend *backend)
{
	PkPackageId *pi;
	GList *list = NULL;
	sqlite3 *db;
	gchar **package_ids;
	int deps_type;

	db = db_open ();
	package_id = pk_backend_get_string (backend, "package_id");
	deps_type = pk_backend_get_uint (backend, "type");

	pi = pk_package_id_new_from_string (package_id);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");
		db_close (db);

		return FALSE;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	if (deps_type == DEPS_TYPE_DEPENDS)
		list = box_db_repos_get_depends(db, pi->name);
	else if (deps_type == DEPS_TYPE_REQUIRES)
		list = box_db_repos_get_requires(db, pi->name);

	add_packages_from_list (backend, list, FALSE);
	box_db_repos_package_list_free (list);
	pk_package_id_free (pi);

	db_close (db);

	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
backend_remove_packages_thread (PkBackend *backend)
{
	PkPackageId *pi;
	gchar **package_ids;

	package_ids = pk_backend_get_strv (backend, "package_ids");
	pi = pk_package_id_new_from_string (package_ids[0]);
	if (pi == NULL) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_PACKAGE_ID_INVALID, "invalid package id");

		return FALSE;
	}

	pk_backend_set_status (backend, PK_STATUS_ENUM_REMOVE);

	if (!box_package_uninstall (pi->name, ROOT_DIRECTORY, common_progress, backend, FALSE))
	{
		pk_backend_error_code (backend, PK_ERROR_ENUM_DEP_RESOLUTION_FAILED, "Cannot uninstall");
	}

	pk_package_id_free (pi);
	pk_backend_finished (backend);
	return TRUE;
}

static gboolean
backend_refresh_cache_thread (PkBackend *backend)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_REFRESH_CACHE);

	box_repos_sync(ROOT_DIRECTORY, common_progress, backend);
	pk_backend_finished (backend);
	return TRUE;
}

/* ===================================================================== */

/**
 * backend_initialize:
 */
static void
backend_initialize (PkBackend *backend)
{
}

/**
 * backend_destroy:
 */
static void
backend_destroy (PkBackend *backend)
{
}

/**
 * backend_get_filters:
 */
static PkBitfield
backend_get_filters (PkBackend *backend)
{
	return pk_bitfield_from_enums (
		PK_FILTER_ENUM_GUI,
		PK_FILTER_ENUM_INSTALLED,
		PK_FILTER_ENUM_DEVELOPMENT,
		-1);
}

/**
 * backend_get_depends:
 */
static void
backend_get_depends (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_set_uint (backend, "type", DEPS_TYPE_DEPENDS);
	pk_backend_thread_create (backend, backend_get_depends_requires_thread);
}

/**
 * backend_get_details:
 */
static void
backend_get_details (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_get_details_thread);
}

/**
 * backend_get_files:
 */
static void
backend_get_files (PkBackend *backend, gchar **package_ids)
{
	pk_backend_thread_create (backend, backend_get_files_thread);
}

/**
 * backend_get_requires:
 */
static void
backend_get_requires (PkBackend *backend, PkBitfield filters, gchar **package_ids, gboolean recursive)
{
	pk_backend_set_uint (backend, "type", DEPS_TYPE_REQUIRES);
	pk_backend_thread_create (backend, backend_get_depends_requires_thread);
}

/**
 * backend_get_updates:
 */
static void
backend_get_updates (PkBackend *backend, PkBitfield filters)
{
	pk_backend_thread_create (backend, backend_get_updates_thread);
}

/**
 * backend_install_packages:
 */
static void
backend_install_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot install when offline");
		pk_backend_finished (backend);
		return;
	}

	pk_backend_thread_create (backend, backend_install_packages_thread);
}

/**
 * backend_install_files:
 */
static void
backend_install_files (PkBackend *backend, gboolean only_trusted, gchar **files)
{
	pk_backend_thread_create (backend, backend_install_files_thread);
}

/**
 * backend_refresh_cache:
 */
static void
backend_refresh_cache (PkBackend *backend, gboolean force)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot refresh cache whilst offline");
		pk_backend_finished (backend);
		return;
	}
	pk_backend_thread_create (backend, backend_refresh_cache_thread);
}

/**
 * backend_remove_packages:
 */
static void
backend_remove_packages (PkBackend *backend, gchar **package_id, gboolean allow_deps, gboolean autoremove)
{
	pk_backend_set_uint (backend, "type", DEPS_ALLOW);
	pk_backend_thread_create (backend, backend_remove_packages_thread);
}

/**
 * backend_resolve:
 */
static void
backend_resolve (PkBackend *backend, PkBitfield filters, const gchar *package)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_RESOLVE);
	pk_backend_thread_create (backend, backend_find_packages_thread);
}

/**
 * backend_search_details:
 */
static void
backend_search_details (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_DETAILS);
	pk_backend_thread_create (backend, backend_find_packages_thread);
}

/**
 * backend_search_file:
 */
static void
backend_search_file (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_FILE);
	pk_backend_thread_create (backend, backend_find_packages_thread);
}

/**
 * backend_search_name:
 */
static void
backend_search_name (PkBackend *backend, PkBitfield filters, const gchar *search)
{
	pk_backend_set_uint (backend, "mode", SEARCH_TYPE_NAME);
	pk_backend_thread_create (backend, backend_find_packages_thread);
}

/**
 * backend_update_packages:
 */
static void
backend_update_packages (PkBackend *backend, gboolean only_trusted, gchar **package_ids)
{
	/* check network state */
	if (!pk_backend_is_online (backend)) {
		pk_backend_error_code (backend, PK_ERROR_ENUM_NO_NETWORK, "Cannot update when offline");
		pk_backend_finished (backend);
		return;
	}
	pk_backend_thread_create (backend, backend_update_packages_thread);
}

/**
 * backend_update_system:
 */
static void
backend_update_system (PkBackend *backend, gboolean only_trusted)
{
	pk_backend_thread_create (backend, backend_update_system_thread);
}

/**
 * backend_get_repo_list:
 */
static void
backend_get_repo_list (PkBackend *backend, PkBitfield filters)
{
	GList *list;
	GList *li;
	RepoInfo *repo;

	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	list = box_repos_list_get ();
	for (li = list; li != NULL; li=li->next)
	{
		repo = (RepoInfo*) li->data;
		pk_backend_repo_detail (backend, repo->name, repo->description, repo->enabled);
	}
	box_repos_list_free (list);

	pk_backend_finished (backend);
}

/**
 * backend_repo_enable:
 */
static void
backend_repo_enable (PkBackend *backend, const gchar *rid, gboolean enabled)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	box_repos_enable_repo(rid, enabled);

	pk_backend_finished (backend);
}

/**
 * backend_repo_set_data:
 */
static void
backend_repo_set_data (PkBackend *backend, const gchar *rid, const gchar *parameter, const gchar *value)
{
	pk_backend_set_status (backend, PK_STATUS_ENUM_QUERY);

	if (!box_repos_set_param (rid, parameter, value)) {
		egg_warning ("Cannot set PARAMETER '%s' TO '%s' for REPO '%s'", parameter, value, rid);
	}

	pk_backend_finished (backend);
}

PK_BACKEND_OPTIONS (
	"Box",					/* description */
	"Grzegorz Dąbrowski <grzegorz.dabrowski@gmail.com>",	/* author */
	backend_initialize,			/* initalize */
	backend_destroy,			/* destroy */
	NULL,					/* get_groups */
	backend_get_filters,			/* get_filters */
	NULL,					/* get_roles */
	NULL,					/* get_mime_types */
	NULL,					/* cancel */
	NULL,					/* download_packages */
	NULL,					/* get_categories */
	backend_get_depends,			/* get_depends */
	backend_get_details,			/* get_details */
	NULL,					/* get_distro_upgrades */
	backend_get_files,			/* get_files */
	NULL,					/* get_packages */
	backend_get_repo_list,			/* get_repo_list */
	backend_get_requires,			/* get_requires */
	NULL,					/* get_update_detail */
	backend_get_updates,			/* get_updates */
	backend_install_files,			/* install_files */
	backend_install_packages,		/* install_packages */
	NULL,					/* install_signature */
	backend_refresh_cache,			/* refresh_cache */
	backend_remove_packages,		/* remove_packages */
	backend_repo_enable,			/* repo_enable */
	backend_repo_set_data,			/* repo_set_data */
	backend_resolve,			/* resolve */
	NULL,					/* rollback */
	backend_search_details,			/* search_details */
	backend_search_file,			/* search_file */
	NULL,					/* search_group */
	backend_search_name,			/* search_name */
	backend_update_packages,		/* update_packages */
	backend_update_system,			/* update_system */
	NULL,					/* what_provides */
	NULL,					/* simulate_install_files */
	NULL,					/* simulate_install_packages */
	NULL,					/* simulate_remove_packages */
	NULL					/* simulate_update_packages */
);

