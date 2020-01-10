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

/**
 * SECTION:pk-package-id
 * @short_description: Functionality to modify a PackageID
 *
 * PackageId's are difficult to read and create.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <glib/gi18n.h>

#include "egg-debug.h"
#include "egg-string.h"

#include <packagekit-glib/pk-common.h>
#include <packagekit-glib/pk-package-id.h>

/**
 * pk_package_id_new:
 *
 * Creates a new #PkPackageId object with default values
 *
 * Return value: a new #PkPackageId object
 **/
PkPackageId *
pk_package_id_new (void)
{
	PkPackageId *id;
	id = g_new0 (PkPackageId, 1);
	id->name = NULL;
	id->version = NULL;
	id->arch = NULL;
	id->data = NULL;
	return id;
}

/**
 * pk_package_id_get_name:
 *
 * Gets name property from given #PkPackageId object
 *
 * Return value: name property of given #PkPackageId object
 **/
const gchar *
pk_package_id_get_name (const PkPackageId *id)
{
	g_return_val_if_fail (id != NULL, NULL);
	return id->name;
}

/**
 * pk_package_id_get_version:
 *
 * Gets version property from given #PkPackageId object
 *
 * Return value: version property of given #PkPackageId object
 **/
const gchar *
pk_package_id_get_version (const PkPackageId *id)
{
	g_return_val_if_fail (id != NULL, NULL);
	return id->version;
}

/**
 * pk_package_id_get_arch:
 *
 * Gets arch property from given #PkPackageId object
 *
 * Return value: arch property of given #PkPackageId object
 **/
const gchar *
pk_package_id_get_arch (const PkPackageId *id)
{
	g_return_val_if_fail (id != NULL, NULL);
	return id->arch;
}

/**
 * pk_package_id_get_data:
 *
 * Gets data property from given #PkPackageId object
 *
 * Return value: data property of given #PkPackageId object
 **/
const gchar *
pk_package_id_get_data (const PkPackageId *id)
{
	g_return_val_if_fail (id != NULL, NULL);
	return id->data;
}

/**
 * pk_strsplit:
 * @id: the ; delimited string to split
 * @parts: how many parts the delimted string should be split into
 *
 * Splits a string into the correct number of parts, checking the correct
 * number of delimiters are present.
 *
 * Return value: a char array is split correctly, %NULL if invalid
 * Note: You need to use g_strfreev on the returned value
 **/
static gchar **
pk_strsplit (const gchar *id, guint parts)
{
	gchar **sections = NULL;

	if (id == NULL) {
		goto out;
	}

	/* split by delimeter ';' */
	sections = g_strsplit (id, ";", 0);
	if (g_strv_length (sections) != parts) {
		goto out;
	}

	/* name has to be valid */
	if (egg_strzero (sections[0])) {
		goto out;
	}

	/* all okay, phew.. */
	return sections;

out:
	/* free sections and return NULL */
	if (sections != NULL) {
		g_strfreev (sections);
	}
	return NULL;
}

/**
 * pk_package_id_check:
 * @package_id: the text the check
 *
 * Return value: %TRUE if the package_id was well formed.
 **/
gboolean
pk_package_id_check (const gchar *package_id)
{
	gchar **sections;
	gboolean ret;

	if (package_id == NULL) {
		return FALSE;
	}
	ret = g_utf8_validate (package_id, -1, NULL);
	if (!ret) {
		egg_warning ("invalid UTF8!");
		return FALSE;
	}
	sections = pk_strsplit (package_id, 4);
	if (sections != NULL) {
		g_strfreev (sections);
		return TRUE;
	}
	return FALSE;
}

/**
 * pk_package_id_new_from_string:
 * @package_id: the text to pre-fill the object
 *
 * Creates a new #PkPackageId object with values taken from the supplied id.
 *
 * Return value: a new #PkPackageId object
 **/
PkPackageId *
pk_package_id_new_from_string (const gchar *package_id)
{
	gchar **sections;
	PkPackageId *id = NULL;

	g_return_val_if_fail (package_id != NULL, NULL);

	sections = pk_strsplit (package_id, 4);
	if (sections == NULL) {
		return NULL;
	}

	/* create new object */
	id = pk_package_id_new ();
	if (!egg_strzero (sections[0]))
		id->name = g_strdup (sections[0]);
	if (!egg_strzero (sections[1]))
		id->version = g_strdup (sections[1]);
	if (!egg_strzero (sections[2]))
		id->arch = g_strdup (sections[2]);
	if (!egg_strzero (sections[3]))
		id->data = g_strdup (sections[3]);
	g_strfreev (sections);
	return id;
}

