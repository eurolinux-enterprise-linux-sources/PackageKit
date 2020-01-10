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

#ifndef __PK_PACKAGE_ID_H
#define __PK_PACKAGE_ID_H

#include <glib-object.h>

G_BEGIN_DECLS

/**
 * PkPackageId:
 *
 * Cached object to represent a package ID.
 **/
typedef struct {
	gchar	*name;
	gchar	*version;
	gchar	*arch;
	gchar	*data;
} PkPackageId;

/* objects */
PkPackageId	*pk_package_id_new			(void);
const gchar	*pk_package_id_get_name			(const PkPackageId	*id);
const gchar	*pk_package_id_get_version		(const PkPackageId	*id);
const gchar	*pk_package_id_get_arch			(const PkPackageId	*id);
const gchar	*pk_package_id_get_data			(const PkPackageId	*id);
PkPackageId	*pk_package_id_new_from_string		(const gchar		*package_id)
							 G_GNUC_WARN_UNUSED_RESULT;
PkPackageId	*pk_package_id_new_from_list		(const gchar		*name,
							 const gchar		*version,
							 const gchar		*arch,
							 const gchar		*data)
							 G_GNUC_WARN_UNUSED_RESULT;
PkPackageId	*pk_package_id_copy			(const PkPackageId	*id)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_package_id_equal			(const PkPackageId	*id1,
							 const PkPackageId	*id2);
gboolean	 pk_package_id_equal_fuzzy_arch		(const PkPackageId	*id1,
							 const PkPackageId	*id2);
gchar		*pk_package_id_to_string		(const PkPackageId	*id)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_package_id_free			(PkPackageId		*id);

/* string helpers */
gchar		*pk_package_id_build			(const gchar		*name,
							 const gchar		*version,
							 const gchar		*arch,
							 const gchar		*data)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_package_id_check			(const gchar		*package_id)
							 G_GNUC_WARN_UNUSED_RESULT;
gboolean	 pk_package_id_equal_strings		(const gchar		*pid1,
							 const gchar		*pid2);

G_END_DECLS

#endif /* __PK_PACKAGE_ID_H */

