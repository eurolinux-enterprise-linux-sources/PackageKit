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

/**
 * SECTION:pk-package-ids
 * @short_description: Functionality to modify multiple PackageIDs
 *
 * Composite PackageId's are difficult to read and create.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include <packagekit-glib/pk-common.h>
#include <packagekit-glib/pk-package-id.h>
#include <packagekit-glib/pk-package-ids.h>

#include "egg-debug.h"
#include "egg-string.h"

/**
 * pk_package_ids_from_id:
 * @package_id: A single package_id
 *
 * Form a composite string array of package_id's from
 * a single package_id
 *
 * Return value: the string array, or %NULL if invalid, free with g_strfreev()
 **/
gchar **
pk_package_ids_from_id (const gchar *package_id)
{
	g_return_val_if_fail (package_id != NULL, NULL);
	return g_strsplit (package_id, PK_PACKAGE_IDS_DELIM, 1);
}

/**
 * pk_package_ids_from_text:
 * @package_id: A single package_id
 *
 * Form a composite string array of package_id's from
 * a delimited string
 *
 * Return value: the string array, or %NULL if invalid, free with g_strfreev()
 **/
gchar **
pk_package_ids_from_text (const gchar *package_id)
{
	g_return_val_if_fail (package_id != NULL, NULL);
	return g_strsplit (package_id, PK_PACKAGE_IDS_DELIM, 0);
}

/**
 * pk_package_ids_from_array:
 * @array: the GPtrArray of package_id's
 *
 * Form a composite string array of package_id's.
 * The data in the GPtrArray is copied.
 *
 * Return value: the string array, or %NULL if invalid
 **/
gchar **
pk_package_ids_from_array (GPtrArray *array)
{
	g_return_val_if_fail (array != NULL, NULL);
	return pk_ptr_array_to_strv (array);
}

/**
 * pk_package_ids_from_va_list:
 * @package_id_first: the first package_id
 * @args: any subsequant package_id's
 *
 * Form a composite string array of package_id's.
 *
 * Return value: the string array, or %NULL if invalid
 **/
gchar **
pk_package_ids_from_va_list (const gchar *package_id_first, va_list *args)
{
	g_return_val_if_fail (package_id_first != NULL, NULL);
	return pk_va_list_to_argv (package_id_first, args);
}

/**
 * pk_package_ids_check:
 * @package_ids: a string array of package_id's
 *
 * Check the string array of package_id's for validity
 *
 * Return value: %TRUE if the package_ids are all valid.
 **/
gboolean
pk_package_ids_check (gchar **package_ids)
{
	guint i;
	guint size;
	gboolean ret = FALSE;
	const gchar *package_id;

	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* check all */
	size = g_strv_length (package_ids);
	for (i=0; i<size; i++) {
		package_id = package_ids[i];
		ret = pk_package_id_check (package_id);
		if (!ret)
			goto out;
	}
out:
	return ret;
}

/**
 * pk_package_ids_print:
 * @package_ids: a string array of package_id's
 *
 * Print the string array of package_id's
 *
 * Return value: %TRUE if we printed all the package_id's.
 **/
gboolean
pk_package_ids_print (gchar **package_ids)
{
	guint i;
	guint size;

	g_return_val_if_fail (package_ids != NULL, FALSE);

	/* print all */
	size = g_strv_length (package_ids);
	for (i=0; i<size; i++) {
		egg_debug ("package_id[%i] = %s", i, package_ids[i]);
	}
	return TRUE;
}

/**
 * pk_package_ids_size:
 * @package_ids: a string array of package_id's
 *
 * Gets the size of the array
 *
 * Return value: the size of the array.
 **/
guint
pk_package_ids_size (gchar **package_ids)
{
	g_return_val_if_fail (package_ids != NULL, 0);
	return g_strv_length (package_ids);
}

/**
 * pk_package_ids_to_text:
 * @package_ids: a string array of package_id's
 *
 * Cats the string array of package_id's into one delimited string
 *
 * Return value: a string representation of all the package_id's.
 **/
gchar *
pk_package_ids_to_text (gchar **package_ids)
{
	/* special case as this is allowed */
	if (package_ids == NULL)
		return g_strdup ("(null)");
	return g_strjoinv (PK_PACKAGE_IDS_DELIM, package_ids);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

/**
 * pk_package_ids_test_va_list:
 **/
static gchar **
pk_package_ids_test_va_list (const gchar *package_id_first, ...)
{
	va_list args;
	gchar **package_ids;

	/* process the valist */
	va_start (args, package_id_first);
	package_ids = pk_package_ids_from_va_list (package_id_first, &args);
	va_end (args);

	return package_ids;
}


void
pk_package_ids_test (EggTest *test)
{
	gboolean ret;
	gchar *package_ids_blank[] = {};
	gchar **package_ids;
	guint size;

	if (!egg_test_start (test, "PkPackageIds"))
		return;

	/************************************************************
	 ****************          IDENTS          ******************
	 ************************************************************/

	egg_test_title (test, "parse va_list");
	package_ids = pk_package_ids_test_va_list ("foo;0.0.1;i386;fedora", "bar;0.1.1;noarch;livna", NULL);
	egg_test_assert (test, package_ids != NULL);

	/************************************************************/
	egg_test_title (test, "correct size");
	size = pk_package_ids_size (package_ids);
	egg_test_assert (test, size == 2);

	/************************************************************/
	egg_test_title (test, "verify blank");
	ret = pk_package_ids_check (package_ids_blank);
	egg_test_assert (test, !ret);

	/************************************************************/
	egg_test_title (test, "verify");
	ret = pk_package_ids_check (package_ids);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "first correct");
	ret = pk_package_id_equal_strings (package_ids[0], "foo;0.0.1;i386;fedora");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "second correct");
	ret = pk_package_id_equal_strings (package_ids[1], "bar;0.1.1;noarch;livna");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "print");
	ret = pk_package_ids_print (package_ids);
	egg_test_assert (test, ret);

	g_strfreev (package_ids);

	egg_test_end (test);
}
#endif