/**
 * pk_package_id_new_from_list:
 * @name: the package name
 * @version: the package version
 * @arch: the package architecture
 * @data: the package extra data
 *
 * Creates a new #PkPackageId object with values.
 *
 * Return value: a new #PkPackageId object
 **/
PkPackageId *
pk_package_id_new_from_list (const gchar *name, const gchar *version, const gchar *arch, const gchar *data)
{
	PkPackageId *id = NULL;

	g_return_val_if_fail (name != NULL, NULL);

	/* create new object */
	id = pk_package_id_new ();
	id->name = g_strdup (name);
	id->version = g_strdup (version);
	id->arch = g_strdup (arch);
	id->data = g_strdup (data);
	return id;
}

/**
 * pk_package_id_copy:
 * @id: the %PkPackageId structure to copy
 *
 * Copies into a new #PkPackageId object.
 *
 * Return value: a new #PkPackageId object
 **/
PkPackageId *
pk_package_id_copy (const PkPackageId *id)
{
	return pk_package_id_new_from_list (id->name, id->version, id->arch, id->data);
}

/**
 * pk_package_id_to_string:
 * @id: A #PkPackageId object
 *
 * Return value: returns a string representation of #PkPackageId.
 **/
gchar *
pk_package_id_to_string (const PkPackageId *id)
{
	g_return_val_if_fail (id != NULL, NULL);
	g_return_val_if_fail (id->name != NULL, NULL);
	return g_strdup_printf ("%s;%s;%s;%s", id->name,
				id->version != NULL ? id->version : "",
				id->arch != NULL ? id->arch : "",
				id->data != NULL ? id->data : "");
}

/**
 * pk_package_id_build:
 * @name: the package name
 * @version: the package version
 * @arch: the package architecture
 * @data: the package extra data
 *
 * Return value: returns a string putting together the data.
 **/
gchar *
pk_package_id_build (const gchar *name, const gchar *version,
		     const gchar *arch, const gchar *data)
{
	g_return_val_if_fail (name != NULL, NULL);
	return g_strdup_printf ("%s;%s;%s;%s", name,
				version != NULL ? version : "",
				arch != NULL ? arch : "",
				data != NULL ? data : "");
}

/**
 * pk_package_id_free:
 * @id: the #PkPackageId object
 *
 * Return value: %TRUE if the #PkPackageId object was freed.
 **/
gboolean
pk_package_id_free (PkPackageId *id)
{
	if (id == NULL) {
		return FALSE;
	}
	g_free (id->name);
	g_free (id->arch);
	g_free (id->version);
	g_free (id->data);
	g_free (id);
	return TRUE;
}

/**
 * pk_arch_base_ix86:
 **/
static gboolean
pk_arch_base_ix86 (const gchar *arch)
{
	if (g_strcmp0 (arch, "i386") == 0 ||
	    g_strcmp0 (arch, "i486") == 0 ||
	    g_strcmp0 (arch, "i586") == 0 ||
	    g_strcmp0 (arch, "i686") == 0)
		return TRUE;
	return FALSE;
}

/**
 * pk_arch_fuzzy_equal:
 * @arch1: the first %PkPackageId
 * @arch2: the second %PkPackageId
 *
 * Compare the architectures in a fuzzy way
 *
 * Return value: %TRUE if the archs can be considered equal.
 **/
static gboolean
pk_arch_fuzzy_equal (const gchar *arch1, const gchar *arch2)
{
	if (g_strcmp0 (arch1, arch2) == 0)
		return TRUE;
	if (pk_arch_base_ix86 (arch1) && pk_arch_base_ix86 (arch2))
		return TRUE;
	return FALSE;
}

/**
 * pk_package_id_equal_fuzzy_arch:
 * @id1: the first %PkPackageId
 * @id2: the second %PkPackageId
 *
 * Only compare the name, version, and arch
 *
 * Return value: %TRUE if the ids can be considered equal.
 **/
gboolean
pk_package_id_equal_fuzzy_arch (const PkPackageId *id1, const PkPackageId *id2)
{
	if (g_strcmp0 (id1->name, id2->name) == 0 &&
	    g_strcmp0 (id1->version, id2->version) == 0 &&
	    pk_arch_fuzzy_equal (id1->arch, id2->arch))
		return TRUE;
	return FALSE;
}

/**
 * pk_package_id_equal:
 * @id1: the first %PkPackageId
 * @id2: the second %PkPackageId
 *
 * Only compare the name, version, and arch
 *
 * Return value: %TRUE if the ids can be considered equal.
 **/
gboolean
pk_package_id_equal (const PkPackageId *id1, const PkPackageId *id2)
{
	if (g_strcmp0 (id1->name, id2->name) == 0 &&
	    g_strcmp0 (id1->version, id2->version) == 0 &&
	    g_strcmp0 (id1->arch, id2->arch) == 0)
		return TRUE;
	return FALSE;
}

