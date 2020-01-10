/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_CLIENT_H
#define __PK_CLIENT_H

#include <glib-object.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-progress.h>
#include <packagekit-glib2/pk-bitfield.h>

G_BEGIN_DECLS

#define PK_TYPE_CLIENT		(pk_client_get_type ())
#define PK_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CLIENT, PkClient))
#define PK_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CLIENT, PkClientClass))
#define PK_IS_CLIENT(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CLIENT))
#define PK_IS_CLIENT_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CLIENT))
#define PK_CLIENT_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CLIENT, PkClientClass))
#define PK_CLIENT_ERROR		(pk_client_error_quark ())
#define PK_CLIENT_TYPE_ERROR	(pk_client_error_get_type ())

/**
 * PkClientError:
 * @PK_CLIENT_ERROR_FAILED: the transaction failed for an unknown reason
 * @PK_CLIENT_ERROR_NO_TID: the transaction id was not pre-allocated (internal error)
 * @PK_CLIENT_ERROR_ALREADY_TID: the transaction id has already been used (internal error)
 * @PK_CLIENT_ERROR_ROLE_UNKNOWN: the role was not set (internal error)
 * @PK_CLIENT_ERROR_INVALID_INPUT: the package_id is invalid
 * @PK_CLIENT_ERROR_INVALID_FILE: the file is invalid
 * @PK_CLIENT_ERROR_NOT_SUPPORTED: the action is not supported
 *
 * Errors that can be thrown
 */
typedef enum
{
	PK_CLIENT_ERROR_FAILED,
	PK_CLIENT_ERROR_FAILED_AUTH,
	PK_CLIENT_ERROR_NO_TID,
	PK_CLIENT_ERROR_ALREADY_TID,
	PK_CLIENT_ERROR_ROLE_UNKNOWN,
	PK_CLIENT_ERROR_CANNOT_START_DAEMON,
	PK_CLIENT_ERROR_INVALID_INPUT,
	PK_CLIENT_ERROR_INVALID_FILE,
	PK_CLIENT_ERROR_NOT_SUPPORTED
} PkClientError;

typedef struct _PkClientPrivate	PkClientPrivate;
typedef struct _PkClient		PkClient;
typedef struct _PkClientClass		PkClientClass;

struct _PkClient
{
	 GObject		 parent;
	 PkClientPrivate	*priv;
};

struct _PkClientClass
{
	GObjectClass	parent_class;

	/* signals */
	void		(* changed)			(PkClient	*client);
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GQuark		 pk_client_error_quark			(void);
GType		 pk_client_get_type		  	(void);
PkClient	*pk_client_new				(void);
void		 pk_client_test				(gpointer	 user_data);

/* get transaction results */
PkResults	*pk_client_generic_finish		(PkClient		*client,
							 GAsyncResult		*res,
							 GError			**error);

void		 pk_client_resolve_async		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**packages,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_search_names_async		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_search_details_async		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_search_groups_async		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_search_files_async		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_details_async		(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_update_detail_async	(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_download_packages_async	(PkClient		*client,
							 gchar			**package_ids,
							 const gchar		*directory,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_updates_async		(PkClient		*client,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_old_transactions_async	(PkClient		*client,
							 guint			 number,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_update_system_async		(PkClient		*client,
							 gboolean		 only_trusted,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_depends_async		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**package_ids,
							 gboolean		 recursive,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_packages_async		(PkClient		*client,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_requires_async		(PkClient		*client,
							 PkBitfield		 filters,
							 gchar			**package_ids,
							 gboolean		 recursive,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_what_provides_async		(PkClient		*client,
							 PkBitfield		 filters,
							 PkProvidesEnum		 provides,
							 gchar			**values,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_distro_upgrades_async	(PkClient		*client,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_files_async		(PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_categories_async		(PkClient		*client,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_remove_packages_async	(PkClient		*client,
							 gchar			**package_ids,
							 gboolean		 allow_deps,
							 gboolean		 autoremove,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_refresh_cache_async		(PkClient		*client,
							 gboolean		 force,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_install_packages_async	(PkClient		*client,
							 gboolean		 only_trusted,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_install_signature_async	(PkClient		*client,
							 PkSigTypeEnum		 type,
							 const gchar		*key_id,
							 const gchar		*package_id,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_update_packages_async	(PkClient		*client,
							 gboolean		 only_trusted,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_install_files_async		(PkClient		*client,
							 gboolean		 only_trusted,
							 gchar			**files,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_accept_eula_async		(PkClient		*client,
							 const gchar		*eula_id,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_rollback_async		(PkClient		*client,
							 const gchar		*transaction_id,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_get_repo_list_async		(PkClient		*client,
							 PkBitfield		 filters,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_repo_enable_async		(PkClient		*client,
							 const gchar		*repo_id,
							 gboolean		 enabled,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_repo_set_data_async		(PkClient		*client,
							 const gchar		*repo_id,
							 const gchar		*parameter,
							 const gchar		*value,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_simulate_install_files_async	(PkClient		*client,
							 gchar			**files,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_simulate_install_packages_async (PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_simulate_remove_packages_async (PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_simulate_update_packages_async (PkClient		*client,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

void		 pk_client_adopt_async 			(PkClient		*client,
							 const gchar		*transaction_id,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

PkProgress	*pk_client_get_progress_finish		(PkClient		*client,
							 GAsyncResult		*res,
							 GError			**error);

void		 pk_client_get_progress_async 		(PkClient		*client,
							 const gchar		*transaction_id,
							 GCancellable		*cancellable,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

G_END_DECLS

#endif /* __PK_CLIENT_H */

