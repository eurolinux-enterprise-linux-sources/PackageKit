/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2008 Richard Hughes <richard@hughsie.com>
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

/**
 * SECTION:pk-client
 * @short_description: GObject class for PackageKit client access
 *
 * A nice GObject to use for accessing PackageKit asynchronously
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <string.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <dbus/dbus-glib.h>

#include <packagekit-glib/pk-enum.h>
#include <packagekit-glib/pk-bitfield.h>
#include <packagekit-glib/pk-client.h>
#include <packagekit-glib/pk-connection.h>
#include <packagekit-glib/pk-package-id.h>
#include <packagekit-glib/pk-package-ids.h>
#include <packagekit-glib/pk-package-list.h>
#include <packagekit-glib/pk-marshal.h>
#include <packagekit-glib/pk-common.h>
#include <packagekit-glib/pk-control.h>
#include <packagekit-glib/pk-update-detail-obj.h>
#include <packagekit-glib/pk-details-obj.h>
#include <packagekit-glib/pk-transaction-obj.h>
#include <packagekit-glib/pk-category-obj.h>
#include <packagekit-glib/pk-distro-upgrade-obj.h>
#include <packagekit-glib/pk-require-restart-obj.h>
#include <packagekit-glib/pk-obj-list.h>

#include "egg-debug.h"
#include "egg-string.h"

static void     pk_client_finalize		(GObject	*object);
static gboolean	pk_client_disconnect_proxy	(PkClient	*client);

#define PK_CLIENT_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CLIENT, PkClientPrivate))

/**
 * PkClientPrivate:
 *
 * Private #PkClient data
 **/
struct _PkClientPrivate
{
	DBusGConnection		*connection;
	DBusGProxy		*proxy;
	GMainLoop		*loop;
	gboolean		 is_finished;
	gboolean		 is_finishing;
	gboolean		 use_buffer;
	gboolean		 synchronous;
	gchar			*tid;
	PkControl		*control;
	PkObjList		*category_list;
	PkObjList		*distro_upgrade_list;
	PkObjList		*transaction_list;
	PkObjList		*require_restart_list;
	PkPackageList		*package_list;
	PkConnection		*pconnection;
	gulong			 pconnection_signal_id;
	PkRestartEnum		 require_restart;
	PkStatusEnum		 status;
	PkRoleEnum		 role;
	gboolean		 cached_force;
	gboolean		 cached_allow_deps;
	gboolean		 cached_autoremove;
	gboolean		 cached_only_trusted;
	gchar			*cached_package_id;
	gchar			**cached_package_ids;
	gchar			*cached_transaction_id;
	gchar			*cached_key_id;
	gchar			*cached_full_path;
	gchar			**cached_full_paths;
	gchar			*cached_search;
	gchar			*cached_directory;
	PkProvidesEnum		 cached_provides;
	PkBitfield		 cached_filters;
	gint			 timeout;
	guint			 timeout_id;
	PkExitEnum		 exit;
	GError			*error;
};

enum {
	SIGNAL_DETAILS,
	SIGNAL_ERROR_CODE,
	SIGNAL_FILES,
	SIGNAL_FINISHED,
	SIGNAL_PACKAGE,
	SIGNAL_PROGRESS_CHANGED,
	SIGNAL_REQUIRE_RESTART,
	SIGNAL_MESSAGE,
	SIGNAL_TRANSACTION,
	SIGNAL_DISTRO_UPGRADE,
	SIGNAL_STATUS_CHANGED,
	SIGNAL_UPDATE_DETAIL,
	SIGNAL_REPO_SIGNATURE_REQUIRED,
	SIGNAL_EULA_REQUIRED,
	SIGNAL_CALLER_ACTIVE_CHANGED,
	SIGNAL_REPO_DETAIL,
	SIGNAL_ALLOW_CANCEL,
	SIGNAL_CATEGORY,
	SIGNAL_DESTROY,
	SIGNAL_MEDIA_CHANGE_REQUIRED,
	SIGNAL_LAST
};

enum {
	PROP_0,
	PROP_ROLE,
	PROP_STATUS,
	PROP_EXIT,
	PROP_LAST,
};

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkClient, pk_client, G_TYPE_OBJECT)

/**
 * pk_client_error_quark:
 *
 * Return value: Our personal error quark.
 **/
GQuark
pk_client_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("pk_client_error");
	return quark;
}

/**
 * pk_client_error_get_type:
 **/
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }
GType
pk_client_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] =
		{
			ENUM_ENTRY (PK_CLIENT_ERROR_FAILED, "Failed"),
			ENUM_ENTRY (PK_CLIENT_ERROR_FAILED_AUTH, "FailedAuth"),
			ENUM_ENTRY (PK_CLIENT_ERROR_NO_TID, "NoTid"),
			ENUM_ENTRY (PK_CLIENT_ERROR_ALREADY_TID, "AlreadyTid"),
			ENUM_ENTRY (PK_CLIENT_ERROR_ROLE_UNKNOWN, "RoleUnknown"),
			ENUM_ENTRY (PK_CLIENT_ERROR_CANNOT_START_DAEMON, "CannotStartDaemon"),
			ENUM_ENTRY (PK_CLIENT_ERROR_INVALID_INPUT, "InvalidInput"),
			ENUM_ENTRY (PK_CLIENT_ERROR_INVALID_FILE, "InvalidFile"),
			ENUM_ENTRY (PK_CLIENT_ERROR_NOT_SUPPORTED, "NotSupported"),
			{ 0, NULL, NULL }
		};
		etype = g_enum_register_static ("PkClientError", values);
	}
	return etype;
}

/******************************************************************************
 *                    LOCAL FUNCTIONS
 ******************************************************************************/

/**
 * pk_client_error_fixup:
 * @error: a %GError
 **/
static GError *
pk_client_error_fixup (GError *error_local)
{
	GError *error;
	const gchar *name;

	g_return_val_if_fail (error_local != NULL, NULL);

	/* PolicyKit failure */
	if (g_str_has_prefix (error_local->message, "org.freedesktop.packagekit.")) {
		egg_debug ("fixing up code for Policykit auth failure");
		error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED_AUTH, "PolicyKit authorization failure");
		goto out;
	}

	/* new default error with correct domain and code */
	error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "%s", error_local->message);

	/* get some proper debugging */
	if (error_local->domain == DBUS_GERROR &&
	    error_local->code == DBUS_GERROR_REMOTE_EXCEPTION) {
		/* use one of our local codes */
		name = dbus_g_error_get_name (error_local);

		/* trim common prefix */
		if (g_str_has_prefix (name, "org.freedesktop.PackageKit.Transaction."))
			name = &name[39];

		/* try to get a better error */
		if (g_str_has_prefix (name, "PermissionDenied") ||
		    g_str_has_prefix (name, "RefusedByPolicy"))
			error->code = PK_CLIENT_ERROR_FAILED_AUTH;
		else if (g_str_has_prefix (name, "PackageIdInvalid") ||
			 g_str_has_prefix (name, "SearchInvalid") ||
			 g_str_has_prefix (name, "FilterInvalid") ||
			 g_str_has_prefix (name, "InvalidProvide") ||
			 g_str_has_prefix (name, "InputInvalid"))
			error->code = PK_CLIENT_ERROR_INVALID_INPUT;
		else if (g_str_has_prefix (name, "PackInvalid") ||
			 g_str_has_prefix (name, "NoSuchFile") ||
			 g_str_has_prefix (name, "NoSuchDirectory"))
			error->code = PK_CLIENT_ERROR_INVALID_FILE;
		else if (g_str_has_prefix (name, "NotSupported"))
			error->code = PK_CLIENT_ERROR_NOT_SUPPORTED;
	}
out:
	return error;
}

/**
 * pk_client_set_only_trusted:
 * @client: a valid #PkClient instance
 * @only_trusted: only operate on trusted packages
 *
 * Set the trusted mode. This is useful when doing pk_client_requeue()
 *
 * Return value: %TRUE if the mode was set correctly
 **/
gboolean
pk_client_set_only_trusted (PkClient *client, gboolean only_trusted)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);

	client->priv->cached_only_trusted = only_trusted;

	return TRUE;
}

/**
 * pk_client_get_tid:
 * @client: a valid #PkClient instance
 *
 * The %tid is unique for this transaction.
 *
 * Return value: The transaction_id we are using for this client, or %NULL
 **/
gchar *
pk_client_get_tid (PkClient *client)
{
	if (client->priv->tid == NULL)
		return NULL;
	return g_strdup (client->priv->tid);
}

/**
 * pk_client_set_use_buffer:
 * @client: a valid #PkClient instance
 * @use_buffer: if we should use the package buffer
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * If the package buffer is enabled then after the transaction has completed
 * then the package list can be retrieved in one go, rather than processing
 * each package request async.
 * If this is not set true explicitly, then pk_client_get_package_list
 * will always return zero items.
 *
 * This is not forced on as there may be significant overhead if the list
 * contains many hundreds of items.
 *
 * Return value: %TRUE if the package buffer was enabled
 **/
gboolean
pk_client_set_use_buffer (PkClient *client, gboolean use_buffer, GError **error)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* are we doing this without any need? */
	if (client->priv->use_buffer) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "already set use_buffer!");
		return FALSE;
	}

	client->priv->use_buffer = use_buffer;
	return TRUE;
}

/**
 * pk_client_set_synchronous:
 * @client: a valid #PkClient instance
 * @synchronous: if we should do the method synchronous
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * A synchronous mode allows us to listen in all transactions.
 *
 * Return value: %TRUE if the synchronous mode was enabled
 **/
gboolean
pk_client_set_synchronous (PkClient *client, gboolean synchronous, GError **error)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* are we doing this without any need? */
	if ((client->priv->synchronous && synchronous) ||
	    (!client->priv->synchronous && !synchronous)) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "already synchronous : %i!", synchronous);
		return FALSE;
	}

	client->priv->synchronous = synchronous;
	return TRUE;
}

/**
 * pk_client_set_timeout:
 * @client: a valid #PkClient instance
 * @timeout: the timeout in milliseconds, or -1 for disabled
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * A synchronous mode allows us to listen in all transactions.
 *
 * Return value: %TRUE if the timeout mode was set
 **/
gboolean
pk_client_set_timeout (PkClient *client, gint timeout, GError **error)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* are we doing this again without reset? */
	if (client->priv->timeout != -1) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "already set timeout to %i!", client->priv->timeout);
		return FALSE;
	}

	client->priv->timeout = timeout;
	return TRUE;
}

/**
 * pk_client_get_use_buffer:
 * @client: a valid #PkClient instance
 *
 * Are we using a client side package buffer?
 *
 * Return value: %TRUE if the package buffer is enabled
 **/
gboolean
pk_client_get_use_buffer (PkClient *client)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return client->priv->use_buffer;
}

/**
 * pk_client_get_require_restart:
 * @client: a valid #PkClient instance
 *
 * This method returns the 'worst' restart of all the transactions.
 * It is needed as multiple sub-transactions may emit require-restart with
 * different values, and we always want to get the most invasive of all.
 *
 * For instance, if a transaction emits RequireRestart(system) and then
 * RequireRestart(session) then pk_client_get_require_restart will return
 * system as a session restart is implied with a system restart.
 *
 * Return value: a #PkRestartEnum value, e.g. PK_RESTART_ENUM_SYSTEM
 **/
PkRestartEnum
pk_client_get_require_restart (PkClient *client)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	return client->priv->require_restart;
}

/**
 * pk_client_get_require_restart_list:
 * @client: a valid #PkClient instance
 *
 * This method allows a client program to discover what packages
 * caused different require restarts.
 *
 * Return value: a #PkObjList list of #PkRequireRestartObj's or %NULL if not found or invalid
 **/
PkObjList *
pk_client_get_require_restart_list (PkClient *client)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);

	if (!client->priv->use_buffer)
		return NULL;
	return g_object_ref (client->priv->require_restart_list);
}

/**
 * pk_client_get_package_list:
 * @client: a valid #PkClient instance
 *
 * We do not provide access to the internal package list (as it could be being
 * updated) so provide a way to get access to objects here.
 *
 * Return value: The #PkPackageList or %NULL if not found or invalid
 **/
PkPackageList *
pk_client_get_package_list (PkClient *client)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	if (!client->priv->use_buffer)
		return NULL;
	return g_object_ref (client->priv->package_list);
}

/**
 * pk_client_get_category_list:
 * @client: a valid #PkClient instance
 *
 * Return the cached category list
 *
 * Return value: The #PkObjList of #PkCategoryObj's or %NULL if invalid
 **/
PkObjList *
pk_client_get_category_list (PkClient *client)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	if (!client->priv->use_buffer)
		return NULL;
	return g_object_ref (client->priv->category_list);
}

/**
 * pk_client_get_distro_upgrade_list:
 * @client: a valid #PkClient instance
 *
 * Return the cached distro upgrades list
 *
 * Return value: The #PkObjList of #PkDistroUpgradeObj's or %NULL if invalid
 **/
PkObjList *
pk_client_get_distro_upgrade_list (PkClient *client)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	if (!client->priv->use_buffer)
		return NULL;
	return g_object_ref (client->priv->distro_upgrade_list);
}

/**
 * pk_client_get_transaction_list:
 * @client: a valid #PkClient instance
 *
 * Return the cached transactions list
 *
 * Return value: The #PkObjList of cached objects or %NULL if invalid
 **/
PkObjList *
pk_client_get_transaction_list (PkClient *client)
{
	g_return_val_if_fail (PK_IS_CLIENT (client), NULL);
	if (!client->priv->use_buffer)
		return NULL;
	return g_object_ref (client->priv->transaction_list);
}

/**
 * pk_client_destroy_cb:
 */
