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

#ifndef __PK_PACKAGE_LIST_H
#define __PK_PACKAGE_LIST_H

#include <glib-object.h>
#include <packagekit-glib/pk-enum.h>
#include <packagekit-glib/pk-package-obj.h>
#include <packagekit-glib/pk-obj-list.h>

G_BEGIN_DECLS

#define PK_TYPE_PACKAGE_LIST		(pk_package_list_get_type ())
#define PK_PACKAGE_LIST(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), PK_TYPE_PACKAGE_LIST, PkPackageList))
#define PK_PACKAGE_LIST_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), PK_TYPE_PACKAGE_LIST, PkPackageListClass))
#define PK_IS_PACKAGE_LIST(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), PK_TYPE_PACKAGE_LIST))
#define PK_IS_PACKAGE_LIST_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), PK_TYPE_PACKAGE_LIST))
#define PK_PACKAGE_LIST_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), PK_TYPE_PACKAGE_LIST, PkPackageListClass))

typedef struct _PkPackageListPrivate	PkPackageListPrivate;
typedef struct _PkPackageList		PkPackageList;
typedef struct _PkPackageListClass	PkPackageListClass;

struct _PkPackageList
{
	 PkObjList		 parent;
	 PkPackageListPrivate	*priv;
};

struct _PkPackageListClass
{
	PkObjListClass		parent_class;
};

GType			 pk_package_list_get_type	(void);
PkPackageList		*pk_package_list_new		(void);
gboolean		 pk_package_list_add		(PkPackageList		*plist,
							 PkInfoEnum		 info,
							 const PkPackageId	*ident,
							 const gchar		*summary);
gboolean		 pk_package_list_contains	(const PkPackageList	*plist,
							 const gchar		*package_id);
gboolean		 pk_package_list_remove		(PkPackageList		*plist,
							 const gchar		*package_id);
gchar			**pk_package_list_to_strv	(const PkPackageList	*plist)
							 G_GNUC_WARN_UNUSED_RESULT;
guint			 pk_package_list_get_size	(const PkPackageList	*plist);
gboolean		 pk_package_list_sort		(PkPackageList		*plist);
gboolean		 pk_package_list_sort_info	(PkPackageList		*plist);
gboolean		 pk_package_list_sort_summary	(PkPackageList		*plist);
const PkPackageObj	*pk_package_list_get_obj	(const PkPackageList	*plist,
							 guint			 item);
gboolean		 pk_package_list_set_fuzzy_arch	(PkPackageList		*plist,
							 gboolean		 fuzzy_arch);

G_END_DECLS

#endif /* __PK_PACKAGE_LIST_H */

