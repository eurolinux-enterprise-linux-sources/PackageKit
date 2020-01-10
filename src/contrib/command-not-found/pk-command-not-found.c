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

#include "config.h"

#include <string.h>
#include <stdlib.h>
#include <locale.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <packagekit-glib2/packagekit.h>
#include <packagekit-glib2/packagekit-private.h>

#include "egg-debug.h"
#include "egg-string.h"

#define PK_MAX_PATH_LEN 1023

typedef enum {
	PK_CNF_POLICY_RUN,
	PK_CNF_POLICY_INSTALL,
	PK_CNF_POLICY_ASK,
	PK_CNF_POLICY_WARN,
	PK_CNF_POLICY_UNKNOWN
} PkCnfPolicy;

typedef struct {
	PkCnfPolicy	 single_match;
	PkCnfPolicy	 multiple_match;
	PkCnfPolicy	 single_install;
	PkCnfPolicy	 multiple_install;
	gboolean	 software_source_search;
	gchar		**locations;
} PkCnfPolicyConfig;

static PkTask *task = NULL;
static GCancellable *cancellable = NULL;

/* bash reserved code */
#define EXIT_COMMAND_NOT_FOUND	127

/**
 * pk_cnf_find_alternatives_swizzle:
 *
 * Swizzle ordering, e.g. amke -> make
 **/
static void
pk_cnf_find_alternatives_swizzle (const gchar *cmd, guint len, GPtrArray *array)
{
	guint i;
	gchar *possible;
	gchar swap;

	/*  */
	for (i=0; i<len-1; i++) {
		possible = g_strdup (cmd);
		swap = possible[i];
		possible[i] = possible[i+1];
		possible[i+1] = swap;
		g_ptr_array_add (array, possible);
	}
}

/**
 * pk_cnf_find_alternatives_replace:
 *
 * Replace some easily confused chars, e.g. gnome-power-managir to gnome-power-manager
 **/
static void
pk_cnf_find_alternatives_replace (const gchar *cmd, guint len, GPtrArray *array)
{
	guint i;
	gchar *possible;
	gchar temp;

	/* replace some easily confused chars */
	for (i=0; i<len; i++) {
		temp = cmd[i];
		if (temp == 'i') {
			possible = g_strdup (cmd);
			possible[i] = 'e';
			g_ptr_array_add (array, possible);
		}
		if (temp == 'e') {
			possible = g_strdup (cmd);
			possible[i] = 'i';
			g_ptr_array_add (array, possible);
		}
		if (temp == 'i') {
			possible = g_strdup (cmd);
			possible[i] = 'o';
			g_ptr_array_add (array, possible);
		}
		if (temp == 'c') {
			possible = g_strdup (cmd);
			possible[i] = 's';
			g_ptr_array_add (array, possible);
		}
		if (temp == 's') {
			possible = g_strdup (cmd);
			possible[i] = 'c';
			g_ptr_array_add (array, possible);
		}
		if (temp == 's') {
			possible = g_strdup (cmd);
			possible[i] = 'z';
			g_ptr_array_add (array, possible);
		}
		if (temp == 'z') {
			possible = g_strdup (cmd);
			possible[i] = 's';
			g_ptr_array_add (array, possible);
		}
		if (temp == 'k') {
			possible = g_strdup (cmd);
			possible[i] = 'c';
			g_ptr_array_add (array, possible);
		}
		if (temp == 'c') {
			possible = g_strdup (cmd);
			possible[i] = 'k';
			g_ptr_array_add (array, possible);
		}
	}
}

/**
 * pk_cnf_find_alternatives_truncate:
 *
 * Truncate first and last char, so lshall -> lshal
 **/
static void
pk_cnf_find_alternatives_truncate (const gchar *cmd, guint len, GPtrArray *array)
{
	guint i;
	gchar *possible;

	/* truncate last char */
	possible = g_strdup (cmd);
	possible[len-1] = '\0';
	g_ptr_array_add (array, possible);

	/* truncate first char */
	possible = g_strdup (cmd);
	for (i=0; i<len-1; i++)
		possible[i] = possible[i+1];
	possible[len-1] = '\0';
	g_ptr_array_add (array, possible);
}

/**
 * pk_cnf_find_alternatives_remove_double:
 *
 * Remove double chars, e.g. gnome-power-manaager -> gnome-power-manager
 **/