static void
pk_client_destroy_cb (DBusGProxy *proxy, PkClient *client)
{
	gboolean ret;
	g_return_if_fail (PK_IS_CLIENT (client));

	/* exit our private loop if it's running */
	ret = g_main_loop_is_running (client->priv->loop);
	if (ret && client->priv->synchronous) {
		egg_warning ("quitting loop due to transaction being destroyed");
		g_main_loop_quit (client->priv->loop);
	}

	/* ref in case we unref the PkClient in ::destroy */
	g_object_ref (client);

	egg_debug ("emit destroy %s", client->priv->tid);
	g_signal_emit (client, signals [SIGNAL_DESTROY], 0);

	/* unref what we previously ref'd */
	g_object_unref (client);
}

/**
 * pk_client_finished_cb:
 */
static void
pk_client_finished_cb (DBusGProxy *proxy, const gchar *exit_text, guint runtime, PkClient *client)
{
	g_return_if_fail (PK_IS_CLIENT (client));

	/* ref in case we unref the PkClient in ::finished */
	g_object_ref (client);

	/* stop the timeout timer if running */
	if (client->priv->timeout_id != 0) {
		g_source_remove (client->priv->timeout_id);
		client->priv->timeout_id = 0;
	}

	client->priv->exit = pk_exit_enum_from_text (exit_text);
	egg_debug ("emit finished %s, %i", exit_text, runtime);

	/* only this instance is finished, and do it before the signal so we can reset */
	client->priv->is_finished = TRUE;

	/* we are finishing, so we can detect when we try to do insane things
	 * in the ::Finished() handler */
	client->priv->is_finishing = TRUE;

	g_signal_emit (client, signals [SIGNAL_FINISHED], 0, client->priv->exit, runtime);

	/* done callback */
	client->priv->is_finishing = FALSE;

	/* exit our private loop */
	if (client->priv->synchronous) {
		if (client->priv->exit != PK_EXIT_ENUM_SUCCESS)
			client->priv->error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED,
							   "failed: %s", exit_text);
		g_main_loop_quit (client->priv->loop);
	}

	/* unref what we previously ref'd */
	g_object_unref (client);
}

/**
 * pk_client_progress_changed_cb:
 */
static void
pk_client_progress_changed_cb (DBusGProxy *proxy, guint percentage, guint subpercentage,
			       guint elapsed, guint remaining, PkClient *client)
{
	g_return_if_fail (PK_IS_CLIENT (client));

	egg_debug ("emit progress-changed %i, %i, %i, %i", percentage, subpercentage, elapsed, remaining);
	g_signal_emit (client , signals [SIGNAL_PROGRESS_CHANGED], 0,
		       percentage, subpercentage, elapsed, remaining);
}

/**
 * pk_client_change_status:
 */
static void
pk_client_change_status (PkClient *client, PkStatusEnum status)
{
	egg_debug ("emit status-changed %s", pk_status_enum_to_text (status));
	g_signal_emit (client , signals [SIGNAL_STATUS_CHANGED], 0, status);
	client->priv->status = status;
}

/**
 * pk_client_status_changed_cb:
 */
static void
pk_client_status_changed_cb (DBusGProxy *proxy, const gchar *status_text, PkClient *client)
{
	PkStatusEnum status;

	g_return_if_fail (PK_IS_CLIENT (client));

	status = pk_status_enum_from_text (status_text);
	pk_client_change_status (client, status);
}

/**
 * pk_client_package_cb:
 */
static void
pk_client_package_cb (DBusGProxy   *proxy,
		      const gchar  *info_text,
		      const gchar  *package_id,
		      const gchar  *summary,
		      PkClient     *client)
{
	PkInfoEnum info;
	PkPackageId *id;
	PkPackageObj *obj;

	g_return_if_fail (PK_IS_CLIENT (client));

	info = pk_info_enum_from_text (info_text);
	id = pk_package_id_new_from_string (package_id);
	obj = pk_package_obj_new (info, id, summary);

	egg_debug ("emit package %s, %s, %s", info_text, package_id, summary);
	g_signal_emit (client , signals [SIGNAL_PACKAGE], 0, obj);

	/* cache */
	if (client->priv->use_buffer || client->priv->synchronous)
		pk_obj_list_add (PK_OBJ_LIST(client->priv->package_list), obj);
	pk_package_id_free (id);
	pk_package_obj_free (obj);
}

/**
 * pk_client_transaction_cb:
 */
static void
pk_client_transaction_cb (DBusGProxy *proxy, const gchar *old_tid, const gchar *timespec,
			  gboolean succeeded, const gchar *role_text, guint duration,
			  const gchar *data, guint uid, const gchar *cmdline, PkClient *client)
{
	PkTransactionObj *obj;
	PkRoleEnum role;
	g_return_if_fail (PK_IS_CLIENT (client));

	role = pk_role_enum_from_text (role_text);
	obj = pk_transaction_obj_new_from_data (old_tid, timespec, succeeded, role, duration, data, uid, cmdline);
	egg_debug ("emitting transaction %s, %s, %i, %s, %ims, %s, %i, %s", old_tid, timespec,
		  succeeded, role_text, duration, data, uid, cmdline);
	g_signal_emit (client, signals [SIGNAL_TRANSACTION], 0, obj);

	/* cache */
	if (client->priv->use_buffer || client->priv->synchronous)
		pk_obj_list_add (client->priv->transaction_list, obj);

	pk_transaction_obj_free (obj);
}

/**
 * pk_client_distro_upgrade_cb:
 */
static void
pk_client_distro_upgrade_cb (DBusGProxy *proxy, const gchar *type_text, const gchar *name,
			     const gchar *summary, PkClient *client)
{
	PkUpdateStateEnum type;
	PkDistroUpgradeObj *obj;
	g_return_if_fail (PK_IS_CLIENT (client));

	type = pk_update_state_enum_from_text (type_text);
	obj = pk_distro_upgrade_obj_new_from_data  (type, name, summary);
	egg_debug ("emitting distro_upgrade %s, %s, %s", type_text, name, summary);
	g_signal_emit (client, signals [SIGNAL_DISTRO_UPGRADE], 0, obj);

	/* cache */
	if (client->priv->use_buffer || client->priv->synchronous)
		pk_obj_list_add (client->priv->distro_upgrade_list, obj);

	pk_distro_upgrade_obj_free (obj);
}

/**
 * pk_client_update_detail_cb:
 */
static void
pk_client_update_detail_cb (DBusGProxy  *proxy, const gchar *package_id, const gchar *updates,
			    const gchar *obsoletes, const gchar *vendor_url, const gchar *bugzilla_url,
			    const gchar *cve_url, const gchar *restart_text, const gchar *update_text,
			    const gchar *changelog, const gchar *state_text, const gchar *issued_text,
			    const gchar *updated_text, PkClient *client)
{
	PkRestartEnum restart;
	PkUpdateDetailObj *detail;
	PkPackageId *id;
	GDate *issued;
	GDate *updated;
	PkUpdateStateEnum state;

	g_return_if_fail (PK_IS_CLIENT (client));

	egg_debug ("emit update-detail %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s, %s",
		  package_id, updates, obsoletes, vendor_url, bugzilla_url,
		  cve_url, restart_text, update_text, changelog,
		  state_text, issued_text, updated_text);

	id = pk_package_id_new_from_string (package_id);
	restart = pk_restart_enum_from_text (restart_text);
	state = pk_update_state_enum_from_text (state_text);
	issued = pk_iso8601_to_date (issued_text);
	updated = pk_iso8601_to_date (updated_text);

	detail = pk_update_detail_obj_new_from_data (id, updates, obsoletes, vendor_url,
						     bugzilla_url, cve_url, restart,
						     update_text, changelog, state,
						     issued, updated);
	g_signal_emit (client, signals [SIGNAL_UPDATE_DETAIL], 0, detail);

	if (issued != NULL)
		g_date_free (issued);
	if (updated != NULL)
		g_date_free (updated);
	pk_package_id_free (id);
	pk_update_detail_obj_free (detail);
}

/**
 * pk_client_category_cb:
 */
static void
pk_client_category_cb (DBusGProxy  *proxy, const gchar *parent_id, const gchar *cat_id,
		       const gchar *name, const gchar *summary, const gchar *icon, PkClient *client)
{
	PkCategoryObj *category;

	g_return_if_fail (PK_IS_CLIENT (client));

	egg_debug ("emit category %s, %s, %s, %s, %s", parent_id, cat_id, name, summary, icon);

	category = pk_category_obj_new_from_data (parent_id, cat_id, name, summary, icon);
	g_signal_emit (client, signals [SIGNAL_CATEGORY], 0, category);

	/* cache */
	if (client->priv->use_buffer || client->priv->synchronous)
		pk_obj_list_add (client->priv->category_list, category);

	pk_category_obj_free (category);
}

/**
 * pk_client_details_cb:
 */
static void
pk_client_details_cb (DBusGProxy *proxy, const gchar *package_id, const gchar *license,
		      const gchar *group_text, const gchar *description, const gchar *url,
		      guint64 size, PkClient *client)
{
	PkGroupEnum group;
	PkDetailsObj *details;
	PkPackageId *id;

	g_return_if_fail (PK_IS_CLIENT (client));

	group = pk_group_enum_from_text (group_text);
	id = pk_package_id_new_from_string (package_id);

	egg_debug ("emit details %s, %s, %s, %s, %s, %ld",
		  package_id, license, group_text, description, url, (long int) size);

	details = pk_details_obj_new_from_data (id, license, group, description, url, size);
	g_signal_emit (client, signals [SIGNAL_DETAILS], 0, details);

	pk_package_id_free (id);
	pk_details_obj_free (details);
}

/**
 * pk_client_file_copy:
 */
static gboolean
pk_client_file_copy (const gchar *filename, const gchar *directory)
{
	gboolean ret;
	GError *error = NULL;
	gchar *command;

	/* TODO: use GIO when we have a hard dep on it */
	command = g_strdup_printf ("cp \"%s\" \"%s\"", filename, directory);
	egg_debug ("command: %s", command);
	ret = g_spawn_command_line_sync (command, NULL, NULL, NULL, &error);
	if (!ret) {
		egg_warning ("failed to copy: %s", error->message);
		g_error_free (error);
	}
	g_free (command);
	return ret;
}

/**
 * pk_client_files_cb:
 */
static void
pk_client_files_cb (DBusGProxy *proxy, const gchar *package_id, const gchar *filelist, PkClient *client)
{
	guint i;
	guint length;
	gchar **split;

	g_return_if_fail (PK_IS_CLIENT (client));

	egg_debug ("emit files %s, <lots of files>", package_id);
	g_signal_emit (client , signals [SIGNAL_FILES], 0, package_id, filelist);

	/* we are a callback from DownloadPackages */
	if (client->priv->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES) {
		split = g_strsplit (filelist, ";", -1);
		length = g_strv_length (split);
		for (i=0; i<length; i++)
			pk_client_file_copy (split[i], client->priv->cached_directory);
		g_strfreev (split);
	}
}

/**
 * pk_client_repo_signature_required_cb:
 **/
static void
pk_client_repo_signature_required_cb (DBusGProxy *proxy, const gchar *package_id, const gchar *repository_name,
				      const gchar *key_url, const gchar *key_userid, const gchar *key_id,
				      const gchar *key_fingerprint, const gchar *key_timestamp,
				      const gchar *type_text, PkClient *client)
{
	g_return_if_fail (PK_IS_CLIENT (client));

	egg_debug ("emit repo-signature-required %s, %s, %s, %s, %s, %s, %s, %s",
		  package_id, repository_name, key_url, key_userid,
		  key_id, key_fingerprint, key_timestamp, type_text);

	g_signal_emit (client, signals [SIGNAL_REPO_SIGNATURE_REQUIRED], 0,
		       package_id, repository_name, key_url, key_userid, key_id, key_fingerprint, key_timestamp, type_text);
}

/**
 * pk_client_eula_required_cb:
 **/
static void
pk_client_eula_required_cb (DBusGProxy *proxy, const gchar *eula_id, const gchar *package_id,
			    const gchar *vendor_name, const gchar *license_agreement, PkClient *client)
{
	g_return_if_fail (PK_IS_CLIENT (client));

	egg_debug ("emit eula-required %s, %s, %s, %s",
		  eula_id, package_id, vendor_name, license_agreement);

	g_signal_emit (client, signals [SIGNAL_EULA_REQUIRED], 0,
		       eula_id, package_id, vendor_name, license_agreement);
}

/**
 * pk_client_media_change_required_cb:
 **/
static void
pk_client_media_change_required_cb (DBusGProxy *proxy,
				    const gchar *media_type_text,
				    const gchar *media_id,
				    const gchar *media_text,
				    PkClient *client)
{
	PkMediaTypeEnum media_type;
	g_return_if_fail (PK_IS_CLIENT (client));

	media_type = pk_media_type_enum_from_text (media_type_text);
	egg_debug ("emit media-change-required %s, %s, %s",
		   media_type_text, media_id, media_text);
	g_signal_emit (client, signals [SIGNAL_MEDIA_CHANGE_REQUIRED], 0,
		       media_type, media_id, media_text);
}

/**
 * pk_client_repo_detail_cb:
 **/
static void
pk_client_repo_detail_cb (DBusGProxy *proxy, const gchar *repo_id,
			  const gchar *description, gboolean enabled, PkClient *client)
{
	g_return_if_fail (PK_IS_CLIENT (client));

	egg_debug ("emit repo-detail %s, %s, %i", repo_id, description, enabled);
	g_signal_emit (client, signals [SIGNAL_REPO_DETAIL], 0, repo_id, description, enabled);
}

/**
 * pk_client_error_code_cb:
 */
