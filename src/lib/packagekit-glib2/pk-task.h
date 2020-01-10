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

#ifndef __PK_TASK_H
#define __PK_TASK_H

#include <glib-object.h>
#include <gio/gio.h>

#include <packagekit-glib2/pk-progress.h>
#include <packagekit-glib2/pk-results.h>
#include <packagekit-glib2/pk-client.h>

G_BEGIN_DECLS

#define PK_TYPE_TASK		(pk_task_get_type ())
#define PK_TASK(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TASK, PkTask))
#define PK_TASK_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TASK, PkTaskClass))
#define PK_IS_TASK(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TASK))
#define PK_IS_TASK_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TASK))
#define PK_TASK_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TASK, PkTaskClass))
#define PK_TASK_ERROR		(pk_task_error_quark ())
#define PK_TASK_TYPE_ERROR	(pk_task_error_get_type ())

typedef struct _PkTaskPrivate	PkTaskPrivate;
typedef struct _PkTask		PkTask;
typedef struct _PkTaskClass	PkTaskClass;

struct _PkTask
{
	 PkClient		 parent;
	 PkTaskPrivate		*priv;
};

struct _PkTaskClass
{
	PkClientClass		parent_class;
	void	 (*untrusted_question)			(PkTask			*task,
							 guint			 request,
							 PkResults		*results);
	void	 (*key_question)			(PkTask			*task,
							 guint			 request,
							 PkResults		*results);
	void	 (*eula_question)			(PkTask			*task,
							 guint			 request,
							 PkResults		*results);
	void	 (*media_change_question)		(PkTask			*task,
							 guint			 request,
							 PkResults		*results);
	void	 (*simulate_question)			(PkTask			*task,
							 guint			 request,
							 PkResults		*results);
	/* padding for future expansion */
	void (*_pk_reserved1)	(void);
	void (*_pk_reserved2)	(void);
	void (*_pk_reserved3)	(void);
	void (*_pk_reserved4)	(void);
	void (*_pk_reserved5)	(void);
};

GQuark		 pk_task_error_quark			(void);
GType		 pk_task_get_type			(void);
PkTask		*pk_task_new				(void);
void		 pk_task_test				(gpointer		 user_data);

PkResults	*pk_task_generic_finish			(PkTask			*task,
							 GAsyncResult		*res,
							 GError			**error);

void		 pk_task_install_packages_async		(PkTask			*task,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback,
							 gpointer		 user_data);
void		 pk_task_update_packages_async		(PkTask			*task,
							 gchar			**package_ids,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_remove_packages_async		(PkTask			*task,
							 gchar			**package_ids,
							 gboolean		 allow_deps,
							 gboolean		 autoremove,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_install_files_async		(PkTask			*task,
							 gchar			**files,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);
void		 pk_task_update_system_async		(PkTask			*task,
							 GCancellable		*cancellable,
							 PkProgressCallback	 progress_callback,
							 gpointer		 progress_user_data,
							 GAsyncReadyCallback	 callback_ready,
							 gpointer		 user_data);

gboolean	 pk_task_user_accepted			(PkTask			*task,
							 guint			 request);
gboolean	 pk_task_user_declined			(PkTask			*task,
							 guint			 request);

G_END_DECLS

#endif /* __PK_TASK_H */