static void
pk_cnf_find_alternatives_remove_double (const gchar *cmd, guint len, GPtrArray *array)
{
	guint i, j;
	gchar *possible;

	for (i=1; i<len; i++) {
		if (cmd[i-1] == cmd[i]) {
			possible = g_strdup (cmd);
			for (j=i; j<len; j++)
				possible[j] = possible[j+1];
			possible[len-1] = '\0';
			g_ptr_array_add (array, possible);
		}
	}
}

/**
 * pk_cnf_find_alternatives_locale:
 *
 * Fix British spellings, e.g. colourdiff -> colordiff
 **/
static void
pk_cnf_find_alternatives_locale (const gchar *cmd, guint len, GPtrArray *array)
{
	guint i, j;
	gchar *possible;

	for (i=1; i<len; i++) {
		if (cmd[i-1] == 'o' && cmd[i] == 'u') {
			possible = g_strdup (cmd);
			for (j=i; j<len; j++)
				possible[j] = possible[j+1];
			possible[len-1] = '\0';
			g_ptr_array_add (array, possible);
		}
	}
}

/**
 * pk_cnf_find_alternatives_case:
 *
 * Remove double chars, e.g. Lshal -> lshal
 **/
static void
pk_cnf_find_alternatives_case (const gchar *cmd, guint len, GPtrArray *array)
{
	guint i;
	gchar *possible;
	gchar temp;

	for (i=0; i<len; i++) {
		temp = g_ascii_tolower (cmd[i]);
		if (temp != cmd[i]) {
			possible = g_strdup (cmd);
			possible[i] = temp;
			g_ptr_array_add (array, possible);
		}
		temp = g_ascii_toupper (cmd[i]);
		if (temp != cmd[i]) {
			possible = g_strdup (cmd);
			possible[i] = temp;
			g_ptr_array_add (array, possible);
		}
	}

	/* all lower */
	possible = g_strdup (cmd);
	for (i=0; i<len; i++)
		possible[i] = g_ascii_tolower (cmd[i]);
	if (strcmp (possible, cmd) != 0)
		g_ptr_array_add (array, possible);
	else
		g_free (possible);

	/* all upper */
	possible = g_strdup (cmd);
	for (i=0; i<len; i++)
		possible[i] = g_ascii_toupper (cmd[i]);
	if (strcmp (possible, cmd) != 0)
		g_ptr_array_add (array, possible);
	else
		g_free (possible);
}

/**
 * pk_cnf_find_alternatives:
 *
 * Generate a list of commands it might be
 **/
static GPtrArray *
pk_cnf_find_alternatives (const gchar *cmd, guint len)
{
	GPtrArray *array;
	GPtrArray *possible;
	GPtrArray *unique;
	const gchar *cmdt;
	const gchar *cmdt2;
	guint i, j;
	gchar buffer_bin[PK_MAX_PATH_LEN+1];
	gchar buffer_sbin[PK_MAX_PATH_LEN+1];
	gboolean ret;

	array = g_ptr_array_new_with_free_func (g_free);
	possible = g_ptr_array_new_with_free_func (g_free);
	unique = g_ptr_array_new ();
	pk_cnf_find_alternatives_swizzle (cmd, len, possible);
	pk_cnf_find_alternatives_replace (cmd, len, possible);
	if (len > 3)
		pk_cnf_find_alternatives_truncate (cmd, len, possible);
	pk_cnf_find_alternatives_remove_double (cmd, len, possible);
	pk_cnf_find_alternatives_case (cmd, len, possible);
	pk_cnf_find_alternatives_locale (cmd, len, possible);

	/* remove duplicates using a helper array */
	for (i=0; i<possible->len; i++) {
		cmdt = g_ptr_array_index (possible, i);
		ret = TRUE;
		for (j=0; j<unique->len; j++) {
			cmdt2 = g_ptr_array_index (unique, j);
			if (strcmp (cmdt, cmdt2) == 0) {
				ret = FALSE;
				break;
			}
		}
		/* only add if not duplicate */
		if (ret)
			g_ptr_array_add (unique, (gpointer) cmdt);
	}

	/* ITS4: ignore, source is constant size */
	strncpy (buffer_bin, "/usr/bin/", PK_MAX_PATH_LEN);

	/* ITS4: ignore, source is constant size */
	strncpy (buffer_sbin, "/usr/sbin/", PK_MAX_PATH_LEN);

	/* remove any that exist (fast path) */
	for (i=0; i<unique->len; i++) {
		cmdt = g_ptr_array_index (unique, i);

		/* ITS4: ignore, size is checked */
		strncpy (&buffer_bin[9], cmdt, PK_MAX_PATH_LEN-9);

		/* ITS4: ignore, size is checked */
		strncpy (&buffer_sbin[10], cmdt, PK_MAX_PATH_LEN-10);

		/* does file exist in bindir (common case) */
		ret = g_file_test (buffer_bin, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_EXECUTABLE);
		if (ret) {
			g_ptr_array_add (array, g_strdup (cmdt));
			continue;
		}

		/* does file exist in sbindir */
		ret = g_file_test (buffer_sbin, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_EXECUTABLE);
		if (ret)
			g_ptr_array_add (array, g_strdup (cmdt));
	}

	g_ptr_array_unref (possible);
	g_ptr_array_free (unique, TRUE);
	return array;
}