static void
pk_client_error_code_cb (DBusGProxy  *proxy,
			 const gchar *code_text,
			 const gchar *details,
			 PkClient    *client)
{
	PkErrorCodeEnum code;
	g_return_if_fail (PK_IS_CLIENT (client));

	code = pk_error_enum_from_text (code_text);
	egg_debug ("emit error-code %s, %s", pk_error_enum_to_text (code), details);
	g_signal_emit (client , signals [SIGNAL_ERROR_CODE], 0, code, details);
}

/**
 * pk_client_allow_cancel_cb:
 */
static void
pk_client_allow_cancel_cb (DBusGProxy *proxy, gboolean allow_cancel, PkClient *client)
{
	g_return_if_fail (PK_IS_CLIENT (client));

	egg_debug ("emit allow-cancel %i", allow_cancel);
	g_signal_emit (client , signals [SIGNAL_ALLOW_CANCEL], 0, allow_cancel);
}

/**
 * pk_client_get_allow_cancel:
 * @client: a valid #PkClient instance
 * @allow_cancel: %TRUE if we are able to cancel the transaction
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Should we be allowed to cancel this transaction?
 * The tid should have been set with pk_client_set_tid() if this is being done
 * on a foreign object.
 *
 * Return value: %TRUE if the daemon serviced the request
 */
gboolean
pk_client_get_allow_cancel (PkClient *client, gboolean *allow_cancel, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetAllowCancel", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_BOOLEAN, allow_cancel,
				 G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}
out:
	return ret;
}

/**
 * pk_client_caller_active_changed_cb:
 */
static void
pk_client_caller_active_changed_cb (DBusGProxy  *proxy,
				    gboolean     is_active,
				    PkClient    *client)
{
	g_return_if_fail (PK_IS_CLIENT (client));

	egg_debug ("emit caller-active-changed %i", is_active);
	g_signal_emit (client , signals [SIGNAL_CALLER_ACTIVE_CHANGED], 0, is_active);
}

/**
 * pk_client_require_restart_cb:
 */
static void
pk_client_require_restart_cb (DBusGProxy  *proxy,
			      const gchar *restart_text,
			      const gchar *package_id,
			      PkClient    *client)
{
	PkRestartEnum restart;
	PkPackageId *id;
	PkRequireRestartObj *obj;
	g_return_if_fail (PK_IS_CLIENT (client));

	restart = pk_restart_enum_from_text (restart_text);
	id = pk_package_id_new_from_string (package_id);
	obj = pk_require_restart_obj_new_from_data (restart, id);

	/* cache */
	if (client->priv->use_buffer || client->priv->synchronous)
		pk_obj_list_add (client->priv->require_restart_list, obj);

	egg_debug ("emit require-restart %s, %s", pk_restart_enum_to_text (restart), package_id);
	g_signal_emit (client , signals [SIGNAL_REQUIRE_RESTART], 0, obj);
	if (restart > client->priv->require_restart) {
		client->priv->require_restart = restart;
		egg_debug ("restart status now %s", pk_restart_enum_to_text (restart));
	}
	pk_package_id_free (id);
	pk_require_restart_obj_free (obj);
}

/**
 * pk_client_message_cb:
 */
static void
pk_client_message_cb (DBusGProxy  *proxy, const gchar *message_text, const gchar *details, PkClient *client)
{
	PkMessageEnum message;
	g_return_if_fail (PK_IS_CLIENT (client));

	message = pk_message_enum_from_text (message_text);
	egg_debug ("emit message %i, %s", message, details);
	g_signal_emit (client , signals [SIGNAL_MESSAGE], 0, message, details);
}

/**
 * pk_client_get_status:
 * @client: a valid #PkClient instance
 * @status: a PkStatusEnum value such as %PK_STATUS_ENUM_WAITING
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Gets the status of a transaction.
 * A transaction has one roles in it's lifetime, but many values of status.
 *
 * Reading the property "status" is quicker than contacting the daemon, but this
 * only works when the transaction was created by the application, and not when
 * using pk_client_set_tid() in a client program.
 *
 * TODO: deprecate
 *
 * Return value: %TRUE if we found the status successfully
 **/
gboolean
pk_client_get_status (PkClient *client, PkStatusEnum *status, GError **error)
{
	gboolean ret = FALSE;
	gchar *status_text = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (status != NULL, FALSE);
	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetStatus", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &status_text,
				 G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}
	*status = pk_status_enum_from_text (status_text);
out:
	g_free (status_text);
	return ret;
}

/**
 * pk_client_get_package:
 * @client: a valid #PkClient instance
 * @package: a %package_id or free text string
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Gets the aim of the transaction, e.g. what was asked to be installed or
 * searched for.
 *
 * Return value: %TRUE if we found the status successfully
 **/
gboolean
pk_client_get_package (PkClient *client, gchar **package, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package != NULL, FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetPackageLast", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, package,
				 G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}
out:
	return ret;
}

/**
 * pk_client_get_progress:
 * @client: a valid #PkClient instance
 * @percentage: the percentage complete of the transaction
 * @subpercentage: the percentage complete of the sub-transaction
 * @elapsed: the duration so far of the transaction
 * @remaining: the estimated time to completion of the transaction
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * To show the user a progress bar or dialog is much more friendly than
 * just a pulsing bar, so we can return this information here.
 * NOTE: the %time_remaining value is guessed and may not be accurate if the
 * backend does not do frequent calls to pk_backend_set_percentage().
 *
 * Return value: %TRUE if we found the progress successfully
 **/
gboolean
pk_client_get_progress (PkClient *client, guint *percentage, guint *subpercentage,
			guint *elapsed, guint *remaining, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (client->priv->tid != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetProgress", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, percentage,
				 G_TYPE_UINT, subpercentage,
				 G_TYPE_UINT, elapsed,
				 G_TYPE_UINT, remaining,
				 G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}
out:
	return ret;
}

/**
 * pk_client_get_role:
 * @client: a valid #PkClient instance
 * @role: a PkRoleEnum value such as %PK_ROLE_ENUM_UPDATE_SYSTEM
 * @text: the primary search term or package name associated with the role
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * The role is the action of the transaction as does not change for the entire
 * lifetime of the transaction.
 *
 * Return value: %TRUE if we found the status successfully
 **/
gboolean
pk_client_get_role (PkClient *client, PkRoleEnum *role, gchar **text, GError **error)
{
	gboolean ret = FALSE;
	gchar *role_text = NULL;
	gchar *text_temp;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (role != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		goto out;
	}

	/* we can avoid a trip to the daemon */
	if (text == NULL && client->priv->role != PK_ROLE_ENUM_UNKNOWN) {
		*role = client->priv->role;
		return TRUE;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetRole", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &role_text,
				 G_TYPE_STRING, &text_temp,
				 G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}
	*role = pk_role_enum_from_text (role_text);
	if (text != NULL)
		*text = text_temp;
	else
		g_free (text_temp);
out:
	g_free (role_text);
	return ret;
}

/**
 * pk_client_cancel:
 * @client: a valid #PkClient instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Cancel the transaction if possible.
 * This is good idea when downloading or depsolving, but not when writing
 * to the disk.
 * The daemon shouldn't let you do anything stupid, so it's quite safe to call
 * this method.
 *
 * Return value: %TRUE if we cancelled successfully
 **/
gboolean
pk_client_cancel (PkClient *client, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* we don't need to cancel, so return TRUE */
	if (client->priv->proxy == NULL) {
		ret = TRUE;
		goto out;
	}

	/* we cannot cancel a client in ::Finished() */
	if (client->priv->is_finishing) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "unable to cancel client in finished handler");
		goto out;
	}

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "Cancel", &error_local,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* no error to process */
	if (ret)
		goto out;

	/* special case - if the tid is already finished, then cancel should return TRUE */
	if (g_str_has_suffix (error_local->message, " doesn't exist\n")) {
		egg_debug ("error ignored '%s' as we are trying to cancel", error_local->message);
		g_error_free (error_local);
		ret = TRUE;
		goto out;
	}

	/* we failed one of these, return the error to the user */
	if (error != NULL)
		*error = pk_client_error_fixup (error_local);
	g_error_free (error_local);
out:
	return ret;
}

/**
 * pk_client_transaction_timeout_cb:
 **/
static gboolean
pk_client_transaction_timeout_cb (PkClient *client)
{
	gboolean ret;
	const gchar *details = "cancelling client as timeout is up";

	egg_debug ("timeout up");
	ret = pk_client_cancel (client, NULL);
	if (!ret) {
		egg_warning ("failed to cancel");
		return TRUE;
	}

	/* emit signal */
	egg_debug ("emit error-code %i, %s", PK_ERROR_ENUM_TRANSACTION_CANCELLED, details);
	g_signal_emit (client , signals [SIGNAL_ERROR_CODE], 0, PK_ERROR_ENUM_TRANSACTION_CANCELLED, details);

	/* set used */
	client->priv->timeout_id = 0;

	return FALSE;
}

/**
 * pk_client_allocate_transaction_id:
 * @client: a valid #PkClient instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Get a new tid, freeing the old tid if required.
 *
 * Return value: the tid, or %NULL if we had an error.
 **/
static gboolean
pk_client_allocate_transaction_id (PkClient *client, GError **error)
{
	gboolean ret = FALSE;
	gchar *tid = NULL;
	GError *error_local = NULL;
	const gchar **list;
	guint len;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* special value meaning "don't wait if another transaction queued" */
	if (client->priv->timeout == 0) {
		list = pk_control_transaction_list_get (client->priv->control);
		len = g_strv_length ((gchar**)list);
		if (len > 0) {
			if (error != NULL)
				*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "will not queue as timeout 0");
			goto out;
		}
	}

	/* get a new ID */
	ret = pk_control_allocate_transaction_id (client->priv->control, &tid, &error_local);
	if (!ret) {
		if (error != NULL) {
			if (error_local->code == PK_CONTROL_ERROR_CANNOT_START_DAEMON)
				*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_CANNOT_START_DAEMON, "cannot start daemon: %s", error_local->message);
			else
				*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "failed to get a TID: %s (%i)", error_local->message, error_local->code);
		}
		g_error_free (error_local);
		goto out;
	}

	/* free any old tid */
	g_free (client->priv->tid);
	client->priv->tid = NULL;

	/* set that new ID to this GObject */
	ret = pk_client_set_tid (client, tid, &error_local);
	if (!ret) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "failed to set TID: %s", error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* set a timeout */
	if (client->priv->timeout > 0)
		client->priv->timeout_id = g_timeout_add (client->priv->timeout, (GSourceFunc) pk_client_transaction_timeout_cb, client);
out:
	g_free (tid);
	return ret;
}

/**
 * pk_client_get_updates:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Get a list of all the packages that can be updated for all repositories.
 *
 * Return value: %TRUE if we got told the daemon to get the update list
 **/
gboolean
pk_client_get_updates (PkClient *client, PkBitfield filters, GError **error)
{
	gboolean ret = FALSE;
	gchar *filter_text = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_UPDATES;
	client->priv->cached_filters = filters;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}
	filter_text = pk_filter_bitfield_to_text (filters);

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetUpdates", &error_local,
				 G_TYPE_STRING, filter_text,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	g_free (filter_text);
	return ret;
}

/**
 * pk_client_get_categories:
 * @client: a valid #PkClient instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Get a list of all categories supported
 *
 * Return value: %TRUE if we got told the daemon to get the category list
 **/
gboolean
pk_client_get_categories (PkClient *client, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_CATEGORIES;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetCategories", &error_local,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_update_system:
 * @client: a valid #PkClient instance
 * @only_trusted: only trusted packages should be installed
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Update all the packages on the system with the highest versions found in all
 * repositories.
 * NOTE: you can't choose what repositories to update from, but you can do:
 * - pk_client_repo_disable()
 * - pk_client_update_system()
 * - pk_client_repo_enable()
 *
 * Return value: %TRUE if we told the daemon to update the system
 **/
gboolean
pk_client_update_system (PkClient *client, gboolean only_trusted, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_UPDATE_SYSTEM;
	client->priv->cached_only_trusted = only_trusted;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "UpdateSystem", &error_local,
				 G_TYPE_BOOLEAN, only_trusted,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* we failed one of these, return the error to the user */
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_search_name:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @search: free text to search for, for instance, "power"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Search all the locally installed files and remote repositories for a package
 * that matches a specific name.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_search_name (PkClient *client, PkBitfield filters, const gchar *search, GError **error)
{
	gboolean ret = FALSE;
	gchar *filter_text = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SEARCH_NAME;
	client->priv->cached_filters = filters;
	client->priv->cached_search = g_strdup (search);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}
	filter_text = pk_filter_bitfield_to_text (filters);

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "SearchName", &error_local,
				 G_TYPE_STRING, filter_text,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	g_free (filter_text);
	return ret;
}

/**
 * pk_client_search_details:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @search: free text to search for, for instance, "power"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Search all detailed summary information to try and find a keyword.
 * Think of this as pk_client_search_name(), but trying much harder and
 * taking longer.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_search_details (PkClient *client, PkBitfield filters, const gchar *search, GError **error)
{
	gboolean ret = FALSE;
	gchar *filter_text = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SEARCH_DETAILS;
	client->priv->cached_filters = filters;
	client->priv->cached_search = g_strdup (search);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}
	filter_text = pk_filter_bitfield_to_text (filters);

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "SearchDetails", &error_local,
				 G_TYPE_STRING, filter_text,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	g_free (filter_text);
	return ret;
}

/**
 * pk_client_search_group:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @search: a group enum to search for, for instance, "system-tools"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Return all packages in a specific group.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_search_group (PkClient *client, PkBitfield filters, const gchar *search, GError **error)
{
	gboolean ret = FALSE;
	gchar *filter_text = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SEARCH_GROUP;
	client->priv->cached_filters = filters;
	client->priv->cached_search = g_strdup (search);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}
	filter_text = pk_filter_bitfield_to_text (filters);

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "SearchGroup", &error_local,
				 G_TYPE_STRING, filter_text,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	g_free (filter_text);
	return ret;
}

/**
 * pk_client_search_file:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @search: file to search for, for instance, "/sbin/service"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Search for packages that provide a specific file.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_search_file (PkClient *client, PkBitfield filters, const gchar *search, GError **error)
{
	gboolean ret = FALSE;
	gchar *filter_text = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SEARCH_FILE;
	client->priv->cached_filters = filters;
	client->priv->cached_search = g_strdup (search);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}
	filter_text = pk_filter_bitfield_to_text (filters);

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "SearchFile", &error_local,
				 G_TYPE_STRING, filter_text,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	g_free (filter_text);
	return ret;
}

/**
 * pk_client_get_depends:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: If we should search recursively for depends
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Get the packages that depend this one, i.e. child->parent.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_get_depends (PkClient *client, PkBitfield filters, gchar **package_ids, gboolean recursive, GError **error)
{
	gboolean ret = FALSE;
	gchar *filter_text = NULL;
	gchar *package_ids_temp;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* check the PackageIDs here to avoid a round trip if invalid */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "package_ids '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_DEPENDS;
	client->priv->cached_filters = filters;
	client->priv->cached_package_ids = g_strdupv (package_ids);
	client->priv->cached_force = recursive;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}
	filter_text = pk_filter_bitfield_to_text (filters);

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetDepends", &error_local,
				 G_TYPE_STRING, filter_text,
				 G_TYPE_STRV, package_ids,
				 G_TYPE_BOOLEAN, recursive,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	g_free (filter_text);
	return ret;
}

