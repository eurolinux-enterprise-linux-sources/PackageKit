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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

/**
 * SECTION:pk-control
 * @short_description: An abstract control access GObject
 */

#ifndef __PK_CONTROL_H
#define __PK_CONTROL_H

#include <glib-object.h>
#include <packagekit-glib/pk-enum.h>
#include <packagekit-glib/pk-bitfield.h>

G_BEGIN_DECLS

#define PK_TYPE_CONTROL		(pk_control_get_type ())
#define PK_CONTROL(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_CONTROL, PkControl))
#define PK_CONTROL_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_CONTROL, PkControlClass))
#define PK_IS_CONTROL(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_CONTROL))
#define PK_IS_CONTROL_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_CONTROL))
#define PK_CONTROL_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_CONTROL, PkControlClass))
#define PK_CONTROL_ERROR	(pk_control_error_quark ())
#define PK_CONTROL_TYPE_ERROR	(pk_control_error_get_type ())

typedef struct _PkControlPrivate	PkControlPrivate;
typedef struct _PkControl		PkControl;
typedef struct _PkControlClass		PkControlClass;

/**
 * PkControlError:
 * @PK_CONTROL_ERROR_FAILED: the transaction failed for an unknown reason
 *
 * Errors that can be thrown
 */
typedef enum
{
	PK_CONTROL_ERROR_FAILED,
	PK_CONTROL_ERROR_CANNOT_START_DAEMON
} PkControlError;

struct _PkControl
{
	 GObject		 parent;
	 PkControlPrivate	*priv;
};

struct _PkControlClass
{
	GObjectClass	parent_class;
	/* signals */
	void		(* transaction_list_changed)	(PkControl	*control);
	void		(* updates_changed)		(PkControl	*control);
	void		(* repo_list_changed)		(PkControl	*control);
	void		(* network_state_changed)	(PkControl	*control);
	void		(* restart_schedule)		(PkControl	*control);
	void		(* locked)			(PkControl	*control,
							 gboolean	 is_locked);
	/* padding for future expansion */
	void (*_pk_reserved1) (void);
	void (*_pk_reserved2) (void);
	void (*_pk_reserved3) (void);
	void (*_pk_reserved4) (void);
	void (*_pk_reserved5) (void);
};

GQuark		 pk_control_error_quark			(void);
GType		 pk_control_get_type		  	(void);
PkControl	*pk_control_new				(void);

gboolean	 pk_control_allocate_transaction_id	(PkControl	*control,
							 gchar		**tid,
							 GError		**error);
gboolean	 pk_control_set_proxy			(PkControl	*control,
							 const gchar	*proxy_http,
							 const gchar	*proxy_ftp,
							 GError		**error);
PkBitfield	 pk_control_get_actions			(PkControl	*control,
							 GError		**error);
PkBitfield	 pk_control_get_filters			(PkControl	*control,
							 GError		**error);
PkBitfield	 pk_control_get_groups			(PkControl	*control,
							 GError		**error);
gchar		**pk_control_get_mime_types		(PkControl	*control,
							 GError		**error);
gchar		*pk_control_get_daemon_state		(PkControl	*control,
							 GError		**error);
PkNetworkEnum	 pk_control_get_network_state		(PkControl	*control,
							 GError		**error);
gboolean	 pk_control_get_backend_detail		(PkControl	*control,
							 gchar		**name,
							 gchar		**author,
							 GError		**error);
gboolean	 pk_control_get_time_since_action	(PkControl	*control,
							 PkRoleEnum	 role,
							 guint		*seconds,
							 GError		**error);
gboolean	 pk_control_transaction_list_print	(PkControl	*control);
const gchar	**pk_control_transaction_list_get	(PkControl	*control);
PkAuthorizeEnum	 pk_control_can_authorize		(PkControl	*control,
							 const gchar	*action_id,
							 GError		**error);

G_END_DECLS

#endif /* __PK_CONTROL_H */

