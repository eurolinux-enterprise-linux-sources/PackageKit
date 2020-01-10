/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2008 Richard Hughes <richard@hughsie.com>
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

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>

#include <string.h>
#include <locale.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif /* HAVE_UNISTD_H */

#include <sys/wait.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <dbus/dbus-glib.h>

#include <packagekit-glib/pk-control.h>
#include <packagekit-glib/pk-client.h>
#include <packagekit-glib/pk-marshal.h>
#include <packagekit-glib/pk-connection.h>
#include <packagekit-glib/pk-common.h>
#include <packagekit-glib/pk-enum.h>
#include <packagekit-glib/pk-version.h>

#include "egg-debug.h"

static void     pk_control_finalize	(GObject     *object);

#define PK_CONTROL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_CONTROL, PkControlPrivate))

/**
 * PkControlPrivate:
 *
 * Private #PkControl data
 **/
struct _PkControlPrivate
{
	DBusGProxy		*proxy;
	DBusGConnection		*connection;
	PkConnection		*pconnection;
	gchar			**array;
	guint			 idle_id;
	gboolean		 version_major;
	gboolean		 version_minor;
	gboolean		 version_micro;
};

enum {
	PK_CONTROL_LOCKED,
	PK_CONTROL_LIST_CHANGED,
	PK_CONTROL_RESTART_SCHEDULE,
	PK_CONTROL_UPDATES_CHANGED,
	PK_CONTROL_REPO_LIST_CHANGED,
	PK_CONTROL_NETWORK_STATE_CHANGED,
	PK_CONTROL_LAST_SIGNAL
};

enum {
	PROP_0,
	PROP_VERSION_MAJOR,
	PROP_VERSION_MINOR,
	PROP_VERSION_MICRO,
	PROP_LAST
};

static guint signals [PK_CONTROL_LAST_SIGNAL] = { 0 };
static gpointer pk_control_object = NULL;

G_DEFINE_TYPE (PkControl, pk_control, G_TYPE_OBJECT)

/**
 * pk_control_error_quark:
 *
 * We are a clever GObject that sets errors
 *
 * Return value: Our personal error quark.
 **/
GQuark
pk_control_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("pk_control_error");
	return quark;
}

/**
 * pk_control_error_set:
 *
 * Sets the correct error code (if allowed) and print to the screen
 * as a warning.
 **/
static gboolean
pk_control_error_set (GError **error, gint code, const gchar *format, ...)
{
	va_list args;
	gchar *buffer = NULL;
	gboolean ret = TRUE;

	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	va_start (args, format);
	g_vasprintf (&buffer, format, args);
	va_end (args);

	/* dumb */
	if (error == NULL) {
		egg_warning ("No error set, so can't set: %s", buffer);
		ret = FALSE;
		goto out;
	}

	/* already set */
	if (*error != NULL) {
		egg_warning ("not NULL error!");
		g_clear_error (error);
	}

	/* propogate */
	g_set_error (error, PK_CONTROL_ERROR, code, "%s", buffer);

out:
	g_free(buffer);
	return ret;
}

/**
 * pk_control_get_actions:
 * @control: a valid #PkControl instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Actions are roles that the daemon can do with the current backend
 *
 * Return value: an enumerated list of the actions the backend supports
 **/