/**
 * pk_client_download_packages:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @directory: the location where packages are to be downloaded
 * @error: a %GError to put the error code and message in, or %NULL
 * Get the packages that depend this one, i.e. child->parent.
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_download_packages (PkClient *client, gchar **package_ids, const gchar *directory, GError **error)
{
	gboolean ret = FALSE;
	gchar *package_ids_temp;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* check the PackageIDs here to avoid a round trip if invalid */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "package_ids '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_DOWNLOAD_PACKAGES;
	client->priv->cached_package_ids = g_strdupv (package_ids);
	client->priv->cached_directory = g_strdup (directory);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "DownloadPackages", &error_local,
				 G_TYPE_STRV, package_ids,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_get_packages:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Get the list of packages from the backend
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_get_packages (PkClient *client, PkBitfield filters, GError **error)
{
	gboolean ret = FALSE;
	gchar *filter_text = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_PACKAGES;
	client->priv->cached_filters = filters;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}
	filter_text = pk_filter_bitfield_to_text (filters);

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetPackages", &error_local,
				 G_TYPE_STRING, filter_text,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	g_free (filter_text);
	return ret;
}

/**
 * pk_client_set_locale:
 * @client: a valid #PkClient instance
 * @code: a valid locale code, e.g. en_GB
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Set the locale for this transaction.
 * You normally don't need to call this function as the locale is set
 * automatically when the tid is requested.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_set_locale (PkClient *client, const gchar *code, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (code != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "SetLocale", &error_local,
				 G_TYPE_STRING, code,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}
out:
	return ret;
}

/**
 * pk_client_get_requires:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @recursive: If we should search recursively for requires
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Get the packages that require this one, i.e. parent->child.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_get_requires (PkClient *client, PkBitfield filters, gchar **package_ids, gboolean recursive, GError **error)
{
	gboolean ret = FALSE;
	gchar *filter_text = NULL;
	gchar *package_ids_temp;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* check the PackageIDs here to avoid a round trip if invalid */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "package_ids '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_REQUIRES;
	client->priv->cached_filters = filters;
	client->priv->cached_package_ids = g_strdupv (package_ids);
	client->priv->cached_force = recursive;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}
	filter_text = pk_filter_bitfield_to_text (filters);

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetRequires", &error_local,
				 G_TYPE_STRING, filter_text,
				 G_TYPE_STRV, package_ids,
				 G_TYPE_BOOLEAN, recursive,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	g_free (filter_text);
	return ret;
}

/**
 * pk_client_what_provides:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @provides: a #PkProvidesEnum value such as PK_PROVIDES_ENUM_CODEC
 * @search: a search term such as "sound/mp3"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * This should return packages that provide the supplied attributes.
 * This method is useful for finding out what package(s) provide a modalias
 * or GStreamer codec string.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_what_provides (PkClient *client, PkBitfield filters, PkProvidesEnum provides,
			 const gchar *search, GError **error)
{
	gboolean ret = FALSE;
	const gchar *provides_text;
	gchar *filter_text = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (provides != PK_PROVIDES_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (search != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_WHAT_PROVIDES;
	client->priv->cached_search = g_strdup (search);
	client->priv->cached_filters = filters;
	client->priv->cached_provides = provides;

	provides_text = pk_provides_enum_to_text (provides);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}
	filter_text = pk_filter_bitfield_to_text (filters);

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "WhatProvides", &error_local,
				 G_TYPE_STRING, filter_text,
				 G_TYPE_STRING, provides_text,
				 G_TYPE_STRING, search,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	g_free (filter_text);
	return ret;
}

/**
 * pk_client_get_update_detail:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Get details about the specific update, for instance any CVE urls and
 * severity information.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_get_update_detail (PkClient *client, gchar **package_ids, GError **error)
{
	gboolean ret = FALSE;
	gchar *package_ids_temp;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* check the PackageIDs here to avoid a round trip if invalid */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "package_ids '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_UPDATE_DETAIL;
	client->priv->cached_package_ids = g_strdupv (package_ids);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetUpdateDetail", &error_local,
				 G_TYPE_STRV, package_ids,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_rollback:
 * @client: a valid #PkClient instance
 * @transaction_id: a transaction_id structure
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Roll back to a previous transaction. I think only conary supports this right
 * now, but it's useful to add an abstract way of doing it.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_rollback (PkClient *client, const gchar *transaction_id, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_ROLLBACK;
	client->priv->cached_transaction_id = g_strdup (transaction_id);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "Rollback", &error_local,
				 G_TYPE_STRING, transaction_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* we failed one of these, return the error to the user */
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_resolve:
 * @client: a valid #PkClient instance
 * @filters: a %PkBitfield such as %PK_FILTER_ENUM_GUI | %PK_FILTER_ENUM_FREE or %PK_FILTER_ENUM_NONE
 * @packages: an array of package names to resolve, e.g. "gnome-system-tools"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Resolve a package name into a %package_id. This can return installed and
 * available packages and allows you find out if a package is installed locally
 * or is available in a repository.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_resolve (PkClient *client, PkBitfield filters, gchar **packages, GError **error)
{
	gboolean ret = FALSE;
	gchar *filter_text = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (packages != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_RESOLVE;
	client->priv->cached_filters = filters;
	client->priv->cached_package_ids = g_strdupv (packages);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}
	filter_text = pk_filter_bitfield_to_text (filters);

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "Resolve", &error_local,
				 G_TYPE_STRING, filter_text,
				 G_TYPE_STRV, packages,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	g_free (filter_text);
	return ret;
}

