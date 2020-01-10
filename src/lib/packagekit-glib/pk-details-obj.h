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

#if !defined (__PACKAGEKIT_H_INSIDE__) && !defined (PK_COMPILATION)
#error "Only <packagekit.h> can be included directly."
#endif

#ifndef __PK_DETAILS_H
#define __PK_DETAILS_H

#include <glib-object.h>
#include <packagekit-glib/pk-enum.h>
#include <packagekit-glib/pk-package-id.h>

G_BEGIN_DECLS

/**
 * PkDetailsObj:
 *
 * Cached object to represent details about the package.
 **/
typedef struct
{
	PkPackageId			*id;
	gchar				*license;
	PkGroupEnum			 group;
	gchar				*description;
	gchar				*url;
	guint64				 size;
} PkDetailsObj;

PkDetailsObj	*pk_details_obj_new			(void);
PkDetailsObj	*pk_details_obj_copy			(const PkDetailsObj	*obj);
PkDetailsObj	*pk_details_obj_new_from_data		(const PkPackageId	*id,
							 const gchar		*license,
							 PkGroupEnum		 group,
							 const gchar		*description,
							 const gchar		*url,
							 guint64		 size);
gboolean	 pk_details_obj_free			(PkDetailsObj		*obj);

G_END_DECLS

#endif /* __PK_DETAILS_H */