/**
 * pk_cnf_progress_cb:
 **/
static void
pk_cnf_progress_cb (PkProgress *progress, PkProgressType type, gpointer data)
{
	PkStatusEnum status;
	const gchar *text = NULL;

	/* status */
	if (type != PK_PROGRESS_TYPE_STATUS)
		return;

	g_object_get (progress,
		      "status", &status,
		      NULL);

	switch (status) {
	case PK_STATUS_ENUM_SETUP:
	case PK_STATUS_ENUM_FINISHED:
	case PK_STATUS_ENUM_QUERY:
		break;
	case PK_STATUS_ENUM_DOWNLOAD_REPOSITORY:
		/* TRANSLATORS: downloading repo data so we can search */
		text = _("Downloading details about the software sources.");
		break;
	case PK_STATUS_ENUM_DOWNLOAD_FILELIST:
		/* TRANSLATORS: downloading file lists so we can search */
		text = _("Downloading filelists (this may take some time to complete).");
		break;
	case PK_STATUS_ENUM_WAITING_FOR_LOCK:
		/* TRANSLATORS: waiting for native lock */
		text = _("Waiting for package manager lock.");
		break;
	case PK_STATUS_ENUM_LOADING_CACHE:
		/* TRANSLATORS: loading package cache so we can search */
		text = _("Loading list of packages.");
		break;
	default:
		/* fallback to default */
		text = pk_status_enum_to_localised_text (status);
	}

	/* print to screen, still one line */
	if (text != NULL)
		g_print ("\n * %s.. ", text);
}

/**
 * pk_cnf_find_available:
 *
 * Find software we could install
 **/
