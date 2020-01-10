/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007-2009 Richard Hughes <richard@hughsie.com>
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
 * SECTION:pk-task-list
 * @short_description: A nice way to keep a list of the jobs being processed
 *
 * These provide a good way to keep a list of the jobs being processed so we
 * can see what type of jobs and thier status easily.
 */

#include "config.h"

#include <glib-object.h>
#include <gio/gio.h>

#include "egg-debug.h"

#include <packagekit-glib2/pk-transaction-list.h>
#include <packagekit-glib2/pk-control.h>
#include <packagekit-glib2/pk-common.h>

static void     pk_transaction_list_finalize		(GObject         *object);

#define PK_TRANSACTION_LIST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), PK_TYPE_TRANSACTION_LIST, PkTransactionListPrivate))

/**
 * PkTransactionListPrivate:
 *
 * Private #PkTransactionList data
 **/
struct _PkTransactionListPrivate
{
	GPtrArray		*transaction_ids;
	PkControl		*control;
	GCancellable		*cancellable;
};

typedef enum {
	SIGNAL_ADDED,
	SIGNAL_REMOVED,
	SIGNAL_LAST
} PkSignals;

static guint signals [SIGNAL_LAST] = { 0 };

G_DEFINE_TYPE (PkTransactionList, pk_transaction_list, G_TYPE_OBJECT)

/**
 * pk_transaction_list_process_transaction_list:
 **/
static void
pk_transaction_list_process_transaction_list (PkTransactionList *tlist, gchar **transaction_ids)
{
	guint i, j;
	gboolean ret;
	const gchar *tid;
	gchar *tid_tmp;
	GPtrArray *array = tlist->priv->transaction_ids;

	/* debug */
	for (i=0; i<array->len; i++) {
		tid = g_ptr_array_index (array, i);
		egg_debug ("last:\t%s", tid);
	}
	for (i=0; transaction_ids[i] != NULL; i++)
		egg_debug ("current:\t%s", transaction_ids[i]);

	/* remove old entries */
	for (i=0; i<array->len; i++) {
		tid = g_ptr_array_index (array, i);

		/* is in new list */
		ret = FALSE;
		for (j=0; transaction_ids[j] != NULL; j++) {
			ret = (g_strcmp0 (tid, transaction_ids[j]) == 0);
			if (ret)
				break;
		}

		/* no, so remove from array */
		if (!ret) {
			tid_tmp = g_strdup (tid);
			g_ptr_array_remove_index (array, i);
			egg_debug ("emit removed: %s", tid_tmp);
			g_signal_emit (tlist, signals[SIGNAL_REMOVED], 0, tid_tmp);
			g_free (tid_tmp);
		}
	}

	/* add new entries */
	for (i=0; transaction_ids[i] != NULL; i++) {

		/* check to see if tid is in array */
		ret = FALSE;
		for (j=0; j<array->len; j++) {
			tid = g_ptr_array_index (array, j);
			ret = (g_strcmp0 (tid, transaction_ids[i]) == 0);
			if (ret)
				break;
		}

		/* no, so add to array */
		if (!ret) {
			g_ptr_array_add (array, g_strdup (transaction_ids[i]));
			egg_debug ("emit added: %s", transaction_ids[i]);
			g_signal_emit (tlist, signals[SIGNAL_ADDED], 0, transaction_ids[i]);
		}
	}
}

/**
 * pk_transaction_list_get_transaction_list_cb:
 **/
