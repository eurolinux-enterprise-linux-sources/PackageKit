/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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

#ifndef __PK_TASK_LIST_H
#define __PK_TASK_LIST_H

#include <glib-object.h>
#include <packagekit-glib/pk-client.h>
#include <packagekit-glib/pk-enum.h>

G_BEGIN_DECLS

#define PK_TYPE_TASK_LIST		(pk_task_list_get_type ())
#define PK_TASK_LIST(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_TASK_LIST, PkTaskList))
#define PK_TASK_LIST_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_TASK_LIST, PkTaskListClass))
#define PK_IS_TASK_LIST(o)	 	(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_TASK_LIST))
#define PK_IS_TASK_LIST_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_TASK_LIST))
#define PK_TASK_LIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_TASK_LIST, PkTaskListClass))

typedef struct _PkTaskListPrivate	PkTaskListPrivate;
typedef struct _PkTaskList		PkTaskList;
typedef struct _PkTaskListClass		PkTaskListClass;

typedef struct
{
	gchar			*tid;
	PkStatusEnum		 status;
	PkRoleEnum		 role;
	gchar			*text;
	PkClient		*monitor;
	gboolean		 valid;
} PkTaskListItem;

struct _PkTaskList
{
	GObject			 parent;
	PkTaskListPrivate	*priv;
};

struct _PkTaskListClass
{
	GObjectClass	parent_class;
	void		(* changed)			(PkTaskList	*tlist);
	void		(* status_changed)		(PkTaskList	*tlist);
	void		(* message)			(PkTaskList	*tlist,
							 PkClient	*client,
							 PkMessageEnum	 message,
							 const gchar	*details);
	void		(* finished)			(PkTaskList	*tlist,
							 PkClient	*client,
							 PkExitEnum	 exit,
							 guint		 runtime);
	void		(* error_code)			(PkTaskList	*tlist,
							 PkClient	*client,
							 PkErrorCodeEnum code,
							 const gchar	*details);
};

GType		 pk_task_list_get_type			(void);
PkTaskList	*pk_task_list_new			(void);

gboolean	 pk_task_list_refresh			(PkTaskList	*tlist);
gboolean	 pk_task_list_print			(PkTaskList	*tlist);
gboolean	 pk_task_list_free			(PkTaskList	*tlist);
gboolean	 pk_task_list_contains_role		(PkTaskList	*tlist,
							 PkRoleEnum	 role);
guint		 pk_task_list_get_size			(PkTaskList	*tlist);
PkTaskListItem	*pk_task_list_get_item			(PkTaskList	*tlist,
							 guint		 item);

G_END_DECLS

#endif /* __PK_TASK_LIST_H */

