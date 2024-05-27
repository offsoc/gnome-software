/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <locale.h>
#include <stdio.h>
#include <glib.h>
#include <gio/gio.h>

#include "gs-akmods-private.h"

static gchar *
gs_akmods_helper_read_stream (GInputStream *stream)
{
	GString *tmp = NULL;
	gchar buffer[4096];
	gssize did_read;
	g_autoptr (GError) local_error = NULL;

	if (stream == NULL)
		return NULL;

	tmp = g_string_new (NULL);
	while ((did_read = g_input_stream_read (stream, buffer, sizeof (buffer), NULL, &local_error)) > 0) {
		g_string_append_len (tmp, buffer, did_read);
	}

	if (local_error != NULL)
		g_debug ("Failed to read from stream: %s", local_error->message);

	return g_string_free (tmp, FALSE);
}

static int
gs_akmods_helper_check_result (const gchar *val_stdout,
			       gboolean with_print)
{
	g_assert (val_stdout != NULL);

	#define NOT_FOUND_OUTPUT GS_AKMODS_KEY_FILENAME " not found\n"
	#define NOT_ENROLLED_OUTPUT GS_AKMODS_KEY_FILENAME " is not enrolled\n"
	#define PENDING_OUTPUT GS_AKMODS_KEY_FILENAME " is already in the enrollment request\n"
	#define ENROLLED_OUTPUT GS_AKMODS_KEY_FILENAME " is already enrolled\n"

	if (g_ascii_strncasecmp (val_stdout, NOT_FOUND_OUTPUT, strlen (NOT_FOUND_OUTPUT)) == 0) {
		return GS_AKMODS_STATE_NOT_FOUND;
	} else if (g_ascii_strncasecmp (val_stdout, NOT_ENROLLED_OUTPUT, strlen (NOT_ENROLLED_OUTPUT)) == 0) {
		return GS_AKMODS_STATE_NOT_ENROLLED;
	} else if (g_ascii_strncasecmp (val_stdout, PENDING_OUTPUT, strlen (PENDING_OUTPUT)) == 0) {
		return GS_AKMODS_STATE_PENDING;
	} else if (g_ascii_strncasecmp (val_stdout, ENROLLED_OUTPUT, strlen (ENROLLED_OUTPUT)) == 0) {
		return GS_AKMODS_STATE_ENROLLED;
	} else if (with_print) {
		g_printerr ("Unexpected output '%s'", val_stdout);
	}

	#undef NOT_FOUND_OUTPUT
	#undef NOT_ENROLLED_OUTPUT
	#undef PENDING_OUTPUT
	#undef ENROLLED_OUTPUT

	return GS_AKMODS_STATE_ERROR;
}

