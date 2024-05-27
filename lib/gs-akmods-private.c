/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <gio/gio.h>

#include "gs-akmods-private.h"

static gchar *
gs_akmods_read_stream (GInputStream *stream,
		       GCancellable *cancellable)
{
	GString *tmp = NULL;
	gchar buffer[4096];
	gssize did_read;
	g_autoptr (GError) local_error = NULL;

	if (stream == NULL)
		return NULL;

	tmp = g_string_new (NULL);
	while ((did_read = g_input_stream_read (stream, buffer, sizeof (buffer), cancellable, &local_error)) > 0) {
		g_string_append_len (tmp, buffer, did_read);
	}

	if (local_error != NULL)
		g_debug ("akmods: Failed to read from stream: %s", local_error->message);

	return g_string_free (tmp, FALSE);
}

static void
gs_akmods_read_subprocess_pipes (GSubprocess *subprocess,
				 gchar **out_stdout,
				 gchar **out_stderr,
				 GCancellable *cancellable)
{
	if (out_stdout != NULL)
		*out_stdout = gs_akmods_read_stream (g_subprocess_get_stdout_pipe (subprocess), cancellable);

	if (out_stderr != NULL)
		*out_stderr = gs_akmods_read_stream (g_subprocess_get_stderr_pipe (subprocess), cancellable);
}

static GsAkmodsState
gs_akmods_execute_sync (const gchar * const *args,
			const GString *stdin_str,
			GCancellable *cancellable,
			GError **error)
{
	g_autofree gchar *val_stdout = NULL;
	g_autofree gchar *val_stderr = NULL;
	g_autoptr(GSubprocess) subprocess = NULL;
	g_autoptr(GError) local_error = NULL;
	g_autoptr(GError) result_error = NULL;

	subprocess = g_subprocess_newv (args, (stdin_str == NULL ? 0 : G_SUBPROCESS_FLAGS_STDIN_PIPE) |
					G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
					error);
	if (subprocess == NULL)
		return GS_AKMODS_STATE_ERROR;

	if (stdin_str != NULL) {
		GOutputStream *stream = g_subprocess_get_stdin_pipe (subprocess);
		if (stream == NULL) {
			g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to get stdin of the process");
			return GS_AKMODS_STATE_ERROR;
		}
		if (!g_output_stream_write_all (stream, stdin_str->str, stdin_str->len, NULL, cancellable, error) ||
		    !g_output_stream_flush (stream, cancellable, error) ||
		    !g_output_stream_close (stream, cancellable, error))
			return GS_AKMODS_STATE_ERROR;
	}

	gs_akmods_read_subprocess_pipes (subprocess, &val_stdout, &val_stderr, cancellable);

	if (!g_subprocess_wait_check (subprocess, cancellable, &local_error)) {
		if ((val_stdout == NULL || *val_stdout == '\0') && val_stderr != NULL && *val_stderr != '\0') {
			g_set_error_literal (&result_error, local_error->domain, local_error->code, val_stderr);
		} else {
			g_set_error (&result_error, local_error->domain, local_error->code,
				     "%s%s%s%s%s",
				     local_error->message,
				     val_stdout != NULL && *val_stdout != '\0' ? "\nstdout: " : "",
				     val_stdout != NULL && *val_stdout != '\0' ? val_stdout : "",
				     val_stderr != NULL && *val_stderr != '\0' ? "\nstderr: " : "",
				     val_stderr != NULL && *val_stderr != '\0' ? val_stderr : "");
		}
	} else {
		if (g_cancellable_set_error_if_cancelled (cancellable, &local_error)) {
			g_propagate_error (&result_error, g_steal_pointer (&local_error));
		} else if (val_stderr != NULL && *val_stderr != '\0') {
			g_set_error_literal (&result_error, G_IO_ERROR, G_IO_ERROR_FAILED, val_stderr);
		} else {
			return GS_AKMODS_STATE_ENROLLED;
		}
	}

	if (result_error != NULL && result_error->domain == G_SPAWN_EXIT_ERROR) {
		switch (result_error->code) {
		case GS_AKMODS_STATE_ENROLLED:
		case GS_AKMODS_STATE_NOT_FOUND:
		case GS_AKMODS_STATE_NOT_ENROLLED:
		case GS_AKMODS_STATE_PENDING:
			return result_error->code;
		case GS_AKMODS_STATE_ERROR:
		default:
			break;
		}
	}

	if (result_error != NULL)
		g_propagate_error (error, g_steal_pointer (&result_error));

	return GS_AKMODS_STATE_ERROR;
}

/*
 * gs_akmods_get_key_state_sync:
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Synchronously checks what state the akmods key currently is.
 *
 * Returns: one of #GsAkmodsState
 *
 * Since: 47
 **/
