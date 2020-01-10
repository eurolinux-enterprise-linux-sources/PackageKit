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

#ifndef __PK_PACKAGE_OBJ_H
#define __PK_PACKAGE_OBJ_H

#include <glib-object.h>
#include <packagekit-glib/pk-enum.h>
#include <packagekit-glib/pk-package-id.h>

/**
 * PkPackageObj:
 *
 * A cached store for the complete Package object
 */
typedef struct {
	PkInfoEnum		 info;
	PkPackageId		*id;
	gchar			*summary;
} PkPackageObj;

PkPackageObj	*pk_package_obj_new			(PkInfoEnum		 info,
							 const PkPackageId	*id,
							 const gchar		*summary);
gboolean	 pk_package_obj_free			(PkPackageObj		*obj);
PkPackageObj	*pk_package_obj_copy			(const PkPackageObj	*obj);
gboolean	 pk_package_obj_equal			(const PkPackageObj	*obj1,
							 const PkPackageObj	*obj2);
gboolean	 pk_package_obj_equal_fuzzy_arch	(const PkPackageObj	*obj1,
							 const PkPackageObj	*obj2);
gchar		*pk_package_obj_to_string		(const PkPackageObj	*obj);
PkPackageObj	*pk_package_obj_from_string		(const gchar		*text);
PkInfoEnum	 pk_package_obj_get_info		(const PkPackageObj	*obj);
const PkPackageId	*pk_package_obj_get_id		(const PkPackageObj	*obj);
const gchar		*pk_package_obj_get_summary	(const PkPackageObj	*obj);

#endif /* __PK_PACKAGE_OBJ_H */