static void
pk_transaction_list_get_transaction_list_cb (PkControl *control, GAsyncResult *res, PkTransactionList *tlist)
{
	GError *error = NULL;
	gchar **transaction_ids = NULL;

	/* get the result */
	transaction_ids = pk_control_get_transaction_list_finish (control, res, &error);
	if (transaction_ids == NULL) {
		egg_warning ("Failed to get transaction list: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* process */
	pk_transaction_list_process_transaction_list (tlist, transaction_ids);
out:
	g_strfreev (transaction_ids);
}

/**
 * pk_transaction_list_get_transaction_list:
 **/
static void
pk_transaction_list_get_transaction_list (PkTransactionList *tlist)
{
	egg_debug ("refreshing task list");
	pk_control_get_transaction_list_async (tlist->priv->control, tlist->priv->cancellable,
					       (GAsyncReadyCallback) pk_transaction_list_get_transaction_list_cb, tlist);
}

/**
 * pk_transaction_list_task_list_changed_cb:
 **/
static void
pk_transaction_list_task_list_changed_cb (PkControl *control, gchar **transaction_ids, PkTransactionList *tlist)
{
	/* process */
	pk_transaction_list_process_transaction_list (tlist, transaction_ids);
}

/**
 * pk_transaction_list_notify_connected_cb:
 **/
static void
pk_transaction_list_notify_connected_cb (PkControl *control, GParamSpec *pspec, PkTransactionList *tlist)
{
	gboolean connected;
	g_object_get (control, "connected", &connected, NULL);
	if (connected)
		pk_transaction_list_get_transaction_list (tlist);
}

/**
 * pk_transaction_list_get_ids:
 * @tlist: a valid #PkTransactionList instance
 *
 * Gets the string lists of transaction IDs recognised as pending, running or finished by the daemon.
 *
 * Return value: the array of strings, free with g_strfreev()
 **/
gchar **
pk_transaction_list_get_ids (PkTransactionList *tlist)
{
	g_return_val_if_fail (PK_IS_TRANSACTION_LIST (tlist), NULL);
	return pk_ptr_array_to_strv (tlist->priv->transaction_ids);
}

/**
 * pk_transaction_list_class_init:
 **/
static void
pk_transaction_list_class_init (PkTransactionListClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = pk_transaction_list_finalize;

	/**
	 * PkTransactionList::added:
	 * @tlist: the #PkTransactionList instance that emitted the signal
	 *
	 * The ::added signal is emitted when a tid has been added to the transaction list
	 **/
	signals [SIGNAL_ADDED] =
		g_signal_new ("added",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTransactionListClass, added),
			      NULL, NULL, g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);

	/**
	 * PkTransactionList::removed:
	 * @tlist: the #PkTransactionList instance that emitted the signal
	 *
	 * The ::removed signal is emitted when a tid has been removed from the transaction list
	 **/
	signals [SIGNAL_REMOVED] =
		g_signal_new ("removed",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (PkTransactionListClass, added),
			      NULL, NULL, g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE, 1, G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (PkTransactionListPrivate));
}

/**
 * pk_transaction_list_init:
 **/
static void
pk_transaction_list_init (PkTransactionList *tlist)
{
	tlist->priv = PK_TRANSACTION_LIST_GET_PRIVATE (tlist);

	/* get the changing job list */
	tlist->priv->cancellable = g_cancellable_new ();
	tlist->priv->control = pk_control_new ();
	g_signal_connect (tlist->priv->control, "transaction-list-changed",
			  G_CALLBACK (pk_transaction_list_task_list_changed_cb), tlist);
	g_signal_connect (tlist->priv->control, "notify::connected",
			  G_CALLBACK (pk_transaction_list_notify_connected_cb), tlist);

	/* we maintain a local copy */
	tlist->priv->transaction_ids = g_ptr_array_new_with_free_func (g_free);

	/* force a refresh so we have valid data*/
	pk_transaction_list_get_transaction_list (tlist);
}

/**
 * pk_transaction_list_finalize:
 **/
static void
pk_transaction_list_finalize (GObject *object)
{
	PkTransactionList *tlist;

	g_return_if_fail (object != NULL);
	g_return_if_fail (PK_IS_TRANSACTION_LIST (object));
	tlist = PK_TRANSACTION_LIST (object);
	g_return_if_fail (tlist->priv != NULL);

	/* cancel if we're in the act */
	g_cancellable_cancel (tlist->priv->cancellable);

	/* unhook all signals */
	g_signal_handlers_disconnect_by_func (tlist->priv->control, G_CALLBACK (pk_transaction_list_task_list_changed_cb), tlist);
	g_signal_handlers_disconnect_by_func (tlist->priv->control, G_CALLBACK (pk_transaction_list_notify_connected_cb), tlist);

	/* remove all watches */
	g_ptr_array_unref (tlist->priv->transaction_ids);
	g_object_unref (tlist->priv->control);
	g_object_unref (tlist->priv->cancellable);

	G_OBJECT_CLASS (pk_transaction_list_parent_class)->finalize (object);
}

/**
 * pk_transaction_list_new:
 **/
PkTransactionList *
pk_transaction_list_new (void)
{
	PkTransactionList *tlist;
	tlist = g_object_new (PK_TYPE_TRANSACTION_LIST, NULL);
	return PK_TRANSACTION_LIST (tlist);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

#include <packagekit-glib2/pk-client.h>
#include <packagekit-glib2/pk-package-ids.h>

guint _added = 0;
guint _removed = 0;
guint _refcount = 0;

static void
pk_transaction_list_test_resolve_cb (GObject *object, GAsyncResult *res, EggTest *test)
{
	PkClient *client = PK_CLIENT (object);
	GError *error = NULL;
	PkResults *results = NULL;
	PkExitEnum exit_enum;

	/* get the results */
	results = pk_client_generic_finish (client, res, &error);
	if (results == NULL) {
		egg_test_failed (test, "failed to resolve: %s", error->message);
		g_error_free (error);
		goto out;
	}

	exit_enum = pk_results_get_exit_code (results);
	if (exit_enum != PK_EXIT_ENUM_SUCCESS)
		egg_test_failed (test, "failed to resolve success: %s", pk_exit_enum_to_text (exit_enum));
out:
	if (results != NULL)
		g_object_unref (results);
	if (--_refcount == 0)
		egg_test_loop_quit (test);
}

static void
pk_transaction_list_test_added_cb (PkTransactionList *tlist, const gchar *tid, EggTest *test)
{
	egg_debug ("added %s", tid);
	_added++;
}

static void
pk_transaction_list_test_removed_cb (PkTransactionList *tlist, const gchar *tid, EggTest *test)
{
	egg_debug ("removed %s", tid);
	_removed++;
}

static gboolean
pk_transaction_list_delay_cb (EggTest *test)
{
	egg_test_loop_quit (test);
	return FALSE;
}

void
pk_transaction_list_test (gpointer user_data)
{
	EggTest *test = (EggTest *) user_data;
	PkTransactionList *tlist;
	PkClient *client;
	gchar **package_ids;

	if (!egg_test_start (test, "PkTransactionList"))
		return;

	/************************************************************/
	egg_test_title (test, "get transaction_list object");
	tlist = pk_transaction_list_new ();
	egg_test_assert (test, tlist != NULL);
	g_signal_connect (tlist, "added",
			  G_CALLBACK (pk_transaction_list_test_added_cb), test);
	g_signal_connect (tlist, "removed",
			  G_CALLBACK (pk_transaction_list_test_removed_cb), test);

	/************************************************************/
	egg_test_title (test, "get client");
	client = pk_client_new ();
	egg_test_assert (test, client != NULL);

	/************************************************************/
	egg_test_title (test, "resolve package");
	package_ids = pk_package_ids_from_text ("glib2;2.14.0;i386;fedora&powertop");
	_refcount = 2;
	pk_client_resolve_async (client, pk_bitfield_value (PK_FILTER_ENUM_INSTALLED), package_ids, NULL, NULL, NULL,
				 (GAsyncReadyCallback) pk_transaction_list_test_resolve_cb, test);
	pk_client_resolve_async (client, pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED), package_ids, NULL, NULL, NULL,
				 (GAsyncReadyCallback) pk_transaction_list_test_resolve_cb, test);
	g_strfreev (package_ids);
	egg_test_loop_wait (test, 15000);
	egg_test_success (test, "resolved in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "wait for remove");
	g_timeout_add (100, (GSourceFunc) pk_transaction_list_delay_cb, test);
	egg_test_loop_wait (test, 15000);
	egg_test_success (test, "resolved in %i", egg_test_elapsed (test));

	/************************************************************/
	egg_test_title (test, "correct number of added signals");
	egg_test_assert (test, (_added == 2));

	/************************************************************/
	egg_test_title (test, "correct number of removed signals");
	egg_test_assert (test, (_removed == 2));

	g_object_unref (tlist);
	g_object_unref (client);
	egg_test_end (test);
}
#endif