static gchar **
pk_cnf_find_available (const gchar *cmd)
{
	PkPackage *item;
	gchar **package_ids = NULL;
	const gchar *prefixes[] = {"/usr/bin", "/usr/sbin", "/bin", "/sbin", NULL};
	gchar **values = NULL;
	GError *error = NULL;
	GPtrArray *array = NULL;
	guint i;
	guint len;
	PkBitfield filters;
	PkResults *results = NULL;
	PkError *error_code = NULL;

	/* create new array of full paths */
	len = g_strv_length ((gchar **)prefixes);
	values = g_new0 (gchar *, len + 1);
	for (i=0; prefixes[i] != NULL; i++)
		values[i] = g_build_filename (prefixes[i], cmd, NULL);

	/* do search */
	filters = pk_bitfield_from_enums (PK_FILTER_ENUM_NOT_INSTALLED,
					  PK_FILTER_ENUM_NEWEST,
					  PK_FILTER_ENUM_ARCH, -1);
	results = pk_client_search_files (PK_CLIENT(task), filters, values, cancellable,
					 (PkProgressCallback) pk_cnf_progress_cb, NULL, &error);
	if (results == NULL) {
		/* TRANSLATORS: we failed to find the package, this shouldn't happen */
		g_printerr ("%s: %s\n", _("Failed to search for file"), error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		/* TRANSLATORS: the transaction failed in a way we could not expect */
		g_printerr ("%s: %s, %s\n", _("The transaction failed"), pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	/* get the packages returned */
	array = pk_results_get_package_array (results);
	package_ids = g_new0 (gchar *, array->len+1);
	for (i=0; i<array->len; i++) {
		item = g_ptr_array_index (array, i);
		package_ids[i] = g_strdup (pk_package_get_id (item));
	}
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
	if (array != NULL)
		g_ptr_array_unref (array);
	g_strfreev (values);
	return package_ids;
}

/**
 * pk_cnf_get_policy_from_string:
 **/
static PkCnfPolicy
pk_cnf_get_policy_from_string (const gchar *policy_text)
{
	if (policy_text == NULL)
		return PK_CNF_POLICY_UNKNOWN;
	if (strcmp (policy_text, "run") == 0)
		return PK_CNF_POLICY_RUN;
	if (strcmp (policy_text, "ask") == 0)
		return PK_CNF_POLICY_ASK;
	if (strcmp (policy_text, "warn") == 0)
		return PK_CNF_POLICY_WARN;
	return PK_CNF_POLICY_UNKNOWN;
}

/**
 * pk_cnf_get_policy_from_file:
 **/
static PkCnfPolicy
pk_cnf_get_policy_from_file (GKeyFile *file, const gchar *key)
{
	PkCnfPolicy policy;
	gchar *policy_text;
	GError *error = NULL;

	/* get from file */
	policy_text = g_key_file_get_string (file, "CommandNotFound", key, &error);
	if (policy_text == NULL) {
		egg_warning ("failed to get key %s: %s", key, error->message);
		g_error_free (error);
	}

	/* convert to enum */
	policy = pk_cnf_get_policy_from_string (policy_text);
	g_free (policy_text);
	return policy;
}

/**
 * pk_cnf_get_config:
 **/
static PkCnfPolicyConfig *
pk_cnf_get_config (void)
{
	GKeyFile *file;
	gchar *path;
	gboolean ret;
	GError *error = NULL;
	PkCnfPolicyConfig *config;

	/* create */
	config = g_new0 (PkCnfPolicyConfig, 1);

	/* set defaults if the conf file is not found */
	config->single_match = PK_CNF_POLICY_UNKNOWN;
	config->multiple_match = PK_CNF_POLICY_UNKNOWN;
	config->single_install = PK_CNF_POLICY_UNKNOWN;
	config->multiple_install = PK_CNF_POLICY_UNKNOWN;
	config->software_source_search = FALSE;
	config->locations = NULL;

	/* load file */
	file = g_key_file_new ();
	path = g_build_filename (SYSCONFDIR, "PackageKit", "CommandNotFound.conf", NULL);
	ret = g_key_file_load_from_file (file, path, G_KEY_FILE_NONE, &error);
	if (!ret) {
		egg_warning ("failed to open policy: %s", error->message);
		g_error_free (error);
		goto out;
	}

	/* get data */
	config->single_match = pk_cnf_get_policy_from_file (file, "SingleMatch");
	config->multiple_match = pk_cnf_get_policy_from_file (file, "MultipleMatch");
	config->single_install = pk_cnf_get_policy_from_file (file, "SingleInstall");
	config->multiple_install = pk_cnf_get_policy_from_file (file, "MultipleInstall");
	config->software_source_search = g_key_file_get_boolean (file, "CommandNotFound", "SoftwareSourceSearch", NULL);
	config->locations = g_key_file_get_string_list (file, "CommandNotFound", "SearchLocations", NULL, NULL);

	/* fallback */
	if (config->locations == NULL) {
		egg_warning ("not found SearchLocations, using fallback");
		config->locations = g_strsplit ("/usr/bin;/usr/sbin", ";", -1);
	}
out:
	g_free (path);
	g_key_file_free (file);
	return config;
}

/**
 * pk_cnf_spawn_command:
 **/
static gint
pk_cnf_spawn_command (const gchar *exec, gchar **arguments)
{
	gboolean ret;
	gint exit_status;
	gchar *cmd;
	gchar *args;
	GError *error = NULL;

	/* ensure program starts on a fresh line */
	g_print ("\n");

	args = g_strjoinv (" ", arguments);
	cmd = g_strjoin (" ", exec, args, NULL);
	ret = g_spawn_command_line_sync (cmd, NULL, NULL, &exit_status, &error);
	if (!ret) {
		/* TRANSLATORS: we failed to launch the executable, the error follows */
		g_printerr ("%s '%s': %s\n", _("Failed to launch:"), cmd, error->message);
		g_error_free (error);
		goto out;
	}
out:
	g_free (args);
	g_free (cmd);
	return ret;
}

/**
 * pk_cnf_install_package_id:
 **/
static gboolean
pk_cnf_install_package_id (const gchar *package_id)
{
	GError *error = NULL;
	PkResults *results = NULL;
	gchar **package_ids;
	gboolean ret = FALSE;
	PkError *error_code = NULL;

	/* do install */
	package_ids = pk_package_ids_from_id (package_id);
	results = pk_task_install_packages_sync (task, package_ids, cancellable,
						 (PkProgressCallback) pk_cnf_progress_cb, NULL, &error);
	if (results == NULL) {
		/* TRANSLATORS: we failed to install the package */
		g_printerr ("%s: %s\n", _("Failed to install packages"), error->message);
		g_error_free (error);
		goto out;
	}

	/* check error code */
	error_code = pk_results_get_error_code (results);
	if (error_code != NULL) {
		/* TRANSLATORS: the transaction failed in a way we could not expect */
		g_printerr ("%s: %s, %s\n", _("The transaction failed"), pk_error_enum_to_text (pk_error_get_code (error_code)), pk_error_get_details (error_code));
		goto out;
	}

	ret = TRUE;
out:
	if (error_code != NULL)
		g_object_unref (error_code);
	if (results != NULL)
		g_object_unref (results);
	g_strfreev (package_ids);
	return ret;
}

/**
 * pk_cnf_sigint_handler:
 **/
static void
pk_cnf_sigint_handler (int sig)
{
	egg_debug ("Handling SIGINT");

	/* restore default ASAP, as the cancel might hang */
	signal (SIGINT, SIG_DFL);

	/* hopefully, cancel client */
	g_cancellable_cancel (cancellable);

	/* kill ourselves */
	egg_debug ("Retrying SIGINT");
	kill (getpid (), SIGINT);
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	gboolean ret;
	GOptionContext *context;
	GPtrArray *array = NULL;
	gchar **package_ids = NULL;
	PkCnfPolicyConfig *config = NULL;
	guint i;
	guint len;
	gchar *text;
	const gchar *possible;
	gchar **parts;
	guint retval = EXIT_SUCCESS;

	const GOptionEntry options[] = {
		{ NULL}
	};

	setlocale (LC_ALL, "");
	bindtextdomain (GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	if (! g_thread_supported ())
		g_thread_init (NULL);
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (NULL);
	/* TRANSLATORS: tool that gets called when the command is not found */
	g_option_context_set_summary (context, _("PackageKit Command Not Found"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_add_group (context, egg_debug_get_option_group ());
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);

	/* no input */
	if (argv[1] == NULL)
		goto out;

	/* do stuff on ctrl-c */
	signal (SIGINT, pk_cnf_sigint_handler);

	/* get policy config */
	config = pk_cnf_get_config ();
	task = PK_TASK(pk_task_text_new ());
	cancellable = g_cancellable_new ();

	/* get length */
	len = egg_strlen (argv[1], 1024);
	if (len < 1) {
		retval = EXIT_COMMAND_NOT_FOUND;
		goto out;
	}

	/* generate swizzles */
	array = pk_cnf_find_alternatives (argv[1], len);

	/* TRANSLATORS: the prefix of all the output telling the user why it's not executing */
	g_printerr ("%s ", _("Command not found."));

	/* one exact possibility */
	if (array->len == 1) {
		possible = g_ptr_array_index (array, 0);
		if (config->single_match == PK_CNF_POLICY_WARN) {
			/* TRANSLATORS: tell the user what we think the command is */
			g_printerr ("%s '%s'\n", _("Similar command is:"), possible);
			retval = EXIT_COMMAND_NOT_FOUND;

		/* run */
		} else if (config->single_match == PK_CNF_POLICY_RUN) {
			retval = pk_cnf_spawn_command (possible, &argv[2]);

		/* ask */
		} else if (config->single_match == PK_CNF_POLICY_ASK) {
			/* TRANSLATORS: Ask the user if we should run the similar command */
			text = g_strdup_printf ("%s %s", _("Run similar command:"), possible);
			ret = pk_console_get_prompt (text, TRUE);
			if (ret)
				retval = pk_cnf_spawn_command (possible, &argv[2]);
			else
				retval = EXIT_COMMAND_NOT_FOUND;
			g_free (text);
		}
		goto out;

	/* multiple choice */
	} else if (array->len > 1) {
		if (config->multiple_match == PK_CNF_POLICY_WARN) {
			/* TRANSLATORS: show the user a list of commands that they could have meant */
			g_printerr ("%s:\n", _("Similar commands are:"));
			for (i=0; i<array->len; i++) {
				possible = g_ptr_array_index (array, i);
				g_printerr ("'%s'\n", possible);
			}

		/* ask */
		} else if (config->multiple_match == PK_CNF_POLICY_ASK) {
			/* TRANSLATORS: show the user a list of commands we could run */
			g_printerr ("%s:\n", _("Similar commands are:"));
			for (i=0; i<array->len; i++) {
				possible = g_ptr_array_index (array, i);
				g_printerr ("%i\t'%s'\n", i+1, possible);
			}

			/* TRANSLATORS: ask the user to choose a file to run */
			i = pk_console_get_number (_("Please choose a command to run"), array->len);

			/* run command */
			possible = g_ptr_array_index (array, i);
			retval = pk_cnf_spawn_command (possible, &argv[2]);
		}
		goto out;

	/* only search using PackageKit if configured to do so */
	} else if (config->software_source_search) {
		package_ids = pk_cnf_find_available (argv[1]);
		if (package_ids == NULL)
			goto out;
		len = g_strv_length (package_ids);
		if (len == 1) {
			parts = pk_package_id_split (package_ids[0]);
			if (config->single_install == PK_CNF_POLICY_WARN) {
				/* TRANSLATORS: tell the user what package provides the command */
				g_printerr ("%s '%s'\n", _("The package providing this file is:"), parts[PK_PACKAGE_ID_NAME]);

			/* ask */
			} else if (config->single_install == PK_CNF_POLICY_ASK) {
				/* TRANSLATORS: as the user if we want to install a package to provide the command */
				text = g_strdup_printf (_("Install package '%s' to provide command '%s'?"), parts[PK_PACKAGE_ID_NAME], argv[1]);
				ret = pk_console_get_prompt (text, FALSE);
				g_free (text);
				if (ret) {
					ret = pk_cnf_install_package_id (package_ids[0]);
					if (ret)
						retval = pk_cnf_spawn_command (argv[1], &argv[2]);
					else
						retval = EXIT_COMMAND_NOT_FOUND;
				}

			/* install */
			} else if (config->single_install == PK_CNF_POLICY_INSTALL) {
				ret = pk_cnf_install_package_id (package_ids[0]);
				if (ret)
					retval = pk_cnf_spawn_command (argv[1], &argv[2]);
				else
					retval = EXIT_COMMAND_NOT_FOUND;
			}
			g_strfreev (parts);
			goto out;
		} else if (len > 1) {
			if (config->multiple_install == PK_CNF_POLICY_WARN) {
				/* TRANSLATORS: Show the user a list of packages that provide this command */
				g_printerr ("%s\n", _("Packages providing this file are:"));
				for (i=0; package_ids[i] != NULL; i++) {
					parts = pk_package_id_split (package_ids[i]);
					g_printerr ("'%s'\n", parts[PK_PACKAGE_ID_NAME]);
					g_strfreev (parts);
				}

			/* ask */
			} else if (config->multiple_install == PK_CNF_POLICY_ASK) {
				/* TRANSLATORS: Show the user a list of packages that they can install to provide this command */
				g_printerr ("%s:\n", _("Suitable packages are:"));
				for (i=0; package_ids[i] != NULL; i++) {
					parts = pk_package_id_split (package_ids[i]);
					g_printerr ("%i\t'%s'\n", i+1, parts[PK_PACKAGE_ID_NAME]);
					g_strfreev (parts);
				}

				/* get selection */
				/* TRANSLATORS: ask the user to choose a file to install */
				i = pk_console_get_number (_("Please choose a package to install"), len);

				/* run command */
				ret = pk_cnf_install_package_id (package_ids[i]);
				if (ret)
					retval = pk_cnf_spawn_command (argv[1], &argv[2]);
				else
					retval = EXIT_COMMAND_NOT_FOUND;
			}
			goto out;
		}
	}

	g_printerr ("\n");

out:
	g_strfreev (package_ids);
	if (task != NULL)
		g_object_unref (task);
	if (cancellable != NULL)
		g_object_unref (cancellable);
	if (config != NULL) {
		g_strfreev (config->locations);
		g_free (config);
	}
	if (array != NULL)
		g_ptr_array_unref (array);

	return retval;
}