/**
 * pk_strcmp_sections:
 * @id1: the first item of text to test
 * @id2: the second item of text to test
 * @parts: the number of parts each id should have
 * @compare: the leading number of parts to compare
 *
 * We only want to compare some first sections, not all the data when
 * comparing package_id's and transaction_id's.
 *
 * Return value: %TRUE if the strings can be considered the same.
 *
 **/
static gboolean
pk_strcmp_sections (const gchar *id1, const gchar *id2, guint parts, guint compare)
{
	gchar **sections1;
	gchar **sections2;
	gboolean ret = FALSE;
	guint i;

	if (id1 == NULL || id2 == NULL) {
		egg_warning ("package id compare invalid '%s' and '%s'", id1, id2);
		return FALSE;
	}
	if (compare > parts) {
		egg_warning ("compare %i > parts %i", compare, parts);
		return FALSE;
	}
	if (compare == parts) {
		return (g_strcmp0 (id1, id2) == 0);
	}

	/* split, NULL will be returned if error */
	sections1 = pk_strsplit (id1, parts);
	sections2 = pk_strsplit (id2, parts);

	/* check we split okay */
	if (sections1 == NULL) {
		egg_warning ("string id compare sections1 invalid '%s'", id1);
		goto out;
	}
	if (sections2 == NULL) {
		egg_warning ("string id compare sections2 invalid '%s'", id2);
		goto out;
	}

	/* only compare preceeding sections */
	for (i=0; i<compare; i++) {
		if (g_strcmp0 (sections1[i], sections2[i]) != 0) {
			goto out;
		}
	}
	ret = TRUE;

out:
	g_strfreev (sections1);
	g_strfreev (sections2);
	return ret;
}

/**
 * pk_package_id_equal_strings:
 * @pid1: the first package_id
 * @pid2: the second package_id
 *
 * Only compare the first three sections, data is not part of the match
 *
 * Return value: %TRUE if the package_id's can be considered equal.
 **/
gboolean
pk_package_id_equal_strings (const gchar *pid1, const gchar *pid2)
{
	return pk_strcmp_sections (pid1, pid2, 4, 3);
}

/***************************************************************************
 ***                          MAKE CHECK TESTS                           ***
 ***************************************************************************/
#ifdef EGG_TEST
#include "egg-test.h"