static int
gs_akmods_helper_test (gboolean with_print)
{
	const gchar *args[] = {
		"mokutil",
		"--test-key",
		GS_AKMODS_KEY_FILENAME,
		NULL
	};
	g_autofree gchar *val_stdout = NULL;
	g_autofree gchar *val_stderr = NULL;
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autoptr(GError) local_error = NULL;

	if (!g_file_test (GS_AKMODS_KEY_PATH, G_FILE_TEST_IS_DIR)) {
		if (with_print)
			g_printerr ("Akmods key directory not found.\n");
		return GS_AKMODS_STATE_ERROR;
	}

	subprocess = g_subprocess_newv ((const gchar * const *) args,
					G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
					&local_error);
	if (subprocess == NULL) {
		if (with_print)
			g_printerr ("Failed to call 'mokutil --test-key': %s\n", local_error->message);
		return GS_AKMODS_STATE_ERROR;
	}

	val_stdout = gs_akmods_helper_read_stream (g_subprocess_get_stdout_pipe (subprocess));
	val_stderr = gs_akmods_helper_read_stream (g_subprocess_get_stderr_pipe (subprocess));

	if (!g_subprocess_wait_check (subprocess, NULL, &local_error)) {
		if ((val_stdout == NULL || *val_stdout == '\0') && val_stderr != NULL && *val_stderr != '\0') {
			#define NOT_FOUND_ERROR "Failed to open " GS_AKMODS_KEY_FILENAME "\n"
			if (g_ascii_strncasecmp (val_stderr, NOT_FOUND_ERROR, strlen (NOT_FOUND_ERROR)) == 0)
				return GS_AKMODS_STATE_NOT_FOUND;
			#undef NOT_FOUND_ERROR
			if (with_print)
				g_printerr ("Failed to call 'mokutil --test-key': %s", val_stderr);
			return GS_AKMODS_STATE_ERROR;
		} else if (val_stdout != NULL && g_error_matches (local_error, G_SPAWN_EXIT_ERROR, 1)) {
			/* it can mean: "pending to be enrolled" or "already enrolled" */
			return gs_akmods_helper_check_result (val_stdout, with_print);
		} else {
			if (with_print) {
				g_printerr ("Failed to call 'mokutil --test-key': %s%s%s%s%s\n", local_error->message,
					    val_stdout != NULL && *val_stdout != '\0' ? "\nstdout: " : "",
					    val_stdout != NULL && *val_stdout != '\0' ? val_stdout : "",
					    val_stderr != NULL && *val_stderr != '\0' ? "\nstderr: " : "",
					    val_stderr != NULL && *val_stderr != '\0' ? val_stderr : "");
			}
			return GS_AKMODS_STATE_ERROR;
		}
	} else {
		if (val_stderr != NULL && *val_stderr != '\0') {
			if (with_print)
				g_printerr ("Something failed while calling 'mokutil --test-key': %s\n", val_stderr);
			return GS_AKMODS_STATE_ERROR;
		}
	}

	return gs_akmods_helper_check_result (val_stdout, with_print);
}

static int
gs_akmods_helper_generate (void)
{
	const gchar *args[] = {
		"kmodgenca",
		"-a",
		NULL
	};
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autoptr(GError) local_error = NULL;
	g_autofree gchar *val_stdout = NULL;
	g_autofree gchar *val_stderr = NULL;

	subprocess = g_subprocess_newv ((const gchar * const *) args,
					G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
					&local_error);
	if (subprocess == NULL) {
		g_printerr ("Failed to call 'kmodgenca': %s\n", local_error->message);
		return GS_AKMODS_STATE_ERROR;
	}

	val_stdout = gs_akmods_helper_read_stream (g_subprocess_get_stdout_pipe (subprocess));
	val_stderr = gs_akmods_helper_read_stream (g_subprocess_get_stderr_pipe (subprocess));

	if (!g_subprocess_wait_check (subprocess, NULL, &local_error)) {
		if ((val_stdout == NULL || *val_stdout == '\0') && val_stderr != NULL && *val_stderr != '\0') {
			g_printerr ("Failed to call 'kmodgenca': %s\n", val_stderr);
		} else {
			g_printerr ("Failed to call 'kmodgenca': %s%s%s%s%s\n", local_error->message,
				    val_stdout != NULL && *val_stdout != '\0' ? "\nstdout: " : "",
				    val_stdout != NULL && *val_stdout != '\0' ? val_stdout : "",
				    val_stderr != NULL && *val_stderr != '\0' ? "\nstderr: " : "",
				    val_stderr != NULL && *val_stderr != '\0' ? val_stderr : "");
		}
		return GS_AKMODS_STATE_ERROR;
	}
	/* stderr contains keygen random data, thus do not treat it as "something failed" */

	return GS_AKMODS_STATE_NOT_ENROLLED;
}