GsAkmodsState
gs_akmods_get_key_state_sync (GCancellable *cancellable,
			      GError **error)
{
	static GsAkmodsState last_state;
	static gint64 last_state_time = 0;
	const gchar *args[] = {
		"pkexec",
		LIBEXECDIR "/gnome-software-akmods-helper",
		"--test",
		NULL
	};

	if (!g_file_test (GS_AKMODS_KEY_PATH, G_FILE_TEST_IS_DIR)) {
		g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, "Akmods key directory not found.");
		return GS_AKMODS_STATE_ERROR;
	}

	/* consider state discovered within the last 5 seconds still valid */
	if (g_get_real_time () > last_state_time + (G_USEC_PER_SEC * 5)) {
		last_state = gs_akmods_execute_sync ((const gchar * const *) args, NULL, cancellable, error);
		last_state_time = g_get_real_time ();
	}

	return last_state;
}

/*
 * gs_akmods_enroll_sync:
 * @password: (not nullable): an import password
 * @cancellable: a #GCancellable, or %NULL
 * @error: a #GError, or %NULL
 *
 * Synchronously enrolls the akmods key. It will create one, if no such exists yet.
 * The import @password is to be used in MOK on reboot.
 *
 * Returns: one of #GsAkmodsState
 *
 * Since: 47
 **/
GsAkmodsState
gs_akmods_enroll_sync (const GString *password,
		       GCancellable *cancellable,
		       GError **error)
{
	const gchar *args[] = {
		"pkexec",
		LIBEXECDIR "/gnome-software-akmods-helper",
		"--enroll",
		NULL
	};

	g_assert (password != NULL);

	return gs_akmods_execute_sync ((const gchar * const *) args, password, cancellable, error);
}

static GsSecurebootState secureboot_state = GS_SECUREBOOT_STATE_UNKNOWN;

/*
 * gs_akmods_get_secureboot_state_sync:
 *
 * Enumerates secure boot state of the system, if not known,
 * and saves it for later use by gs_akmods_get_last_secureboot_state().
 * If known, then just returns that value.
 *
 * It can happen the return value is %GS_SECUREBOOT_STATE_UNKNOWN, for
 * example when the mokutil is not installed or calling it failed.
 *
 * Returns: one of #GsSecurebootState
 *
 * Since: 47
 **/
GsSecurebootState
gs_akmods_get_secureboot_state_sync (void)
{
	g_autofree gchar *standard_output = NULL;
	g_autofree gchar *standard_error = NULL;
	g_autoptr(GError) local_error = NULL;
	gint wait_status = 0;
	const gchar *argv[] = { "mokutil", "--sb-state", NULL };

	if (secureboot_state != GS_SECUREBOOT_STATE_UNKNOWN)
		return secureboot_state;

	if (!g_spawn_sync (NULL, (gchar **) argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &standard_output, &standard_error, &wait_status, &local_error)) {
		g_debug ("akmods: Failed to enum SecureBoot state: '%s'%s%s", local_error != NULL ? local_error->message : "Unknown error",
			 standard_error != NULL && *standard_error != '\0' ? " standard error:" : "",
			 standard_error != NULL && *standard_error != '\0' ? standard_error : "");
		return secureboot_state;
	}

	if (standard_output == NULL) {
		g_debug ("akmods: No standard output from '%s'", argv[0]);
		return secureboot_state;
	}

	#define ENABLED_OUTPUT "SecureBoot enabled\n"
	#define DISABLED_OUTPUT "SecureBoot disabled\n"
	#define NOT_SUPPORTED_OUTPUT "EFI variables are not supported on this system\n"

	if (g_ascii_strncasecmp (standard_output, ENABLED_OUTPUT, strlen (ENABLED_OUTPUT)) == 0)
		secureboot_state = GS_SECUREBOOT_STATE_ENABLED;
	else if (g_ascii_strncasecmp (standard_output, DISABLED_OUTPUT, strlen (DISABLED_OUTPUT)) == 0)
		secureboot_state = GS_SECUREBOOT_STATE_DISABLED;
	else if (*standard_output == '\0' && standard_error != NULL &&
		 g_ascii_strncasecmp (standard_error, NOT_SUPPORTED_OUTPUT, strlen (NOT_SUPPORTED_OUTPUT)) == 0)
		secureboot_state = GS_SECUREBOOT_STATE_NOT_SUPPORTED;
	else
		g_debug ("akmods: Unexpected response from '%s': '%s'; stderr:'%s'", argv[0], standard_output, standard_error);

	#undef ENABLED_OUTPUT
	#undef DISABLED_OUTPUT
	#undef NOT_SUPPORTED_OUTPUT

	return secureboot_state;
}

/*
 * gs_akmods_get_last_secureboot_state:
 *
 * Returns last recognized state from gs_akmods_get_secureboot_state_sync().
 *
 * Returns: previously enumerated secure boot state
 *
 * Since: 47
 **/
GsSecurebootState
gs_akmods_get_last_secureboot_state (void)
{
	return secureboot_state;
}