void
pk_package_id_test (EggTest *test)
{
	gboolean ret;
	gchar *text;
	const gchar *temp;
	PkPackageId *id;
	PkPackageId *id2;
	gchar **array;

	if (!egg_test_start (test, "PkPackageId"))
		return;

	/************************************************************
	 ****************          id           ******************
	 ************************************************************/

	egg_test_title (test, "id build valid");
	text = pk_package_id_build ("moo", "0.0.1", "i386", "fedora");
	if (g_strcmp0 (text, "moo;0.0.1;i386;fedora") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);
	g_free (text);

	egg_test_title (test, "id build partial");
	text = pk_package_id_build ("moo", NULL, NULL, NULL);
	if (g_strcmp0 (text, "moo;;;") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got '%s', expected '%s'", text, "moo;;;");
	g_free (text);

	egg_test_title (test, "pid equal pass (same)");
	ret = pk_package_id_equal_strings ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;fedora");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "pid equal pass (different)");
	ret = pk_package_id_equal_strings ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;data");
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "get an id object");
	id = pk_package_id_new ();
	egg_test_assert (test, id != NULL);

	/************************************************************/
	egg_test_title (test, "test id freeing early");
	ret = pk_package_id_free (id);
	egg_test_assert (test, ret);

	/************************************************************/
	egg_test_title (test, "parse incorrect package_id from string (empty)");
	temp = "";
	id = pk_package_id_new_from_string (temp);
	if (id == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "passed an invalid string '%s'", temp);

	/************************************************************/
	egg_test_title (test, "parse incorrect package_id from string (not enough)");
	temp = "moo;0.0.1;i386";
	id = pk_package_id_new_from_string (temp);
	if (id == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "passed an invalid string '%s'", temp);

	/************************************************************/
	egg_test_title (test, "parse package_id from string");
	id = pk_package_id_new_from_string ("moo;0.0.1;i386;fedora");
	if (id != NULL &&
	    g_strcmp0 (id->name, "moo") == 0 &&
	    g_strcmp0 (id->arch, "i386") == 0 &&
	    g_strcmp0 (id->data, "fedora") == 0 &&
	    g_strcmp0 (id->version, "0.0.1") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "test copying");
	id2 = pk_package_id_copy (id);
	if (id2 != NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);

	/************************************************************/
	egg_test_title (test, "test id building with valid data");
	text = pk_package_id_to_string (id2);
	if (g_strcmp0 (text, "moo;0.0.1;i386;fedora") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "package_id is '%s'", text);
	g_free (text);
	pk_package_id_free (id);
	pk_package_id_free (id2);

	/************************************************************/
	egg_test_title (test, "test id building with partial data");
	id = pk_package_id_new_from_string ("moo;;;");
	text = pk_package_id_to_string (id);
	if (g_strcmp0 (text, "moo;;;") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "package_id is '%s', should be '%s'", text, "moo;;;");
	g_free (text);
	pk_package_id_free (id);

	/************************************************************/
	egg_test_title (test, "parse short package_id from string");
	id = pk_package_id_new_from_string ("moo;0.0.1;;");
	if (id != NULL &&
	    (g_strcmp0 (id->name, "moo") == 0) &&
	    (g_strcmp0 (id->version, "0.0.1") == 0) &&
	    id->data == NULL &&
	    id->arch == NULL)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, NULL);
	pk_package_id_free (id);

	egg_test_title (test, "id equal pass (same)");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;fedora", 4, 3);
	egg_test_assert (test, ret);

	egg_test_title (test, "id equal pass (parts==match)");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;fedora", 4, 4);
	egg_test_assert (test, ret);

	egg_test_title (test, "id equal pass (different)");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "moo;0.0.1;i386;data", 4, 3);
	egg_test_assert (test, ret);

	egg_test_title (test, "id equal fail1");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "moo;0.0.2;x64;fedora", 4, 3);
	egg_test_assert (test, !ret);

	egg_test_title (test, "id equal fail2");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "gnome;0.0.2;i386;fedora", 4, 3);
	egg_test_assert (test, !ret);

	egg_test_title (test, "id equal fail3");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "moo;0.0.3;i386;fedora", 4, 3);
	egg_test_assert (test, !ret);

	egg_test_title (test, "id equal fail (match too high)");
	ret = pk_strcmp_sections ("moo;0.0.1;i386;fedora", "moo;0.0.3;i386;fedora", 4, 5);
	egg_test_assert (test, !ret);

	/************************************************************
	 ****************          splitting         ****************
	 ************************************************************/
	egg_test_title (test, "test pass 1");
	array = pk_strsplit ("foo", 1);
	if (array != NULL &&
	    g_strcmp0 (array[0], "foo") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %s", array[0]);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "test pass 2");
	array = pk_strsplit ("foo;moo", 2);
	if (array != NULL &&
	    g_strcmp0 (array[0], "foo") == 0 &&
	    g_strcmp0 (array[1], "moo") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %s, %s", array[0], array[1]);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "test pass 3");
	array = pk_strsplit ("foo;moo;bar", 3);
	if (array != NULL &&
	    g_strcmp0 (array[0], "foo") == 0 &&
	    g_strcmp0 (array[1], "moo") == 0 &&
	    g_strcmp0 (array[2], "bar") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %s, %s, %s, %s", array[0], array[1], array[2], array[3]);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "test on real packageid");
	array = pk_strsplit ("kde-i18n-csb;4:3.5.8~pre20071001-0ubuntu1;all;", 4);
	if (array != NULL &&
	    g_strcmp0 (array[0], "kde-i18n-csb") == 0 &&
	    g_strcmp0 (array[1], "4:3.5.8~pre20071001-0ubuntu1") == 0 &&
	    g_strcmp0 (array[2], "all") == 0 &&
	    g_strcmp0 (array[3], "") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %s, %s, %s, %s", array[0], array[1], array[2], array[3]);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "test on short packageid");
	array = pk_strsplit ("kde-i18n-csb;4:3.5.8~pre20071001-0ubuntu1;;", 4);
	if (array != NULL &&
	    g_strcmp0 (array[0], "kde-i18n-csb") == 0 &&
	    g_strcmp0 (array[1], "4:3.5.8~pre20071001-0ubuntu1") == 0 &&
	    g_strcmp0 (array[2], "") == 0 &&
	    g_strcmp0 (array[3], "") == 0)
		egg_test_success (test, NULL);
	else
		egg_test_failed (test, "got %s, %s, %s, %s", array[0], array[1], array[2], array[3]);
	g_strfreev (array);

	/************************************************************/
	egg_test_title (test, "test fail under");
	array = pk_strsplit ("foo;moo", 1);
	egg_test_assert (test, array == NULL);

	/************************************************************/
	egg_test_title (test, "test fail over");
	array = pk_strsplit ("foo;moo", 3);
	egg_test_assert (test, array == NULL);

	/************************************************************/
	egg_test_title (test, "test fail missing first");
	array = pk_strsplit (";moo", 2);
	egg_test_assert (test, array == NULL);

	egg_test_end (test);
}
#endif