/**
 * pk_client_get_details:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Get details of a package, so more information can be obtained for GUI
 * or command line tools.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_get_details (PkClient *client, gchar **package_ids, GError **error)
{
	gboolean ret = FALSE;
	gchar *package_ids_temp;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* check the PackageIDs here to avoid a round trip if invalid */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "package_ids '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_DETAILS;
	client->priv->cached_package_ids = g_strdupv (package_ids);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetDetails", &error_local,
				 G_TYPE_STRV, package_ids,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_get_distro_upgrades:
 * @client: a valid #PkClient instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * This method should return a list of distribution upgrades that are available.
 * It should not return updates, only major upgrades.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_get_distro_upgrades (PkClient *client, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_DISTRO_UPGRADES;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetDistroUpgrades", &error_local,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_get_files:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Get the file list (i.e. a list of files installed) for the specified package.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_get_files (PkClient *client, gchar **package_ids, GError **error)
{
	gboolean ret = FALSE;
	gchar *package_ids_temp;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* check the PackageIDs here to avoid a round trip if invalid */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "package_ids '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_FILES;
	client->priv->cached_package_ids = g_strdupv (package_ids);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetFiles", &error_local,
				 G_TYPE_STRV, package_ids,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_remove_packages:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @allow_deps: if other dependant packages are allowed to be removed from the computer
 * @autoremove: if other packages installed at the same time should be tried to remove
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Remove a package (optionally with dependancies) from the system.
 * If %allow_deps is set to %FALSE, and other packages would have to be removed,
 * then the transaction would fail.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_remove_packages (PkClient *client, gchar **package_ids, gboolean allow_deps,
			  gboolean autoremove, GError **error)
{
	gboolean ret = FALSE;
	gchar *package_ids_temp;
	GError *error_local = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* check the PackageIDs here to avoid a round trip if invalid */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "package_ids '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_REMOVE_PACKAGES;
	client->priv->cached_allow_deps = allow_deps;
	client->priv->cached_autoremove = autoremove;
	client->priv->cached_package_ids = g_strdupv (package_ids);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "RemovePackages", &error_local,
				 G_TYPE_STRV, package_ids,
				 G_TYPE_BOOLEAN, allow_deps,
				 G_TYPE_BOOLEAN, autoremove,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* we failed one of these, return the error to the user */
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_refresh_cache:
 * @client: a valid #PkClient instance
 * @force: if we shoudl aggressively drop caches
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Refresh the cache, i.e. download new metadata from a remote URL so that
 * package lists are up to date.
 * This action may take a few minutes and should be done when the session and
 * system are idle.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_refresh_cache (PkClient *client, gboolean force, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_REFRESH_CACHE;
	client->priv->cached_force = force;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "RefreshCache", &error_local,
				 G_TYPE_BOOLEAN, force,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* we failed one of these, return the error to the user */
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_install_packages:
 * @client: a valid #PkClient instance
 * @only_trusted: only trusted packages should be installed
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a package of the newest and most correct version.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_install_packages (PkClient *client, gboolean only_trusted, gchar **package_ids, GError **error)
{
	gboolean ret = FALSE;
	gchar *package_ids_temp;
	GError *error_local = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* check the PackageIDs here to avoid a round trip if invalid */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "package_ids '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_INSTALL_PACKAGES;
	client->priv->cached_only_trusted = only_trusted;
	client->priv->cached_package_ids = g_strdupv (package_ids);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "InstallPackages", &error_local,
				 G_TYPE_BOOLEAN, only_trusted,
				 G_TYPE_STRV, package_ids,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* we failed one of these, return the error to the user */
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_install_signature:
 * @client: a valid #PkClient instance
 * @package_id: a signature_id structure such as "hal;0.0.1;i386;fedora"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a signature of the newest and most correct version.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_install_signature (PkClient *client, PkSigTypeEnum type, const gchar *key_id,
			     const gchar *package_id, GError **error)
{
	gboolean ret = FALSE;
	const gchar *type_text;
	GError *error_local = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (type != PK_SIGTYPE_ENUM_UNKNOWN, FALSE);
	g_return_val_if_fail (key_id != NULL, FALSE);
	g_return_val_if_fail (package_id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_INSTALL_SIGNATURE;
	client->priv->cached_package_id = g_strdup (package_id);
	client->priv->cached_key_id = g_strdup (key_id);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}
	type_text = pk_sig_type_enum_to_text (type);

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "InstallSignature", &error_local,
				 G_TYPE_STRING, type_text,
				 G_TYPE_STRING, key_id,
				 G_TYPE_STRING, package_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* we failed one of these, return the error to the user */
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_update_packages:
 * @client: a valid #PkClient instance
 * @only_trusted: only trusted packages should be installed
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Update specific packages to the newest available versions.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_update_packages (PkClient *client, gboolean only_trusted, gchar **package_ids, GError **error)
{
	gboolean ret = FALSE;
	gchar *package_ids_temp;
	GError *error_local = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* check the PackageIDs here to avoid a round trip if invalid */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "package_ids '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_UPDATE_PACKAGES;
	client->priv->cached_only_trusted = only_trusted;

	/* only copy if we are not requeing */
	if (client->priv->cached_package_ids != package_ids) {
		client->priv->cached_package_ids = g_strdupv (package_ids);
	}

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "UpdatePackages", &error_local,
				 G_TYPE_BOOLEAN, only_trusted,
				 G_TYPE_STRV, package_ids,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* we failed one of these, return the error to the user */
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_resolve_local_path:
 *
 * Resolves paths like ../../Desktop/bar.rpm to /home/hughsie/Desktop/bar.rpm
 * TODO: We should use canonicalize_filename() in gio/glocalfile.c as realpath()
 * is crap.
 **/
static gchar *
pk_resolve_local_path (const gchar *rel_path)
{
	gchar *real = NULL;
	gchar *temp;

	/* don't trust realpath one little bit */
	if (rel_path == NULL)
		return NULL;

#ifndef __FreeBSD__
	/* ITS4: ignore, glibc allocates us a buffer to try and fix some brain damage */
	temp = realpath (rel_path, NULL);
	if (temp != NULL) {
		real = g_strdup (temp);
		/* yes, free, not g_free */
		free (temp);
	}
#else /* __FreeBSD__ */
{
	char abs_path[PATH_MAX];
	temp = realpath (rel_path, abs_path);
	if (temp != NULL) {
		real = g_strdup (temp);
	}
}
#endif
	return real;
}

/**
 * pk_client_install_files:
 * @client: a valid #PkClient instance
 * @only_trusted: only trusted packages should be installed
 * @files_rel: a file such as "/home/hughsie/Desktop/hal-devel-0.10.0.rpm"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Install a file locally, and get the deps from the repositories.
 * This is useful for double clicking on a .rpm or .deb file.
 *
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_install_files (PkClient *client, gboolean only_trusted, gchar **files_rel, GError **error)
{
	guint i;
	guint length;
	gboolean ret = FALSE;
	gchar **files = NULL;
	gchar *file;
	GError *error_local = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (files_rel != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* convert all the relative paths to absolute ones */
	files = g_strdupv (files_rel);
	length = g_strv_length (files);
	for (i=0; i<length; i++) {
		file = pk_resolve_local_path (files[i]);
		/* only replace if different */
		if (g_strcmp0 (file, files[i]) != 0) {
			egg_debug ("resolved %s to %s", files[i], file);
			/* replace */
			g_free (files[i]);
			files[i] = g_strdup (file);
		}
		g_free (file);
	}

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_INSTALL_FILES;
	client->priv->cached_only_trusted = only_trusted;
	client->priv->cached_full_paths = g_strdupv (files);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "InstallFiles", &error_local,
				 G_TYPE_BOOLEAN, only_trusted,
				 G_TYPE_STRV, files,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* we failed one of these, return the error to the user */
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	g_strfreev (files);
	return ret;
}

/**
 * pk_client_get_repo_list:
 * @client: a valid #PkClient instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Get the list of repositories installed on the system.
 *
 * Return value: %TRUE if the daemon queued the transaction
 */
gboolean
pk_client_get_repo_list (PkClient *client, PkBitfield filters, GError **error)
{
	gboolean ret = FALSE;
	gchar *filter_text = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_GET_REPO_LIST;
	client->priv->cached_filters = filters;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}
	filter_text = pk_filter_bitfield_to_text (filters);

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetRepoList", &error_local,
				 G_TYPE_STRING, filter_text,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	g_free (filter_text);
	return ret;
}

/**
 * pk_client_accept_eula:
 * @client: a valid #PkClient instance
 * @eula_id: the <literal>eula_id</literal> we are agreeing to
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * We may want to agree to a EULA dialog if one is presented.
 *
 * Return value: %TRUE if the daemon queued the transaction
 */
gboolean
pk_client_accept_eula (PkClient *client, const gchar *eula_id, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (eula_id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_ACCEPT_EULA;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "AcceptEula", &error_local,
				 G_TYPE_STRING, eula_id,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* we failed one of these, return the error to the user */
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_repo_enable:
 * @client: a valid #PkClient instance
 * @repo_id: a repo_id structure such as "livna-devel"
 * @enabled: if we should enable the repository
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Enable or disable the repository.
 *
 * Return value: %TRUE if the daemon queued the transaction
 */
gboolean
pk_client_repo_enable (PkClient *client, const gchar *repo_id, gboolean enabled, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (repo_id != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_REPO_ENABLE;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "RepoEnable", &error_local,
				 G_TYPE_STRING, repo_id,
				 G_TYPE_BOOLEAN, enabled,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* we failed one of these, return the error to the user */
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_repo_set_data:
 * @client: a valid #PkClient instance
 * @repo_id: a repo_id structure such as "livna-devel"
 * @parameter: the parameter to change
 * @value: what we should change it to
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * We may want to set a repository parameter.
 * NOTE: this is free text, and is left to the backend to define a format.
 *
 * Return value: %TRUE if the daemon queued the transaction
 */
gboolean
pk_client_repo_set_data (PkClient *client, const gchar *repo_id, const gchar *parameter,
			 const gchar *value, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL; /* we can't use the same error as we might be NULL */

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (repo_id != NULL, FALSE);
	g_return_val_if_fail (parameter != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_REPO_SET_DATA;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "RepoSetData", &error_local,
				 G_TYPE_STRING, repo_id,
				 G_TYPE_STRING, parameter,
				 G_TYPE_STRING, value,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* we failed one of these, return the error to the user */
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_simulate_install_files:
 * @client: a valid #PkClient instance
 * @files_rel: a file such as "/home/hughsie/Desktop/hal-devel-0.10.0.rpm"
 * @error: a %GError to put the error code and message in, or %NULL
 * Simulate an installation of files.
 * NOTE: This method might emit packages with INSTALLING, REMOVING, UPDATING,
 *       REINSTALLING or DOWNGRADING status.
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_simulate_install_files (PkClient *client, gchar **files_rel, GError **error)
{
	guint i;
	guint length;
	gboolean ret = FALSE;
	gchar **files = NULL;
	gchar *file;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (files_rel != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		return FALSE;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}

	/* convert all the relative paths to absolute ones */
	files = g_strdupv (files_rel);
	length = g_strv_length (files);
	for (i=0; i<length; i++) {
		file = pk_resolve_local_path (files[i]);
		/* only replace if different */
		if (g_strcmp0 (file, files[i]) != 0) {
			egg_debug ("resolved %s to %s", files[i], file);
			/* replace */
			g_free (files[i]);
			files[i] = g_strdup (file);
		}
		g_free (file);
	}

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SIMULATE_INSTALL_FILES;
	client->priv->cached_full_paths = g_strdupv (files);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		return FALSE;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "SimulateInstallFiles", error,
				 G_TYPE_STRV, files,
				 G_TYPE_INVALID, G_TYPE_INVALID);

	/* we failed one of these, return the error to the user */
	if (ret && !client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}

	return ret;
}

/**
 * pk_client_simulate_install_packages:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @error: a %GError to put the error code and message in, or %NULL
 * Simulate an installation of packages.
 * NOTE: This method might emit packages with INSTALLING, REMOVING, UPDATING,
 *       REINSTALLING or DOWNGRADING status.
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_simulate_install_packages (PkClient *client, gchar **package_ids, GError **error)
{
	gboolean ret;
	gchar *package_ids_temp;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		return FALSE;
	}

	/* check the PackageIDs here to avoid a round trip if invalid */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "package_ids '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		return FALSE;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES;
	client->priv->cached_package_ids = g_strdupv (package_ids);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		return FALSE;
	}
	ret = dbus_g_proxy_call (client->priv->proxy, "SimulateInstallPackages", error,
				 G_TYPE_STRV, package_ids,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret && !client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}

	return ret;
}

/**
 * pk_client_simulate_remove_packages:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @error: a %GError to put the error code and message in, or %NULL
 * Simulate a removal of packages.
 * NOTE: This method might emit packages with INSTALLING, REMOVING, UPDATING,
 *       REINSTALLING or DOWNGRADING status.
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_simulate_remove_packages (PkClient *client, gchar **package_ids, GError **error)
{
	gboolean ret;
	gchar *package_ids_temp;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		return FALSE;
	}

	/* check the PackageIDs here to avoid a round trip if invalid */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "package_ids '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		return FALSE;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES;
	client->priv->cached_package_ids = g_strdupv (package_ids);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		return FALSE;
	}
	ret = dbus_g_proxy_call (client->priv->proxy, "SimulateRemovePackages", error,
				 G_TYPE_STRV, package_ids,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret && !client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}

	return ret;
}

/**
 * pk_client_simulate_update_packages:
 * @client: a valid #PkClient instance
 * @package_ids: a null terminated array of package_id structures such as "hal;0.0.1;i386;fedora"
 * @error: a %GError to put the error code and message in, or %NULL
 * Simulate an update of packages.
 * NOTE: This method might emit packages with INSTALLING, REMOVING, UPDATING,
 *       REINSTALLING or DOWNGRADING status.
 * Return value: %TRUE if the daemon queued the transaction
 **/
gboolean
pk_client_simulate_update_packages (PkClient *client, gchar **package_ids, GError **error)
{
	gboolean ret;
	gchar *package_ids_temp;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (package_ids != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		return FALSE;
	}

	/* check the PackageIDs here to avoid a round trip if invalid */
	ret = pk_package_ids_check (package_ids);
	if (!ret) {
		package_ids_temp = pk_package_ids_to_text (package_ids);
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_INVALID_INPUT, "package_ids '%s' are not valid", package_ids_temp);
		g_free (package_ids_temp);
		return FALSE;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret) {
		return FALSE;
	}

	/* save this so we can re-issue it */
	client->priv->role = PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES;
	client->priv->cached_package_ids = g_strdupv (package_ids);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		return FALSE;
	}
	ret = dbus_g_proxy_call (client->priv->proxy, "SimulateUpdatePackages", error,
				 G_TYPE_STRV, package_ids,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (ret && !client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}

	return ret;
}

/**
 * pk_client_is_caller_active:
 * @client: a valid #PkClient instance
 * @is_active: if the caller of the method is still alive
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * If the caller is no longer active, we may want to show a warning or message
 * as a libnotify box as the application can't handle it internally any more.
 *
 * Return value: %TRUE if the daemon serviced the request
 **/
gboolean
pk_client_is_caller_active (PkClient *client, gboolean *is_active, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (is_active != NULL, FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "IsCallerActive", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_BOOLEAN, is_active,
				 G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}
out:
	return ret;
}

/**
 * pk_client_get_old_transactions:
 * @client: a valid #PkClient instance
 * @number: the number of past transactions to return, or 0 for all
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Get the old transaction list, mainly used for the rollback viewer.
 *
 * Return value: %TRUE if the daemon serviced the request
 **/
gboolean
pk_client_get_old_transactions (PkClient *client, guint number, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* ensure we are not trying to run without reset */
	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "TID already set to %s", client->priv->tid);
		goto out;
	}

	/* get and set a new ID */
	ret = pk_client_allocate_transaction_id (client, error);
	if (!ret)
		goto out;

	/* check to see if we have a valid proxy */
	if (client->priv->proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_NO_TID, "No proxy for transaction");
		ret = FALSE;
		goto out;
	}

	/* do the method */
	ret = dbus_g_proxy_call (client->priv->proxy, "GetOldTransactions", &error_local,
				 G_TYPE_UINT, number,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		if (error != NULL)
			*error = pk_client_error_fixup (error_local);
		g_error_free (error_local);
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		/* allow clients to respond in the status changed callback */
		pk_client_change_status (client, PK_STATUS_ENUM_WAIT);

		/* spin until finished */
		if (client->priv->synchronous) {
			g_main_loop_run (client->priv->loop);
			if (PK_IS_CLIENT (client) && client->priv->error != NULL) {
				ret = FALSE;
				if (error != NULL)
					*error = g_error_copy (client->priv->error);
			}
		}
	}
out:
	return ret;
}

/**
 * pk_client_requeue:
 * @client: a valid #PkClient instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * We might need to requeue if we want to take an existing #PkClient instance
 * and re-run it after completion. Doing this allows us to do things like
 * re-searching when the output list may have changed state.
 *
 * Return value: %TRUE if we could requeue the client
 */
gboolean
pk_client_requeue (PkClient *client, GError **error)
{
	gboolean ret = FALSE;
	PkClientPrivate *priv = PK_CLIENT_GET_PRIVATE (client);

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* we are no longer waiting, we are setting up */
	if (priv->role == PK_ROLE_ENUM_UNKNOWN) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_ROLE_UNKNOWN, "role unknown for reque");
		goto out;
	}

	/* is not already finished */
	if (!client->priv->is_finished) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "not finished, so cannot requeue");
		goto out;
	}

	/* clear enough data of the client to allow us to requeue */
	g_free (client->priv->tid);
	client->priv->tid = NULL;
	client->priv->status = PK_STATUS_ENUM_UNKNOWN;
	client->priv->is_finished = FALSE;
	g_clear_error (&client->priv->error);

	/* clear package list */
	pk_obj_list_clear (PK_OBJ_LIST(client->priv->package_list));
	pk_obj_list_clear (client->priv->category_list);
	pk_obj_list_clear (client->priv->distro_upgrade_list);
	pk_obj_list_clear (client->priv->transaction_list);

	/* don't exit from the loop when the first tid times out */
	pk_client_disconnect_proxy (client);

	/* do the correct action with the cached parameters */
	if (priv->role == PK_ROLE_ENUM_GET_DEPENDS)
		ret = pk_client_get_depends (client, priv->cached_filters, priv->cached_package_ids, priv->cached_force, error);
	else if (priv->role == PK_ROLE_ENUM_GET_UPDATE_DETAIL)
		ret = pk_client_get_update_detail (client, priv->cached_package_ids, error);
	else if (priv->role == PK_ROLE_ENUM_RESOLVE)
		ret = pk_client_resolve (client, priv->cached_filters, priv->cached_package_ids, error);
	else if (priv->role == PK_ROLE_ENUM_ROLLBACK)
		ret = pk_client_rollback (client, priv->cached_transaction_id, error);
	else if (priv->role == PK_ROLE_ENUM_GET_DETAILS)
		ret = pk_client_get_details (client, priv->cached_package_ids, error);
	else if (priv->role == PK_ROLE_ENUM_GET_FILES)
		ret = pk_client_get_files (client, priv->cached_package_ids, error);
	else if (priv->role == PK_ROLE_ENUM_DOWNLOAD_PACKAGES)
		ret = pk_client_download_packages (client, priv->cached_package_ids, priv->cached_directory, error);
	else if (priv->role == PK_ROLE_ENUM_GET_REQUIRES)
		ret = pk_client_get_requires (client, priv->cached_filters, priv->cached_package_ids, priv->cached_force, error);
	else if (priv->role == PK_ROLE_ENUM_GET_UPDATES)
		ret = pk_client_get_updates (client, priv->cached_filters, error);
	else if (priv->role == PK_ROLE_ENUM_SEARCH_DETAILS)
		ret = pk_client_search_details (client, priv->cached_filters, priv->cached_search, error);
	else if (priv->role == PK_ROLE_ENUM_SEARCH_FILE)
		ret = pk_client_search_file (client, priv->cached_filters, priv->cached_search, error);
	else if (priv->role == PK_ROLE_ENUM_SEARCH_GROUP)
		ret = pk_client_search_group (client, priv->cached_filters, priv->cached_search, error);
	else if (priv->role == PK_ROLE_ENUM_SEARCH_NAME)
		ret = pk_client_search_name (client, priv->cached_filters, priv->cached_search, error);
	else if (priv->role == PK_ROLE_ENUM_INSTALL_PACKAGES)
		ret = pk_client_install_packages (client, priv->cached_only_trusted, priv->cached_package_ids, error);
	else if (priv->role == PK_ROLE_ENUM_INSTALL_FILES)
		ret = pk_client_install_files (client, priv->cached_only_trusted, priv->cached_full_paths, error);
	else if (priv->role == PK_ROLE_ENUM_INSTALL_SIGNATURE)
		ret = pk_client_install_signature (client, PK_SIGTYPE_ENUM_GPG, priv->cached_key_id, priv->cached_package_id, error);
	else if (priv->role == PK_ROLE_ENUM_REFRESH_CACHE)
		ret = pk_client_refresh_cache (client, priv->cached_force, error);
	else if (priv->role == PK_ROLE_ENUM_REMOVE_PACKAGES)
		ret = pk_client_remove_packages (client, priv->cached_package_ids, priv->cached_allow_deps, priv->cached_autoremove, error);
	else if (priv->role == PK_ROLE_ENUM_UPDATE_PACKAGES)
		ret = pk_client_update_packages (client, priv->cached_only_trusted, priv->cached_package_ids, error);
	else if (priv->role == PK_ROLE_ENUM_UPDATE_SYSTEM)
		ret = pk_client_update_system (client, priv->cached_only_trusted, error);
	else if (priv->role == PK_ROLE_ENUM_GET_REPO_LIST)
		ret = pk_client_get_repo_list (client, priv->cached_filters, error);
	else if (priv->role == PK_ROLE_ENUM_GET_CATEGORIES)
		ret = pk_client_get_categories (client, error);
	else if (priv->role == PK_ROLE_ENUM_GET_DISTRO_UPGRADES)
		ret = pk_client_get_distro_upgrades (client, error);
	else if (priv->role == PK_ROLE_ENUM_SIMULATE_INSTALL_FILES)
		ret = pk_client_simulate_install_files (client, priv->cached_full_paths, error);
	else if (priv->role == PK_ROLE_ENUM_SIMULATE_INSTALL_PACKAGES)
		ret = pk_client_simulate_install_packages (client, priv->cached_package_ids, error);
	else if (priv->role == PK_ROLE_ENUM_SIMULATE_REMOVE_PACKAGES)
		ret = pk_client_simulate_remove_packages (client, priv->cached_package_ids, error);
	else if (priv->role == PK_ROLE_ENUM_SIMULATE_UPDATE_PACKAGES)
		ret = pk_client_simulate_update_packages (client, priv->cached_package_ids, error);
	else {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_ROLE_UNKNOWN, "role unknown for reque");
		goto out;
	}