PkBitfield
pk_control_get_actions (PkControl *control, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *actions;
	PkBitfield roles_enum = 0;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_GROUP_ENUM_UNKNOWN);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		egg_warning ("No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetActions", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &actions,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		egg_warning ("GetActions failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* convert to enumerated types */
	roles_enum = pk_role_bitfield_from_text (actions);
	g_free (actions);
out:
	return roles_enum;
}

/**
 * pk_control_set_proxy:
 * @control: a valid #PkControl instance
 * @proxy_http: a HTTP proxy string such as "username:password@server.lan:8080"
 * @proxy_ftp: a FTP proxy string such as "server.lan:8080"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Set a proxy on the PK daemon
 *
 * Return value: if we set the proxy successfully
 **/
gboolean
pk_control_set_proxy (PkControl *control, const gchar *proxy_http, const gchar *proxy_ftp, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_GROUP_ENUM_UNKNOWN);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		egg_warning ("No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "SetProxy", &error_local,
				 G_TYPE_STRING, proxy_http,
				 G_TYPE_STRING, proxy_ftp,
				 G_TYPE_INVALID,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		egg_warning ("SetProxy failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
	}
out:
	return ret;
}

/**
 * pk_control_get_groups:
 * @control: a valid #PkControl instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * The group list is enumerated so it can be localised and have deep
 * integration with desktops.
 * This method allows a frontend to only display the groups that are supported.
 *
 * Return value: an enumerated list of the groups the backend supports
 **/
PkBitfield
pk_control_get_groups (PkControl *control, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *groups;
	PkBitfield groups_enum = 0;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_GROUP_ENUM_UNKNOWN);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		egg_warning ("No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetGroups", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &groups,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		egg_warning ("GetGroups failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* convert to enumerated types */
	groups_enum = pk_group_bitfield_from_text (groups);
	g_free (groups);
out:
	return groups_enum;
}

/**
 * pk_control_get_mime_types:
 * @control: a valid #PkControl instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * The MIME list is the supported package formats.
 *
 * Return value: an strv list of the formats the backend supports,
 * or %NULL if unknown
 **/
gchar **
pk_control_get_mime_types (PkControl *control, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *type_str = NULL;
	gchar **types = NULL;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		egg_warning ("No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetMimeTypes", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &type_str,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		egg_warning ("GetMimeTypes failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* convert to enumerated types */
	types = g_strsplit (type_str, ";", 0);
	g_free (type_str);
out:
	return types;
}

/**
 * pk_control_get_daemon_state:
 * @control: a valid #PkControl instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * The engine state debugging output
 *
 * Return value: a string of debugging data of unspecified format
 **/
gchar *
pk_control_get_daemon_state (PkControl *control, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *state = NULL;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);
	g_return_val_if_fail (error == NULL || *error == NULL, NULL);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		egg_warning ("No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetDaemonState", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &state,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		egg_warning ("GetDaemonState failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	return state;
}

/**
 * pk_control_get_network_state:
 * @control: a valid #PkControl instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Return value: an enumerated network state
 **/
PkNetworkEnum
pk_control_get_network_state (PkControl *control, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *network_state;
	PkNetworkEnum network_state_enum = PK_NETWORK_ENUM_UNKNOWN;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_NETWORK_ENUM_UNKNOWN);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		egg_warning ("No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetNetworkState", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &network_state,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		egg_warning ("GetNetworkState failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* convert to enumerated types */
	network_state_enum = pk_network_enum_from_text (network_state);
	g_free (network_state);
out:
	return network_state_enum;
}

/**
 * pk_control_get_filters:
 * @control: a valid #PkControl instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * Filters are how the backend can specify what type of package is returned.
 *
 * Return value: an enumerated list of the filters the backend supports
 **/
PkBitfield
pk_control_get_filters (PkControl *control, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;
	gchar *filters;
	PkBitfield filters_enum = 0;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_FILTER_ENUM_UNKNOWN);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		egg_warning ("No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetFilters", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &filters,
				 G_TYPE_INVALID);
	if (!ret) {
		/* abort as the DBUS method failed */
		egg_warning ("GetFilters failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* convert to enumerated types */
	filters_enum = pk_filter_bitfield_from_text (filters);
	g_free (filters);
out:
	return filters_enum;
}

/**
 * pk_control_get_backend_detail:
 * @control: a valid #PkControl instance
 * @name: the name of the backend
 * @author: the author of the backend
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * The backend detail is useful for the pk-backend-status program, or for
 * automatic bugreports.
 *
 * Return value: %TRUE if the daemon serviced the request
 **/
gboolean
pk_control_get_backend_detail (PkControl *control, gchar **name, gchar **author, GError **error)
{
	gboolean ret;
	gchar *tname;
	gchar *tauthor;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, "No proxy for manager");
		return FALSE;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetBackendDetail", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &tname,
				 G_TYPE_STRING, &tauthor,
				 G_TYPE_INVALID, G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("GetFilters failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		return FALSE;
	}

	/* copy needed bits */
	if (name != NULL)
		*name = tname;
	else
		g_free (tname);
	/* copy needed bits */
	if (author != NULL)
		*author = tauthor;
	else
		g_free (tauthor);
	return ret;
}

/**
 * pk_control_get_time_since_action:
 * @control: a valid #PkControl instance
 * @role: the role we are querying
 * @seconds: the number of seconds since the request was completed
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * We may want to know how long it has been since we refreshed the cache or
 * retrieved the update list.
 *
 * Return value: %TRUE if the daemon serviced the request
 **/
gboolean
pk_control_get_time_since_action (PkControl *control, PkRoleEnum role, guint *seconds, GError **error)
{
	gboolean ret;
	const gchar *role_text;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	role_text = pk_role_enum_to_text (role);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, "No proxy for manager");
		return FALSE;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetTimeSinceAction", &error_local,
				 G_TYPE_STRING, role_text,
				 G_TYPE_INVALID,
				 G_TYPE_UINT, seconds,
				 G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("GetTimeSinceAction failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
	}
	return ret;
}

/**
 * pk_control_can_authorize:
 * @control: a valid #PkControl instance
 * @action_id: the action ID, e.g. "org.freedesktop.packagekit.system-update"
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * We may want to know before we run a method if we are going to be denied,
 * accepted or challenged for authentication.
 *
 * Return value: the %PkAuthorizeEnum or %PK_AUTHORIZE_ENUM_UNKNOWN if the method failed
 */
PkAuthorizeEnum
pk_control_can_authorize (PkControl *control, const gchar *action_id, GError **error)
{
	gboolean ret = FALSE;
	GError *error_local = NULL;
	gchar *authorize = NULL;
	PkAuthorizeEnum retval = PK_AUTHORIZE_ENUM_UNKNOWN;

	g_return_val_if_fail (PK_IS_CONTROL (control), PK_AUTHORIZE_ENUM_UNKNOWN);
	g_return_val_if_fail (error == NULL || *error == NULL, PK_AUTHORIZE_ENUM_UNKNOWN);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, "No proxy for manager");
		goto out;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "CanAuthorize", &error_local,
				 G_TYPE_STRING, action_id,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &authorize,
				 G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("CanAuthorize failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* convert to enum */
	retval = pk_authorize_type_enum_from_text (authorize);
	if (retval == PK_AUTHORIZE_ENUM_UNKNOWN) {
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, "unexpected return value");
		goto out;
	}
out:
	g_free (authorize);
	return retval;
}

/**
 * pk_control_set_locale:
 * @error: a %GError to put the error code and message in, or %NULL
 **/
static gboolean
pk_control_set_locale (PkControl *control, const gchar *tid, GError **error)
{
	PkClient *client;
	gboolean ret;
	gchar *locale; /* does not need to be freed */
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	client = pk_client_new ();
	ret = pk_client_set_tid (client, tid, &error_local);
	if (!ret) {
		egg_warning ("failed to set the tid: %s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* get the session locale and set th transaction to be in this locale */
	locale = setlocale (LC_ALL, NULL);
	ret = pk_client_set_locale (client, locale, &error_local);
	if (!ret) {
		egg_warning ("SetLocale failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}
out:
	g_object_unref (client);
	return ret;
}

/**
 * pk_control_allocate_transaction_id:
 * @control: a valid #PkControl instance
 * @error: a %GError to put the error code and message in, or %NULL
 *
 * We have to create a transaction ID then use it, as a one-step constructor
 * is inherently racey.
 *
 * Return value: %TRUE if we allocated a TID.
 **/
gboolean
pk_control_allocate_transaction_id (PkControl *control, gchar **tid, GError **error)
{
	gboolean ret;
	gchar *tid_local = NULL;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* check to see if we have a valid proxy */
	if (control->priv->proxy == NULL) {
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, "No proxy for GetTid");
		return FALSE;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetTid", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_STRING, &tid_local,
				 G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("GetTid failed :%s", error_local->message);
		if (error_local->code == DBUS_GERROR_SPAWN_CHILD_EXITED)
			pk_control_error_set (error, PK_CONTROL_ERROR_CANNOT_START_DAEMON, "cannot GetTid: %s", error_local->message);
		else
			pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* check we are not running new client tools with an old server */
	if (g_strrstr (tid_local, ";") != NULL) {
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, "Incorrect path with ';' returned!");
		ret = FALSE;
		goto out;
	}

	/* automatically set the locale */
	ret = pk_control_set_locale (control, tid_local, &error_local);
	if (!ret) {
		egg_warning ("GetTid failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
		goto out;
	}

	/* copy */
	*tid = g_strdup (tid_local);
	egg_debug ("Got tid: '%s'", tid_local);
out:
	g_free (tid_local);
	return ret;
}

/**
 * pk_control_transaction_list_print:
 **/
gboolean
pk_control_transaction_list_print (PkControl *control)
{
	guint i;
	gchar *tid;
	guint length;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);

	length = g_strv_length (control->priv->array);
	if (length == 0)
		return TRUE;
	egg_debug ("jobs:");
	for (i=0; i<length; i++) {
		tid = control->priv->array[i];
		egg_debug ("%s", tid);
	}
	return TRUE;
}

/**
 * pk_control_transaction_list_refresh:
 *
 * Not normally required, but force a refresh
 **/
static gboolean
pk_control_transaction_list_refresh (PkControl *control, GError **error)
{
	gboolean ret;
	GError *error_local = NULL;

	g_return_val_if_fail (PK_IS_CONTROL (control), FALSE);
	g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

	/* clear old data */
	if (control->priv->array != NULL) {
		g_strfreev (control->priv->array);
		control->priv->array = NULL;
	}
	ret = dbus_g_proxy_call (control->priv->proxy, "GetTransactionList", &error_local,
				 G_TYPE_INVALID,
				 G_TYPE_STRV, &control->priv->array,
				 G_TYPE_INVALID);
	if (!ret) {
		control->priv->array = NULL;
		egg_warning ("GetTransactionList failed :%s", error_local->message);
		pk_control_error_set (error, PK_CONTROL_ERROR_FAILED, error_local->message);
		g_error_free (error_local);
	}

	return ret;
}

/**
 * pk_control_transaction_list_refresh_idle_cb:
 * @control: This class instance
 **/
static gboolean
pk_control_transaction_list_refresh_idle_cb (PkControl *control)
{
	gboolean ret;
	GError *error = NULL;

	/* refresh the internal lists */
	ret = pk_control_transaction_list_refresh (control, &error);
	if (!ret) {
		egg_warning ("failed to get list: %s", error->message);
		g_error_free (error);
	}
	control->priv->idle_id = 0;
	return FALSE;
}

/**
 * pk_control_transaction_list_get:
 **/
const gchar **
pk_control_transaction_list_get (PkControl *control)
{
	gboolean ret;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_CONTROL (control), NULL);

	/* we might not have refreshed this ever */
	if (control->priv->array == NULL) {
		ret = pk_control_transaction_list_refresh (control, &error);
		if (!ret) {
			egg_warning ("failed to get list: %s", error->message);
			g_error_free (error);
		}

		/* no need to do this twice at startup */
		if (control->priv->idle_id != 0) {
			g_source_remove (control->priv->idle_id);
			control->priv->idle_id = 0;
		}
	}

	return (const gchar **) control->priv->array;
}

/**
 * pk_control_transaction_list_changed_cb:
 */
static void
pk_control_transaction_list_changed_cb (DBusGProxy *proxy, gchar **array, PkControl *control)
{
	g_return_if_fail (PK_IS_CONTROL (control));

	/* clear old data */
	if (control->priv->array != NULL)
		g_strfreev (control->priv->array);
	control->priv->array = g_strdupv (array);
	egg_debug ("emit transaction-list-changed");
	g_signal_emit (control, signals [PK_CONTROL_LIST_CHANGED], 0);
}

/**
 * pk_control_connection_changed_cb:
 **/
static void
pk_control_connection_changed_cb (PkConnection *pconnection, gboolean connected, PkControl *control)
{
	/* force a refresh so we have valid data*/
	if (connected && control->priv->idle_id == 0)
		control->priv->idle_id = g_idle_add ((GSourceFunc) pk_control_transaction_list_refresh_idle_cb, control);
}

/**
 * pk_control_restart_schedule_cb:
 */
static void
pk_control_restart_schedule_cb (DBusGProxy *proxy, PkControl *control)
{
	g_return_if_fail (PK_IS_CONTROL (control));

	egg_debug ("emitting restart-schedule");
	g_signal_emit (control, signals [PK_CONTROL_RESTART_SCHEDULE], 0);

}

/**
 * pk_control_updates_changed_cb:
 */
static void
pk_control_updates_changed_cb (DBusGProxy *proxy, PkControl *control)
{
	g_return_if_fail (PK_IS_CONTROL (control));

	egg_debug ("emitting updates-changed");
	g_signal_emit (control, signals [PK_CONTROL_UPDATES_CHANGED], 0);

}

/**
 * pk_control_repo_list_changed_cb:
 */
static void
pk_control_repo_list_changed_cb (DBusGProxy *proxy, PkControl *control)
{
	g_return_if_fail (PK_IS_CONTROL (control));

	egg_debug ("emitting repo-list-changed");
	g_signal_emit (control, signals [PK_CONTROL_REPO_LIST_CHANGED], 0);
}

/**
 * pk_control_network_state_changed_cb:
 */
static void
pk_control_network_state_changed_cb (DBusGProxy *proxy, const gchar *network_text, PkControl *control)
{
	PkNetworkEnum network;
	g_return_if_fail (PK_IS_CONTROL (control));

	network = pk_network_enum_from_text (network_text);
	egg_debug ("emitting network-state-changed: %s", network_text);
	g_signal_emit (control, signals [PK_CONTROL_NETWORK_STATE_CHANGED], 0, network);
}

/**
 * pk_control_locked_cb:
 */
static void
pk_control_locked_cb (DBusGProxy *proxy, gboolean is_locked, PkControl *control)
{
	egg_debug ("emit locked %i", is_locked);
	g_signal_emit (control , signals [PK_CONTROL_LOCKED], 0, is_locked);
}

/**
 * pk_control_get_property:
 **/
static void
pk_control_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	PkControl *control = PK_CONTROL (object);
	switch (prop_id) {
	case PROP_VERSION_MAJOR:
		g_value_set_uint (value, control->priv->version_major);
		break;
	case PROP_VERSION_MINOR:
		g_value_set_uint (value, control->priv->version_minor);
		break;
	case PROP_VERSION_MICRO:
		g_value_set_uint (value, control->priv->version_micro);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_control_set_property:
 **/
static void
pk_control_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * pk_control_class_init:
 * @klass: The PkControlClass
 **/
static void
pk_control_class_init (PkControlClass *klass)
{
	GParamSpec *pspec;
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->get_property = pk_control_get_property;
	object_class->set_property = pk_control_set_property;
	object_class->finalize = pk_control_finalize;

	/**
	 * PkControl:version-major:
	 */
	pspec = g_param_spec_uint ("version-major", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_VERSION_MAJOR, pspec);

	/**
	 * PkControl:version-minor:
	 */
	pspec = g_param_spec_uint ("version-minor", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_VERSION_MINOR, pspec);

	/**
	 * PkControl:version-micro:
	 */
	pspec = g_param_spec_uint ("version-micro", NULL, NULL,
				   0, G_MAXUINT, 0,
				   G_PARAM_READWRITE);
	g_object_class_install_property (object_class, PROP_VERSION_MICRO, pspec);

	/**
	 * PkControl::updates-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::updates-changed signal is emitted when the update list may have
	 * changed and the control program may have to update some UI.
	 **/
	signals [PK_CONTROL_UPDATES_CHANGED] =
		g_signal_new ("updates-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, updates_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::repo-list-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::repo-list-changed signal is emitted when the repo list may have
	 * changed and the control program may have to update some UI.
	 **/
	signals [PK_CONTROL_REPO_LIST_CHANGED] =
		g_signal_new ("repo-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, repo_list_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::network-state-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::network-state-changed signal is emitted when the network has changed speed or
	 * connections state.
	 **/
	signals [PK_CONTROL_NETWORK_STATE_CHANGED] =
		g_signal_new ("network-state-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, network_state_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT,
			      G_TYPE_NONE, 1, G_TYPE_UINT);
	/**
	 * PkControl::restart-schedule:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::restart_schedule signal is emitted when the packagekitd service
	 * has been restarted because it has been upgraded.
	 * Client programs should reload themselves when it is convenient to
	 * do so, as old client tools may not be compatable with the new daemon.
	 **/
	signals [PK_CONTROL_RESTART_SCHEDULE] =
		g_signal_new ("restart-schedule",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, restart_schedule),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::transaction-list-changed:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::transaction-list-changed signal is emitted when the list
	 * of transactions handled by the daemon is changed.
	 **/
	signals [PK_CONTROL_LIST_CHANGED] =
		g_signal_new ("transaction-list-changed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, transaction_list_changed),
			      NULL, NULL, g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	/**
	 * PkControl::locked:
	 * @control: the #PkControl instance that emitted the signal
	 *
	 * The ::locked signal is emitted when the backend instance has been
	 * locked by PackageKit.
	 * This may mean that other native package tools will not work.
	 **/
	signals [PK_CONTROL_LOCKED] =
		g_signal_new ("locked",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkControlClass, locked),
			      NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

	g_type_class_add_private (klass, sizeof (PkControlPrivate));
}

/**
 * pk_control_get_properties:
 **/
static GHashTable *
pk_control_get_properties (PkControl *control)
{
	gboolean ret;
	GError *error = NULL;
	GHashTable *hash_table = NULL;
	DBusGProxy *proxy;

	/* connect to the correct path for properties */
	proxy = dbus_g_proxy_new_for_name (control->priv->connection,
					   "org.freedesktop.PackageKit",
					   "/org/freedesktop/PackageKit",
					   "org.freedesktop.DBus.Properties");
	if (proxy == NULL) {
		egg_warning ("Couldn't connect to proxy");
		goto out;
	}

	/* get all properties */
	ret = dbus_g_proxy_call (proxy, "GetAll", &error,
				 G_TYPE_STRING, "org.freedesktop.PackageKit",
				 G_TYPE_INVALID,
				 dbus_g_type_get_map ("GHashTable", G_TYPE_STRING, G_TYPE_VALUE),
				 &hash_table,
				 G_TYPE_INVALID);
	if (!ret) {
		egg_warning ("Couldn't call GetAll() to get properties for %s", error->message);
		g_error_free (error);
		goto out;
	}
out:
	if (proxy != NULL)
		g_object_unref (proxy);
	return hash_table;
}

/**
 * pk_control_collect_props_cb:
 **/
static void
pk_control_collect_props_cb (const char *key, const GValue *value, PkControl *control)
{
	if (g_strcmp0 (key, "version-major") == 0 || g_strcmp0 (key, "VersionMajor") == 0)
		control->priv->version_major = g_value_get_uint (value);
	else if (g_strcmp0 (key, "version-minor") == 0 || g_strcmp0 (key, "VersionMinor") == 0)
		control->priv->version_minor = g_value_get_uint (value);
	else if (g_strcmp0 (key, "version-micro") == 0 || g_strcmp0 (key, "VersionMicro") == 0)
		control->priv->version_micro = g_value_get_uint (value);
	else {
		egg_warning ("unhandled property '%s'", key);
	}
}

/**
 * pk_control_init:
 * @control: This class instance
 **/
static void
pk_control_init (PkControl *control)
{
	GError *error = NULL;
	GHashTable *hash;

	control->priv = PK_CONTROL_GET_PRIVATE (control);
	/* check dbus connections, exit if not valid */
	control->priv->connection = dbus_g_bus_get (DBUS_BUS_SYSTEM, &error);
	if (error != NULL) {
		egg_warning ("%s", error->message);
		g_error_free (error);
		g_error ("This program cannot start until you start the dbus system service.");
	}

	/* we maintain a local copy */
	control->priv->array = NULL;
	control->priv->idle_id = 0;
	control->priv->version_major = 0;
	control->priv->version_minor = 0;
	control->priv->version_micro = 0;

	/* watch for PackageKit on the bus, and try to connect up at start */
	control->priv->pconnection = pk_connection_new ();
	g_signal_connect (control->priv->pconnection, "connection-changed",
			  G_CALLBACK (pk_control_connection_changed_cb), control);

	/* get a connection to the engine object */
	control->priv->proxy = dbus_g_proxy_new_for_name (control->priv->connection,
							 PK_DBUS_SERVICE, PK_DBUS_PATH, PK_DBUS_INTERFACE);
	if (control->priv->proxy == NULL)
		egg_error ("Cannot connect to PackageKit.");

	/* don't timeout, as dbus-glib sets the timeout ~25 seconds */
	dbus_g_proxy_set_default_timeout (control->priv->proxy, INT_MAX);

	dbus_g_proxy_add_signal (control->priv->proxy, "TransactionListChanged",
				 G_TYPE_STRV, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "TransactionListChanged",
				     G_CALLBACK(pk_control_transaction_list_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "UpdatesChanged", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "UpdatesChanged",
				     G_CALLBACK (pk_control_updates_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "RepoListChanged", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "RepoListChanged",
				     G_CALLBACK (pk_control_repo_list_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "NetworkStateChanged",
				 G_TYPE_STRING, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "NetworkStateChanged",
				     G_CALLBACK (pk_control_network_state_changed_cb), control, NULL);

	dbus_g_proxy_add_signal (control->priv->proxy, "RestartSchedule", G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "RestartSchedule",
				     G_CALLBACK (pk_control_restart_schedule_cb), control, NULL);

	dbus_g_object_register_marshaller (g_cclosure_marshal_VOID__BOOLEAN,
					   G_TYPE_NONE, G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_add_signal (control->priv->proxy, "Locked", G_TYPE_BOOLEAN, G_TYPE_INVALID);
	dbus_g_proxy_connect_signal (control->priv->proxy, "Locked",
				     G_CALLBACK (pk_control_locked_cb), control, NULL);

	/* get properties if they exist */
	hash = pk_control_get_properties (control);
	if (hash != NULL) {
		g_hash_table_foreach (hash, (GHFunc) pk_control_collect_props_cb, control);
		g_hash_table_unref (hash);
	}

	/* idle add a refresh so we have valid data */
	control->priv->idle_id = g_idle_add ((GSourceFunc) pk_control_transaction_list_refresh_idle_cb, control);
}

/**
 * pk_control_finalize:
 * @object: The object to finalize
 **/
static void
pk_control_finalize (GObject *object)
{
	PkControl *control;
	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_CONTROL (object));
	control = PK_CONTROL (object);

	g_return_if_fail (control->priv != NULL);

	/* disconnect signal handlers */
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "Locked",
				        G_CALLBACK (pk_control_locked_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "TransactionListChanged",
				        G_CALLBACK (pk_control_transaction_list_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "UpdatesChanged",
				        G_CALLBACK (pk_control_updates_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "RepoListChanged",
				        G_CALLBACK (pk_control_repo_list_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "NetworkStateChanged",
				        G_CALLBACK (pk_control_network_state_changed_cb), control);
	dbus_g_proxy_disconnect_signal (control->priv->proxy, "RestartSchedule",
				        G_CALLBACK (pk_control_restart_schedule_cb), control);

	if (control->priv->idle_id != 0)
		g_source_remove (control->priv->idle_id);
	g_object_unref (G_OBJECT (control->priv->proxy));
	g_object_unref (G_OBJECT (control->priv->pconnection));
	g_strfreev (control->priv->array);

	G_OBJECT_CLASS (pk_control_parent_class)->finalize (object);
}

/**
 * pk_control_new:
 *
 * Return value: a new PkControl object.
 **/
PkControl *
pk_control_new (void)
{
	if (pk_control_object != NULL) {
		g_object_ref (pk_control_object);
	} else {
		pk_control_object = g_object_new (PK_TYPE_CONTROL, NULL);
		g_object_add_weak_pointer (pk_control_object, &pk_control_object);
	}
	return PK_CONTROL (pk_control_object);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_control_test (EggTest *test)
{
	gboolean ret;
	PkControl *control;
	PkConnection *connection;
	guint version;
	PkAuthorizeEnum authorize;

	if (!egg_test_start (test, "PkControl"))
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
	egg_test_title (test, "get control");
	control = pk_control_new ();
	egg_test_assert (test, control != NULL);

	/************************************************************/
	egg_test_title (test, "version major");
	g_object_get (control, "version-major", &version, NULL);
	egg_test_assert (test, (version == PK_MAJOR_VERSION));

	/************************************************************/
	egg_test_title (test, "version minor");
	g_object_get (control, "version-minor", &version, NULL);
	egg_test_assert (test, (version == PK_MINOR_VERSION));

	/************************************************************/
	egg_test_title (test, "version micro");
	g_object_get (control, "version-micro", &version, NULL);
	egg_test_assert (test, (version == PK_MICRO_VERSION));

	/************************************************************/
	egg_test_title (test, "can authorize invalid prefix");
	authorize = pk_control_can_authorize (control, "org.freedesktop.devicekit.power.system-update", NULL);
	egg_test_assert (test, (authorize == PK_AUTHORIZE_ENUM_UNKNOWN));

	/************************************************************/
	egg_test_title (test, "can authorize unknown method");
	authorize = pk_control_can_authorize (control, "org.freedesktop.packagekit.system-x-update", NULL);
	egg_test_assert (test, (authorize == PK_AUTHORIZE_ENUM_UNKNOWN));

	/************************************************************/
	egg_test_title (test, "can authorize yes method");
	authorize = pk_control_can_authorize (control, "org.freedesktop.packagekit.system-sources-refresh", NULL);
	egg_test_assert (test, (authorize == PK_AUTHORIZE_ENUM_YES));

	/************************************************************/
	egg_test_title (test, "can authorize interactive method");
	authorize = pk_control_can_authorize (control, "org.freedesktop.packagekit.system-rollback", NULL);
	egg_test_assert (test, (authorize == PK_AUTHORIZE_ENUM_INTERACTIVE));

	g_object_unref (control);
out:
	egg_test_end (test);
}
#endif