static int
gs_akmods_helper_import (void)
{
	const gchar *args[] = {
		"mokutil",
		"--import",
		GS_AKMODS_KEY_FILENAME,
		NULL
	};

	GOutputStream *stdin_stream;
	g_autofree gchar *val_stdout = NULL;
	g_autofree gchar *val_stderr = NULL;
	g_autoptr(GString) password = g_string_new (NULL);
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autoptr(GError) local_error = NULL;
	int chr;

	/* the password comes on stdin */
	while ((chr = getchar ()) != EOF) {
		g_string_append_c (password, chr);
	}

	if (password->len == 0) {
		g_printerr ("Password cannot be empty.\n");
		return GS_AKMODS_STATE_ERROR;
	}

	subprocess = g_subprocess_newv ((const gchar * const *) args,
					G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
					&local_error);
	if (subprocess == NULL) {
		g_printerr ("Failed to call 'mokutil --import': %s\n", local_error->message);
		return GS_AKMODS_STATE_ERROR;
	}

	stdin_stream = g_subprocess_get_stdin_pipe (subprocess);
	/* input password */
	if (!g_output_stream_write_all (stdin_stream, password->str, password->len, NULL, NULL, &local_error) ||
	    !g_output_stream_write_all (stdin_stream, "\n", 1, NULL, NULL, &local_error) ||
	    /* input password again */
	    !g_output_stream_write_all (stdin_stream, password->str, password->len, NULL, NULL, &local_error) ||
	    !g_output_stream_write_all (stdin_stream, "\n", 1, NULL, NULL, &local_error) ||
	    /* flush what had been written */
	    !g_output_stream_flush (stdin_stream, NULL, &local_error)) {
		g_printerr ("Failed to enter password to 'mokutil --import': %s\n", local_error->message);
		return GS_AKMODS_STATE_ERROR;
	}

	val_stdout = gs_akmods_helper_read_stream (g_subprocess_get_stdout_pipe (subprocess));
	val_stderr = gs_akmods_helper_read_stream (g_subprocess_get_stderr_pipe (subprocess));

	if (!g_subprocess_wait_check (subprocess, NULL, &local_error)) {
		if ((val_stdout == NULL || *val_stdout == '\0') && val_stderr != NULL && *val_stderr != '\0') {
			g_printerr ("Failed to call 'mokutil --import': %s\n", val_stderr);
		} else {
			g_printerr ("Failed to call 'mokutil --import': %s%s%s%s%s\n", local_error->message,
				    val_stdout != NULL && *val_stdout != '\0' ? "\nstdout: " : "",
				    val_stdout != NULL && *val_stdout != '\0' ? val_stdout : "",
				    val_stderr != NULL && *val_stderr != '\0' ? "\nstderr: " : "",
				    val_stderr != NULL && *val_stderr != '\0' ? val_stderr : "");
		}
		return GS_AKMODS_STATE_ERROR;
	} else {
		if (val_stderr != NULL && *val_stderr != '\0') {
			g_printerr ("Something failed while calling 'mokutil --import': %s\n", val_stderr);
			return GS_AKMODS_STATE_ERROR;
		}
	}

	return GS_AKMODS_STATE_PENDING;
}

static int
gs_akmods_helper_enroll (void)
{
	GsAkmodsState state = gs_akmods_helper_test (FALSE);
	if (state == GS_AKMODS_STATE_ERROR)
		return gs_akmods_helper_test (TRUE);
	if (state == GS_AKMODS_STATE_ENROLLED || state == GS_AKMODS_STATE_PENDING)
		return state;
	if (state == GS_AKMODS_STATE_NOT_FOUND)
		state = gs_akmods_helper_generate ();
	if (state == GS_AKMODS_STATE_NOT_ENROLLED)
		state = gs_akmods_helper_import ();
	return state;
}

int
main (int argc,
      const char *argv[])
{
	setlocale (LC_ALL, "");

	if (argc != 2) {
		g_printerr ("Requires one argument, --test or --enroll\n");
		return GS_AKMODS_STATE_ERROR;
	}

	if (g_strcmp0 (argv[1], "--test") == 0)
		return gs_akmods_helper_test (TRUE);

	if (g_strcmp0 (argv[1], "--enroll") == 0)
		return gs_akmods_helper_enroll ();

	g_printerr ("Unknown argument '%s'\n", argv[1]);
	return GS_AKMODS_STATE_ERROR;
}