out:
	return ret;
}

/**
 * pk_client_set_tid:
 * @client: a valid #PkClient instance
 * @tid: a transaction id
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * This method sets the transaction ID that should be used for the DBUS method
 * and then watched for any callback signals.
 * You cannot call pk_client_set_tid multiple times for one instance.
 *
 * Return value: %TRUE if set correctly
 **/
gboolean
pk_client_set_tid (PkClient *client, const gchar *tid, GError **error)
{
	DBusGProxy *proxy = NULL;
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	if (client->priv->tid != NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_ALREADY_TID, "cannot set the tid on an already set client");
		goto out;
	}

	/* get a connection */
	proxy = dbus_g_proxy_new_for_name (client->priv->connection,
					   PK_DBUS_SERVICE, tid, PK_DBUS_INTERFACE_TRANSACTION);
	if (proxy == NULL) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_ALREADY_TID, "Cannot connect to PackageKit tid %s", tid);
		goto out;
	}

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (proxy, INT_MAX);

	client->priv->tid = g_strdup (tid);
	egg_debug ("set tid %s on %p", client->priv->tid, client);

	dbus_g_proxy_add_signal (proxy, "Finished",
				 G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "ProgressChanged",
				 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "StatusChanged",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Package",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Transaction",
				 G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING,
				 G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "UpdateDetail",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "DistroUpgrade",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Details",
				 G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64,
				 G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Files", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "RepoSignatureRequired",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "EulaRequired",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "RepoDetail", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "ErrorCode", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "RequireRestart", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Message", G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "CallerActiveChanged", G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "AllowCancel", G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Destroy", G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "Category", G_TYPE_STRING, G_TYPE_STRING,
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (proxy, "MediaChangeRequired",
				 G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	dbus_g_proxy_connect_signal (proxy, "Finished",
				     G_CALLBACK (pk_client_finished_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "ProgressChanged",
				     G_CALLBACK (pk_client_progress_changed_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "StatusChanged",
				     G_CALLBACK (pk_client_status_changed_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "Package",
				     G_CALLBACK (pk_client_package_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "Transaction",
				     G_CALLBACK (pk_client_transaction_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "UpdateDetail",
				     G_CALLBACK (pk_client_update_detail_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "DistroUpgrade",
				     G_CALLBACK (pk_client_distro_upgrade_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "Details",
				     G_CALLBACK (pk_client_details_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "Files",
				     G_CALLBACK (pk_client_files_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "RepoSignatureRequired",
				     G_CALLBACK (pk_client_repo_signature_required_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "EulaRequired",
				     G_CALLBACK (pk_client_eula_required_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "RepoDetail",
				     G_CALLBACK (pk_client_repo_detail_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "ErrorCode",
				     G_CALLBACK (pk_client_error_code_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "RequireRestart",
				     G_CALLBACK (pk_client_require_restart_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "Message",
				     G_CALLBACK (pk_client_message_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "CallerActiveChanged",
				     G_CALLBACK (pk_client_caller_active_changed_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "AllowCancel",
				     G_CALLBACK (pk_client_allow_cancel_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "Category",
				     G_CALLBACK (pk_client_category_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "MediaChangeRequired",
				     G_CALLBACK (pk_client_media_change_required_cb), client, NULL);
	dbus_g_proxy_connect_signal (proxy, "Destroy",
				     G_CALLBACK (pk_client_destroy_cb), client, NULL);
	client->priv->proxy = proxy;
	ret = TRUE;
out:
	return ret;
}

/**
 * pk_client_get_property:
 **/
static void
pk_client_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkClient *client;
	client = PK_CLIENT (object);
	switch (prop_id) {
	case PROP_ROLE:
		g_value_set_uint (value, client->priv->role);
		break;
	case PROP_STATUS:
		g_value_set_uint (value, client->priv->status);
		break;
	case PROP_EXIT:
		g_value_set_uint (value, client->priv->exit);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_client_set_property:
 **/
static void
pk_client_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_client_class_init:
 **/
static void
pk_client_class_init (PkClientClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_client_finalize;
	object_class->get_property = pk_client_get_property;
	object_class->set_property = pk_client_set_property;

	/**
	 * PkClient:role:
	 */
	pspec = g_param_spec_uint ("role", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_ROLE, pspec);

	/**
	 * PkClient:status:
	 */
	pspec = g_param_spec_uint ("status", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_STATUS, pspec);

	/**
	 * PkClient:exit:
	 */
	pspec = g_param_spec_uint ("exit", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_EXIT, pspec);

	/**
	 * PkClient::status-changed:
	 * @client: the #PkClient instance that emitted the signal
	 * @status: the #PkStatusEnum type, e.g. PK_STATUS_ENUM_REMOVE
	 *
	 * The ::status-changed signal is emitted when the transaction status
	 * has changed.
	 **/
	signals [SIGNAL_STATUS_CHANGED] =
		g_signal_new ("status-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, status_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	/**
	 * PkClient::progress-changed:
	 * @client: the #PkClient instance that emitted the signal
	 * @percentage: the percentage of the transaction
	 * @subpercentage: the percentage of the sub-transaction
	 * @elapsed: the elapsed time in seconds of the transaction
	 * @client: the remaining time in seconds of the transaction
	 *
	 * The ::progress-changed signal is emitted when the update list may have
	 * changed and the client program may have to update some UI.
	 **/
	signals [SIGNAL_PROGRESS_CHANGED] =
		g_signal_new ("progress-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, progress_changed),
			      NULL, NULL, pk_marshal_VOID__UINT_UINT_UINT_UINT,
			      G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT);

	/**
	 * PkClient::package:
	 * @client: the #PkClient instance that emitted the signal
	 * @obj: a pointer to a #PkPackageObj structure describing the package
	 *
	 * The ::package signal is emitted when the update list may have
	 * changed and the client program may have to update some UI.
	 **/
	signals [SIGNAL_PACKAGE] =
		g_signal_new ("package",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, package),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	/**
	 * PkClient::transaction:
	 * @client: the #PkClient instance that emitted the signal
	 * @obj: a pointer to a #PkTransactionObj structure describing the transaction
	 *
	 * The ::transaction is emitted when the method GetOldTransactions() is
	 * called, and the values are being replayed from a database.
	 **/
	signals [SIGNAL_TRANSACTION] =
		g_signal_new ("transaction",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, transaction),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	/**
	 * PkClient::distro_upgrade:
	 * @client: the #PkClient instance that emitted the signal
	 * @obj: a pointer to a #PkDistroUpgradeObj structure describing the upgrade
	 *
	 * The ::distro_upgrade signal is emitted when the method GetDistroUpgrades() is
	 * called, and the upgrade options are being sent.
	 **/
	signals [SIGNAL_DISTRO_UPGRADE] =
		g_signal_new ("distro-upgrade",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, distro_upgrade),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	/**
	 * PkClient::update-detail:
	 * @client: the #PkClient instance that emitted the signal
	 * @obj: a pointer to a #PkUpdateDetailsObj structure describing the update
	 *
	 * The ::update-detail signal is emitted when GetUpdateDetail() is
	 * called on a set of package_id's.
	 **/
	signals [SIGNAL_UPDATE_DETAIL] =
		g_signal_new ("update-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, update_detail),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	/**
	 * PkClient::details:
	 * @client: the #PkClient instance that emitted the signal
	 * @obj: a pointer to a #PkDetailObj structure describing the package in detail
	 *
	 * The ::details signal is emitted when GetDetails() is called.
	 **/
	signals [SIGNAL_DETAILS] =
		g_signal_new ("details",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, details),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	/**
	 * PkClient::files:
	 * @package_id: the package_id of the package
	 * @files: the list of files owned by the package, delimited by ';'
	 *
	 * The ::files signal is emitted when the method GetFiles() is used.
	 **/
	signals [SIGNAL_FILES] =
		g_signal_new ("files",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, files),
			      NULL, NULL, pk_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_STRING);
	/**
	 * PkClient::repo-signature-required:
	 * @client: the #PkClient instance that emitted the signal
	 * @package_id: the package_id of the package
	 * @repository_name: the name of the repository
	 * @key_url: the URL of the repository
	 * @key_userid: the user signing the repository
	 * @key_id: the id of the repository
	 * @key_fingerprint: the fingerprint of the repository
	 * @key_timestamp: the timestamp of the repository
	 * @type: the #PkSigTypeEnum of the repository, e.g. PK_SIGTYPE_ENUM_GPG
	 *
	 * The ::repo-signature-required signal is emitted when the transaction
	 * needs to fail for a signature prompt.
	 **/
	signals [SIGNAL_REPO_SIGNATURE_REQUIRED] =
		g_signal_new ("repo-signature-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, repo_signature_required),
			      NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_UINT,
			      G_TYPE_NONE, 8, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);
	/**
	 * PkClient::eula-required:
	 * @client: the #PkClient instance that emitted the signal
	 * @eula_id: the EULA id, e.g. <literal>vmware5_single_user</literal>
	 * @package_id: the package_id of the package
	 * @vendor_name: the Vendor name, e.g. Acme Corp.
	 * @license_agreement: the text of the license agreement
	 *
	 * The ::eula signal is emitted when the transaction needs to fail for a EULA prompt.
	 **/
	signals [SIGNAL_EULA_REQUIRED] =
		g_signal_new ("eula-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, eula_required),
			      NULL, NULL, pk_marshal_VOID__STRING_STRING_STRING_STRING,
			      G_TYPE_NONE, 4, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

	/**
	 * PkClient::media-change-required:
	 * @client: the #PkClient instance that emitted the signal
	 * @media_type: the #PkMediaTypeEnum of the error, e.g. PK_MEDIA_TYPE_ENUM_DVD
	 * @media_id: the non-localised label of the media
	 * @media_text: the non-localised text describing the media
	 *
	 * The ::media-change-required signal is emitted when the transaction needs a
	 * different media to grab the packages.
	 *
	 * This can only happen once in a transaction.
	 **/
	signals [SIGNAL_MEDIA_CHANGE_REQUIRED] =
		g_signal_new ("media-change-required",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, media_change_required),
			      NULL, NULL, pk_marshal_VOID__UINT_STRING_STRING,
			      G_TYPE_NONE, 3, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING);

	/**
	 * PkClient::repo-detail:
	 * @client: the #PkClient instance that emitted the signal
	 * @repo_id: the ID of the repository
	 * @description: the description of the repository
	 * @enabled: if the repository is enabled
	 *
	 * The ::repo-detail signal is emitted when the method GetRepos() is
	 * called.
	 **/
	signals [SIGNAL_REPO_DETAIL] =
		g_signal_new ("repo-detail",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, repo_detail),
			      NULL, NULL, pk_marshal_VOID__STRING_STRING_BOOLEAN,
			      G_TYPE_NONE, 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
	/**
	 * PkClient::error-code:
	 * @client: the #PkClient instance that emitted the signal
	 * @code: the #PkErrorCodeEnum of the error, e.g. PK_ERROR_ENUM_DEP_RESOLUTION_FAILED
	 * @details: the non-locaised details about the error
	 *
	 * The ::error-code signal is emitted when the transaction wants to
	 * convey an error in the transaction.
	 *
	 * This can only happen once in a transaction.
	 **/
	signals [SIGNAL_ERROR_CODE] =
		g_signal_new ("error-code",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, error_code),
			      NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	/**
	 * PkClient::require-restart:
	 * @client: the #PkClient instance that emitted the signal
	 * @obj: a pointer to a #PkRequireRestartObj structure describing the restart request in detail
	 *
	 * The ::require-restart signal is emitted when the transaction
	 * requires a application or session restart.
	 **/
	signals [SIGNAL_REQUIRE_RESTART] =
		g_signal_new ("require-restart",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, require_restart),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	/**
	 * PkClient::message:
	 * @client: the #PkClient instance that emitted the signal
	 * @message: the PkMessageEnum type of the message, e.g. %PK_MESSAGE_ENUM_BROKEN_MIRROR
	 * @details: the non-localised message details
	 *
	 * The ::message signal is emitted when the transaction wants to tell
	 * the user something.
	 **/
	signals [SIGNAL_MESSAGE] =
		g_signal_new ("message",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, message),
			      NULL, NULL, pk_marshal_VOID__UINT_STRING,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_STRING);
	/**
	 * PkClient::allow-cancel:
	 * @client: the #PkClient instance that emitted the signal
	 * @allow_cancel: If cancel would succeed
	 *
	 * The ::allow-cancel signal is emitted when the transaction cancellable
	 * value changes.
	 *
	 * You probably want to enable and disable cancel buttons according to
	 * this value.
	 **/
	signals [SIGNAL_ALLOW_CANCEL] =
		g_signal_new ("allow-cancel",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, allow_cancel),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	/**
	 * PkClient::caller-active-changed:
	 * @client: the #PkClient instance that emitted the signal
	 * @is_active: if the caller is still active
	 *
	 * The ::caller-active-changed signal is emitted when the client that
	 * issued the dbus method is exited.
	 **/
	signals [SIGNAL_CALLER_ACTIVE_CHANGED] =
		g_signal_new ("caller-active-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, caller_active_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	/**
	 * PkClient::category:
	 * @client: the #PkClient instance that emitted the signal
	 * @obj: a pointer to a #PkCategoryObj structure describing the category
	 *
	 * The ::category signal is emitted when GetCategories() is called.
	 **/
	signals [SIGNAL_CATEGORY] =
		g_signal_new ("category",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, category),
			      NULL, NULL, g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE, 1, G_TYPE_POINTER);
	/**
	 * PkClient::finished:
	 * @client: the #PkClient instance that emitted the signal
	 * @exit: the #PkExitEnum status value, e.g. PK_EXIT_ENUM_SUCCESS
	 * @runtime: the time in seconds the transaction has been running
	 *
	 * The ::finished signal is emitted when the transaction is complete.
	 **/
	signals [SIGNAL_FINISHED] =
		g_signal_new ("finished",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, finished),
			      NULL, NULL, pk_marshal_VOID__UINT_UINT,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_UINT);

	/**
	 * PkClient::destroy:
	 * @client: the #PkClient instance that emitted the signal
	 *
	 * The ::destroy signal is emitted when the transaction has been
	 * destroyed and is no longer available for use.
	 **/
	signals [SIGNAL_DESTROY] =
		g_signal_new ("destroy",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkClientClass, destroy),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);

	g_type_class_add_private (klass, sizeof (PkClientPrivate));
}

/**
 * pk_client_connect:
 * @client: a valid #PkClient instance
 **/
static void
pk_client_connect (PkClient *client)
{
	egg_debug ("connect");
}

/**
 * pk_connection_changed_cb:
 **/
static void
pk_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkClient *client)
{
	/* if PK re-started mid-transaction then show a big fat warning */
	if (!connected && client->priv->tid != NULL && !client->priv->is_finished)
		egg_warning ("daemon disconnected mid-transaction!");
}

/**
 * pk_client_disconnect_proxy:
 **/
static gboolean
pk_client_disconnect_proxy (PkClient *client)
{
	if (client->priv->proxy == NULL)
		return FALSE;
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Finished",
					G_CALLBACK (pk_client_finished_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "ProgressChanged",
					G_CALLBACK (pk_client_progress_changed_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "StatusChanged",
					G_CALLBACK (pk_client_status_changed_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Package",
					G_CALLBACK (pk_client_package_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Transaction",
					G_CALLBACK (pk_client_transaction_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "DistroUpgrade",
					G_CALLBACK (pk_client_distro_upgrade_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Details",
					G_CALLBACK (pk_client_details_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Files",
					G_CALLBACK (pk_client_files_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "RepoSignatureRequired",
					G_CALLBACK (pk_client_repo_signature_required_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "EulaRequired",
					G_CALLBACK (pk_client_eula_required_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "ErrorCode",
					G_CALLBACK (pk_client_error_code_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "RequireRestart",
					G_CALLBACK (pk_client_require_restart_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Message",
					G_CALLBACK (pk_client_message_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "CallerActiveChanged",
					G_CALLBACK (pk_client_caller_active_changed_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "AllowCancel",
					G_CALLBACK (pk_client_allow_cancel_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "Destroy",
					G_CALLBACK (pk_client_destroy_cb), client);
	dbus_g_proxy_disconnect_signal (client->priv->proxy, "MediaChangeRequired",
					G_CALLBACK (pk_client_media_change_required_cb), client);
	g_object_unref (G_OBJECT (client->priv->proxy));
	client->priv->proxy = NULL;
	return TRUE;
}

/**
 * pk_client_reset:
 * @client: a valid #PkClient instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Resetting the client way be needed if we canceled the request without
 * waiting for ::finished, or if we want to reuse the #PkClient without
 * unreffing and creating it again.
 *
 * If you call pk_client_reset() on a running transaction, then it will be
 * automatically cancelled. If the cancel fails, the reset will fail.
 *
 * Return value: %TRUE if we reset the client
 **/
gboolean
pk_client_reset (PkClient *client, GError **error)
{
	gboolean ret = FALSE;

	g_return_val_if_fail (PK_IS_CLIENT (client), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* we cannot reset a synchronous client in ::Finished() --
	   the whole point of a sync client is we don't handle this signal
	   and we'll clear the package cache if we allow this */
	if (client->priv->is_finishing && client->priv->synchronous) {
		if (error != NULL)
			*error = g_error_new (PK_CLIENT_ERROR, PK_CLIENT_ERROR_FAILED, "unable to reset synchronous client in finished handler");
		goto out;
	}

	if (client->priv->tid != NULL && !client->priv->is_finished) {
		egg_debug ("not exit status, will try to cancel tid %s", client->priv->tid);
		/* we try to cancel the running tranaction */
		ret = pk_client_cancel (client, error);
		if (!ret)
			goto out;
	}

	/* stop the timeout timer if running */
	if (client->priv->timeout_id != 0) {
		g_source_remove (client->priv->timeout_id);
		client->priv->timeout_id = 0;
	}

	g_free (client->priv->tid);
	g_free (client->priv->cached_package_id);
	g_free (client->priv->cached_key_id);
	g_free (client->priv->cached_transaction_id);
	g_free (client->priv->cached_full_path);
	g_free (client->priv->cached_search);
	g_free (client->priv->cached_directory);
	g_strfreev (client->priv->cached_package_ids);
	g_strfreev (client->priv->cached_full_paths);
	g_object_unref (client->priv->package_list);
	g_clear_error (&client->priv->error);

	/* we need to do this now we have multiple paths */
	pk_client_disconnect_proxy (client);

	client->priv->tid = NULL;
	client->priv->cached_package_id = NULL;
	client->priv->cached_key_id = NULL;
	client->priv->cached_transaction_id = NULL;
	client->priv->cached_full_path = NULL;
	client->priv->cached_full_paths = NULL;
	client->priv->cached_search = NULL;
	client->priv->cached_package_ids = NULL;
	client->priv->cached_directory = NULL;
	client->priv->cached_filters = PK_FILTER_ENUM_UNKNOWN;
	client->priv->status = PK_STATUS_ENUM_UNKNOWN;
	client->priv->role = PK_ROLE_ENUM_UNKNOWN;
	client->priv->is_finished = FALSE;
	client->priv->timeout = -1;
	client->priv->package_list = pk_package_list_new ();

	/* TODO: make clean */
	pk_obj_list_clear (client->priv->category_list);
	pk_obj_list_clear (client->priv->distro_upgrade_list);
	pk_obj_list_clear (client->priv->transaction_list);
	pk_obj_list_clear (client->priv->require_restart_list);
	ret = TRUE;
out:
	return ret;
}

/**
 * pk_client_init:
 **/
static void
pk_client_init (PkClient *client)
{
	GError *error = NULL;

	client->priv = PK_CLIENT_GET_PRIVATE (client);
	client->priv->tid = NULL;
	client->priv->loop = g_main_loop_new (NULL, FALSE);
	client->priv->use_buffer = FALSE;
	client->priv->synchronous = FALSE;
	client->priv->status = PK_STATUS_ENUM_UNKNOWN;
	client->priv->require_restart = PK_RESTART_ENUM_NONE;
	client->priv->role = PK_ROLE_ENUM_UNKNOWN;
	client->priv->exit = PK_EXIT_ENUM_UNKNOWN;
	client->priv->is_finished = FALSE;
	client->priv->is_finishing = FALSE;
	client->priv->package_list = pk_package_list_new ();
	client->priv->cached_package_id = NULL;
	client->priv->cached_package_ids = NULL;
	client->priv->cached_transaction_id = NULL;
	client->priv->cached_key_id = NULL;
	client->priv->cached_full_path = NULL;
	client->priv->cached_full_paths = NULL;
	client->priv->cached_search = NULL;
	client->priv->cached_directory = NULL;
	client->priv->cached_provides = PK_PROVIDES_ENUM_UNKNOWN;
	client->priv->cached_filters = PK_FILTER_ENUM_UNKNOWN;
	client->priv->proxy = NULL;
	client->priv->timeout = -1;
	client->priv->timeout_id = 0;
	client->priv->error = NULL;

	/* cache require restart objects */
	client->priv->require_restart_list = pk_obj_list_new ();
	pk_obj_list_set_copy (client->priv->require_restart_list, (PkObjListCopyFunc) pk_require_restart_obj_copy);
	pk_obj_list_set_free (client->priv->require_restart_list, (PkObjListFreeFunc) pk_require_restart_obj_free);

	/* cache category objects */
	client->priv->category_list = pk_obj_list_new ();
	pk_obj_list_set_copy (client->priv->category_list, (PkObjListCopyFunc) pk_category_obj_copy);
	pk_obj_list_set_free (client->priv->category_list, (PkObjListFreeFunc) pk_category_obj_free);

	/* cache distro upgrade objects */
	client->priv->distro_upgrade_list = pk_obj_list_new ();
	pk_obj_list_set_copy (client->priv->distro_upgrade_list, (PkObjListCopyFunc) pk_distro_upgrade_obj_copy);
	pk_obj_list_set_free (client->priv->distro_upgrade_list, (PkObjListFreeFunc) pk_distro_upgrade_obj_free);

	/* cache transaction objects */
	client->priv->transaction_list = pk_obj_list_new ();
	pk_obj_list_set_copy (client->priv->transaction_list, (PkObjListCopyFunc) pk_transaction_obj_copy);
	pk_obj_list_set_free (client->priv->transaction_list, (PkObjListFreeFunc) pk_transaction_obj_free);

	/* check dbus connections, exit if not valid */
	client->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("%s", error->message);
		g_error_free (error);
		g_error ("Could not connect to system DBUS.");
	}

	/* watch for PackageKit on the bus, and try to connect up at start */
	client->priv->pconnection = pk_connection_new ();
	client->priv->pconnection_signal_id = g_signal_connect (client->priv->pconnection, "connection-changed",
								G_CALLBACK (pk_connection_changed_cb), client);
	if (pk_connection_valid (client->priv->pconnection))
		pk_client_connect (client);

	/* Use a main control object */
	client->priv->control = pk_control_new ();

	/* DistroUpgrade, MediaChangeRequired */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* ProgressChanged */
	dbus_g_object_register_marshaller (pk_marshal_VOID__UINT_UINT_UINT_UINT,
					   G_TYPE_NONE, G_TYPE_UINT,
					   G_TYPE_UINT, G_TYPE_UINT, G_TYPE_UINT, G_TYPE_INVALID);

	/* AllowCancel */
	dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__BOOLEAN,
					   G_TYPE_NONE, G_TYPE_BOOLEAN, G_TYPE_INVALID);

	/* StatusChanged */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_INVALID);

	/* Finished */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_UINT,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_INVALID);

	/* ErrorCode, RequireRestart, Message */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* CallerActiveChanged */
	dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__BOOLEAN,
					   G_TYPE_NONE, G_TYPE_BOOLEAN, G_TYPE_INVALID);

	/* Details */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_UINT64,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64,
					   G_TYPE_INVALID);

	/* EulaRequired */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* Files */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_INVALID);

	/* RepoSignatureRequired */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);

	/* Package */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_INVALID);

	/* RepoDetail */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_BOOL,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_BOOLEAN, G_TYPE_INVALID);

	/* UpdateDetail */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
	/* Transaction */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_BOOL_STRING_UINT_STRING_UINT_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN,
					   G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING,
					   G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID);
	/* Category */
	dbus_g_object_register_marshaller (pk_marshal_VOID__STRING_STRING_STRING_STRING_STRING,
					   G_TYPE_NONE, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
					   G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INVALID);
}

/**
 * pk_client_finalize:
 **/
static void
pk_client_finalize (GObject *object)
{
	PkClient *client;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_CLIENT (object));
	client = PK_CLIENT (object);
	g_return_if_fail (client->priv != NULL);

	/* free cached strings */
	g_free (client->priv->cached_package_id);
	g_free (client->priv->cached_key_id);
	g_free (client->priv->cached_transaction_id);
	g_free (client->priv->cached_full_path);
	g_free (client->priv->cached_search);
	g_free (client->priv->cached_directory);
	g_free (client->priv->tid);
	g_strfreev (client->priv->cached_package_ids);
	g_strfreev (client->priv->cached_full_paths);
	if (client->priv->error)
		g_error_free (client->priv->error);

	/* clear the loop, if we were using it */
	if (client->priv->synchronous)
		g_main_loop_quit (client->priv->loop);
	g_main_loop_unref (client->priv->loop);

	/* stop the timeout timer if running */
	if (client->priv->timeout_id != 0)
		g_source_remove (client->priv->timeout_id);

	/* disconnect signal handlers */
	g_signal_handler_disconnect (client->priv->pconnection, client->priv->pconnection_signal_id);
	pk_client_disconnect_proxy (client);
	g_object_unref (client->priv->pconnection);
	g_object_unref (client->priv->package_list);
	g_object_unref (client->priv->category_list);
	g_object_unref (client->priv->distro_upgrade_list);
	g_object_unref (client->priv->transaction_list);
	g_object_unref (client->priv->require_restart_list);
	g_object_unref (client->priv->control);

	G_OBJECT_CLASS (pk_client_parent_class)->finalize (object);
}

/**
 * pk_client_new:
 *
 * PkClient is a nice GObject wrapper for PackageKit and makes writing
 * frontends easy.
 *
 * Return value: A new %PkClient instance
 **/
PkClient *
pk_client_new (void)
{
	PkClient *client;
	client = g_object_new (PK_TYPE_CLIENT, NULL);
	return PK_CLIENT (client);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"
#include <glib/gstdio.h>

static gboolean finished = FALSE;
static gboolean reset_okay = FALSE;
static guint clone_packages = 0;

static void
pk_client_test_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, EggTest *test)
{
	finished = TRUE;
	/* this is actually quite common */
	g_object_unref (client);
}

static void
pk_client_test_finished2_cb (PkClient *client, PkExitEnum exit, guint runtime, EggTest *test)
{
	GError *error = NULL;
	/* this is supported */
	reset_okay = pk_client_reset (client, &error);
	if (!reset_okay) {
		egg_warning ("failed to reset in ::Finished(): %s", error->message);
		g_error_free (error);
	}
}

static void
pk_client_test_copy_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, EggTest *test)
{
	egg_test_loop_quit (test);
}

static void
pk_client_test_copy_package_cb (PkClient *client, const PkPackageObj *obj, EggTest *test)
{
	clone_packages++;
}

void
pk_client_test (EggTest *test)
{
	PkConnection *connection;
	PkClient *client;
	PkClient *client_copy;
	gboolean ret;
	GError *error = NULL;
	gchar *tid;
	guint size;
	guint size_new;
	guint i;
	gchar *file;
	PkPackageList *list;
	PkRoleEnum role;

	if (!egg_test_start (test, "PkClient"))
		return;

	/* check to see if there is a daemon running */
	connection = pk_connection_new ();
	ret = pk_connection_valid (connection);
	g_object_unref (connection);
	if (!ret) {
		egg_warning ("daemon is not running, skipping tests");
		goto out;
	}

	/************************************************************/
	egg_test_title (test, "test resolve NULL");
	file = pk_resolve_local_path (NULL);
	if (file == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "test resolve /etc/hosts");
	file = pk_resolve_local_path ("/etc/hosts");
	if (file != NULL && g_strcmp0 (file, "/etc/hosts") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got: %s", file);
	g_free (file);

	/************************************************************/
	egg_test_title (test, "test resolve /etc/../etc/hosts");
	file = pk_resolve_local_path ("/etc/../etc/hosts");
	if (file != NULL && g_strcmp0 (file, "/etc/hosts") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got: %s", file);
	g_free (file);

	/************************************************************/
	egg_test_title (test, "get client");
	client = pk_client_new ();
	egg_test_assert (test, client != NULL);

	/************************************************************/
	egg_test_title (test, "set non synchronous (fail)");
	ret = pk_client_set_synchronous (client, FALSE, NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "set synchronous (pass)");
	ret = pk_client_set_synchronous (client, TRUE, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "set synchronous again (fail)");
	ret = pk_client_set_synchronous (client, TRUE, NULL);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "set non synchronous (pass)");
	ret = pk_client_set_synchronous (client, FALSE, NULL);
	egg_test_assert (test, ret);
	g_object_unref (client);

	/************************************************************/
	egg_test_title (test, "get new client");
	client = pk_client_new ();
	egg_test_assert (test, client != NULL);

	/************************************************************/
	egg_test_title (test, "reset client, unused");
	ret = pk_client_reset (client, NULL);
	egg_test_assert (test, ret);

	/* check use after finalise */
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_client_test_finished_cb), test);

	/************************************************************/
	egg_test_title (test, "set synchronous after reset (pass)");
	ret = pk_client_set_synchronous (client, TRUE, NULL);
	egg_test_assert (test, ret);

	/* run the method */
	ret = pk_client_search_name (client, PK_FILTER_ENUM_NONE, "power", NULL);

	/************************************************************/
	egg_test_title (test, "we finished?");
	if (!ret)
		egg_test_failed (test, "not correct return value");
	else if (!finished)
		egg_test_failed (test, "not finished");
	else
		egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "get new client so we can test resets in ::Finished()");
	client = pk_client_new ();
	egg_test_assert (test, client != NULL);

	/* check reset during finalise when sync */
	pk_client_set_synchronous (client, TRUE, NULL);
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_client_test_finished2_cb), test);

	/************************************************************/
	egg_test_title (test, "search name sync, with a reset in finalise");
	ret = pk_client_search_name (client, PK_FILTER_ENUM_NONE, "power", NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "check reset failed");
	if (!reset_okay)
		egg_test_success (test, "failed to reset in finished as sync");
	else
		egg_test_failed (test, "reset in finished when sync");

	g_object_unref (client);

	/************************************************************/
	egg_test_title (test, "get new client");
	client = pk_client_new ();
	egg_test_assert (test, client != NULL);
	pk_client_set_synchronous (client, TRUE, NULL);
	pk_client_set_use_buffer (client, TRUE, NULL);

	/************************************************************/
	egg_test_title (test, "reset client #1");
	ret = pk_client_reset (client, &error);
	if (!ret) {
		egg_test_failed (test, "failed to reset: %s", error->message);
		g_error_free (error);
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "reset client #2");
	ret = pk_client_reset (client, &error);
	if (!ret) {
		egg_test_failed (test, "failed to reset: %s", error->message);
		g_error_free (error);
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "get updates");
	ret = pk_client_get_updates (client, PK_FILTER_ENUM_NONE, &error);
	if (!ret) {
		egg_test_failed (test, "failed to get updates: %s", error->message);
		g_error_free (error);
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "get updates (without reset) with null error");
	ret = pk_client_get_updates (client, PK_FILTER_ENUM_NONE, NULL);
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got updates with no reset (no description possible)");

	/************************************************************/
	egg_test_title (test, "reset client #2");
	ret = pk_client_reset (client, &error);
	if (!ret) {
		egg_test_failed (test, "failed to reset: %s", error->message);
		g_error_free (error);
	}
	egg_test_success (test, NULL);

	/************************************************************/
	egg_test_title (test, "search for power");
	ret = pk_client_search_name (client, PK_FILTER_ENUM_NONE, "power", &error);
	if (!ret) {
		egg_test_failed (test, "failed: %s", error->message);
		g_error_free (error);
	}

	/* get size */
	list = pk_client_get_package_list (client);
	size = pk_package_list_get_size (list);
	g_object_unref (list);
	if (size == 0)
		egg_test_failed (test, "failed: to get any results");
	egg_test_success (test, "search name with %i entries", size);

	/************************************************************/
	egg_test_title (test, "do lots of loops");
	for (i=0;i<5;i++) {
		ret = pk_client_reset (client, &error);
		if (!ret) {
			egg_test_failed (test, "failed: to reset: %s", error->message);
			g_error_free (error);
		}
		ret = pk_client_search_name (client, PK_FILTER_ENUM_NONE, "power", &error);
		if (!ret) {
			egg_test_failed (test, "failed to search: %s", error->message);
			g_error_free (error);
		}
		/* check we got the same results */
		list = pk_client_get_package_list (client);
		size_new = pk_package_list_get_size (list);
		g_object_unref (list);
		if (size != size_new)
			egg_test_failed (test, "old size %i, new size %i", size, size_new);
	}
	egg_test_success (test, "%i search name loops completed in %ims", i, egg_test_elapsed (test));
	g_object_unref (client);

	/************************************************************/
	egg_test_title (test, "try to clone");

	/* set up the source */
	client = pk_client_new ();
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_client_test_copy_finished_cb), test);
	/* set up the copy */
	client_copy = pk_client_new ();
	g_signal_connect (client_copy, "package",
			  G_CALLBACK (pk_client_test_copy_package_cb), test);

	/* search with the source */
	ret = pk_client_search_name (client, PK_FILTER_ENUM_NONE, "power", &error);
	if (!ret) {
		egg_test_failed (test, "failed: %s", error->message);
		g_error_free (error);
	}

	/* get the tid */
	tid = pk_client_get_tid (client);
	if (tid == NULL)
		egg_test_failed (test, "failed to get tid");

	/* set the tid on the copy */
	ret = pk_client_set_tid (client_copy, tid, &error);
	if (!ret) {
		egg_test_failed (test, "failed to set tid: %s", error->message);
		g_error_free (error);
		error = NULL;
	}
	g_free (tid);

	egg_test_loop_wait (test, 5000);
	if (clone_packages != size_new)
		egg_test_failed (test, "failed to get correct number of packages: %i", clone_packages);
	egg_test_success (test, "cloned in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "cancel a finished task");
	ret = pk_client_cancel (client, &error);
	if (ret) {
		if (error != NULL)
			egg_test_failed (test, "error set and retval true");
		egg_test_success (test, "did not cancel finished task");
	} else {
		egg_test_failed (test, "error %s", error->message);
		g_error_free (error);
	}

	/************************************************************/
	egg_test_title (test, "ensure task still has correct role after cancel");
	pk_client_get_role (client, &role, NULL, NULL);
	if (role == PK_ROLE_ENUM_SEARCH_NAME)
		egg_test_success (test, "did not cancel finished task");
	else
		egg_test_failed (test, "role was %s", pk_role_enum_to_text (role));

	g_object_unref (client);
	g_object_unref (client_copy);

	/************************************************************/
	egg_test_title (test, "set a made up TID");
	client = pk_client_new ();
	ret = pk_client_set_tid (client, "/made_up_tid", &error);
	if (ret) {
		if (error != NULL)
			egg_test_failed (test, "error set and retval true");
		egg_test_success (test, NULL);
	} else {
		if (error == NULL)
			egg_test_failed (test, "error not set and retval false");
		egg_test_failed (test, "error %s", error->message);
		g_error_free (error);
		error = NULL;
	}

	/************************************************************/
	egg_test_title (test, "cancel a non running task");
	ret = pk_client_cancel (client, &error);
	if (ret) {
		if (error != NULL)
			egg_test_failed (test, "error set and retval true");
		egg_test_success (test, "did not cancel non running task");
	} else {
		if (error == NULL)
			egg_test_failed (test, "error not set and retval false");
		egg_test_failed (test, "error %s", error->message);
		g_error_free (error);
		error = NULL;
	}
	g_object_unref (client);

	/************************************************************
	 ****************         TIMEOUTS         ******************
	 ************************************************************/
	client = pk_client_new ();
	g_signal_connect (client, "finished",
			  G_CALLBACK (pk_client_test_copy_finished_cb), test);
	client_copy = pk_client_new ();
	g_signal_connect (client_copy, "finished",
			  G_CALLBACK (pk_client_test_copy_finished_cb), test);

	/************************************************************/
	egg_test_title (test, "set timeout");
	ret = pk_client_set_timeout (client, 0, NULL);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set timout");

	/************************************************************/
	egg_test_title (test, "set timeout on copy");
	ret = pk_client_set_timeout (client_copy, 500, NULL);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set timout");

	/************************************************************/
	egg_test_title (test, "set timeout (2)");
	ret = pk_client_set_timeout (client, 1000, NULL);
	if (!ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "set timeout when already set");

	/************************************************************/
	egg_test_title (test, "reset client, to clear timeout");
	ret = pk_client_reset (client, NULL);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "set timeout");
	ret = pk_client_set_timeout (client, 0, NULL);
	if (ret)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "failed to set timout");

	/************************************************************/
	egg_test_title (test, "do first task (which will fail after 500ms)");
	ret = pk_client_search_name (client_copy, PK_FILTER_ENUM_NONE, "power", &error);
	if (ret) {
		egg_test_success (test, NULL);
	} else {
		egg_test_failed (test, "failed: %s", error->message);
		g_error_free (error);
	}

	/************************************************************/
	egg_test_title (test, "do second task which should fail outright");
	ret = pk_client_search_name (client, PK_FILTER_ENUM_NONE, "power", &error);
	if (!ret) {
		egg_test_success (test, "failed (in a good way): %s", error->message);
		g_error_free (error);
	} else {
		egg_test_failed (test, "suceeded, which was bad");
	}

	/* 500ms plus breathing room */
	egg_test_loop_wait (test, 600);

	g_object_unref (client);

out:
	egg_test_end (test);
}
#endif

