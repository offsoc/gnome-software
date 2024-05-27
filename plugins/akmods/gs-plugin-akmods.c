/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gnome-software.h>

#include "gs-akmods-private.h"
#include "gs-worker-thread.h"

#include "gs-plugin-akmods.h"

struct _GsPluginAkmods
{
	GsPlugin	parent;

	GsWorkerThread *worker;  /* (owned) */
	gboolean	did_notify;
};

G_DEFINE_TYPE (GsPluginAkmods, gs_plugin_akmods, GS_TYPE_PLUGIN)

#define assert_in_worker(self) \
	g_assert (gs_worker_thread_is_in_worker_context (self->worker))

static void
gs_akmods_verify_secureboot_state_sync (GsPluginAkmods *self)
{
	GsSecurebootState sb_state = gs_akmods_get_secureboot_state_sync ();
	if (sb_state == GS_SECUREBOOT_STATE_DISABLED ||
	    sb_state == GS_SECUREBOOT_STATE_NOT_SUPPORTED) {
		g_debug ("Disabling plugin, because SecureBoot is %s", sb_state == GS_SECUREBOOT_STATE_DISABLED ? "disabled" : "not supported");
		gs_plugin_set_enabled (GS_PLUGIN (self), FALSE);
	}
}

/* Run in @worker. */
static void
gs_akmods_reload_thread_cb (GTask        *task,
			    gpointer      source_object,
			    gpointer      task_data,
			    GCancellable *cancellable)
{
	GsPluginAkmods *self = GS_PLUGIN_AKMODS (source_object);

	assert_in_worker (self);

	gs_akmods_verify_secureboot_state_sync (self);
}

static void
gs_plugin_akmods_reload (GsPlugin *plugin)
{
	g_debug ("%s", G_STRFUNC);
	if (gs_akmods_get_last_secureboot_state	() == GS_SECUREBOOT_STATE_UNKNOWN) {
		/* mokutil was not installed probably; the reload can be called when some
		   app/package had been installed, thus re-try to check SecureBoot state */
		GsPluginAkmods *self = GS_PLUGIN_AKMODS (plugin);
		gs_worker_thread_queue (self->worker, G_PRIORITY_DEFAULT,
					gs_akmods_reload_thread_cb, NULL);
	}
}

/* Run in @worker. */
static void
gs_akmods_setup_thread_cb (GTask        *task,
			   gpointer      source_object,
			   gpointer      task_data,
			   GCancellable *cancellable)
{
	GsPluginAkmods *self = GS_PLUGIN_AKMODS (source_object);

	assert_in_worker (self);

	gs_akmods_verify_secureboot_state_sync (self);

	g_task_return_boolean (task, TRUE);
}

static void
gs_plugin_akmods_setup_async (GsPlugin            *plugin,
                              GCancellable        *cancellable,
                              GAsyncReadyCallback  callback,
                              gpointer             user_data)
{
	GsPluginAkmods *self = GS_PLUGIN_AKMODS (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (plugin, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_akmods_setup_async);

	/* Start up a worker thread to process all the plugin’s function calls. */
	self->worker = gs_worker_thread_new ("gs-plugin-akmods");

	gs_worker_thread_queue (self->worker, G_PRIORITY_DEFAULT,
				gs_akmods_setup_thread_cb, g_steal_pointer (&task));
}

static gboolean
gs_plugin_akmods_setup_finish (GsPlugin      *plugin,
                               GAsyncResult  *result,
                               GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_akmods_shutdown_cb (GObject      *source_object,
                       GAsyncResult *result,
                       gpointer      user_data)
{
	g_autoptr(GTask) task = G_TASK (user_data);
	GsPluginAkmods *self = g_task_get_source_object (task);
	g_autoptr(GsWorkerThread) worker = NULL;
	g_autoptr(GError) local_error = NULL;

	worker = g_steal_pointer (&self->worker);

	if (!gs_worker_thread_shutdown_finish (worker, result, &local_error)) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	g_task_return_boolean (task, TRUE);
}

static void
gs_plugin_akmods_shutdown_async (GsPlugin            *plugin,
                                 GCancellable        *cancellable,
                                 GAsyncReadyCallback  callback,
                                 gpointer             user_data)
{
	GsPluginAkmods *self = GS_PLUGIN_AKMODS (plugin);
	g_autoptr(GTask) task = NULL;

	task = g_task_new (self, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_akmods_shutdown_async);

	/* Stop the worker thread. */
	gs_worker_thread_shutdown_async (self->worker, cancellable, gs_akmods_shutdown_cb, g_steal_pointer (&task));
}

static gboolean
gs_plugin_akmods_shutdown_finish (GsPlugin      *plugin,
                                  GAsyncResult  *result,
                                  GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

/* Run in @worker. */
static void
gs_akmods_refine_thread_cb (GTask        *task,
			    gpointer      source_object,
			    gpointer      task_data,
			    GCancellable *cancellable)
{
	GsPluginAkmods *self = GS_PLUGIN_AKMODS (source_object);
	GsPluginRefineData *data = task_data;
	GsAppList *list = data->list;
	GsAkmodsState akmods_state = GS_AKMODS_STATE_ERROR;
	g_autoptr(GsApp) notify_for_app = NULL;
	gboolean state_known = FALSE;

	assert_in_worker (self);

	/* nothing to do when SecureBoot is disabled */
	if (gs_akmods_get_secureboot_state_sync () != GS_SECUREBOOT_STATE_ENABLED) {
		g_task_return_boolean (task, TRUE);
		return;
	}

	for (guint i = 0; i < gs_app_list_length (list); i++) {
		GsApp *app = gs_app_list_index (list, i);
		if (g_strcmp0 (gs_app_get_metadata_item (app, "GnomeSoftware::akmods"), "True") != 0)
			continue;
		if (!gs_app_is_installed (app) &&
		    gs_app_get_state (app) != GS_APP_STATE_PENDING_INSTALL)
			continue;
		if (!state_known) {
			g_autoptr(GError) local_error = NULL;

			state_known = TRUE;
			akmods_state = gs_akmods_get_key_state_sync (cancellable, &local_error);
			if (akmods_state == GS_AKMODS_STATE_ERROR) {
				g_debug ("Failed to get key state: %s", local_error->message);
				break;
			}
		}
		if (akmods_state == GS_AKMODS_STATE_ENROLLED) {
			gs_app_remove_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
		} else {
			/* only restart is missing, thus do not bother the user with the MOK password */
			if (akmods_state == GS_AKMODS_STATE_PENDING)
				gs_app_set_metadata (app, "GnomeSoftware::akmods-pending", "True");
			else
				gs_app_set_metadata (app, "GnomeSoftware::akmods-pending", NULL);
			gs_app_add_quirk (app, GS_APP_QUIRK_NEEDS_REBOOT);
			gs_app_set_state (app, GS_APP_STATE_PENDING_INSTALL);

			if (notify_for_app == NULL && !self->did_notify)
				notify_for_app = g_object_ref (app);
		}
	}

	if (notify_for_app != NULL) {
		GApplication *application;

		self->did_notify = TRUE;

		application = g_application_get_default ();

		if (G_IS_APPLICATION (application)) {
			g_autoptr(GNotification) notif = NULL;
			g_autofree gchar *summary = NULL;
			g_autofree gchar *body = NULL;

			summary = g_strdup_printf (_("%s Ready"), gs_app_get_name (notify_for_app));
			body = g_strdup_printf (_("The %s is ready to be enabled and staged for the next boot."), gs_app_get_name (notify_for_app));
			notif = g_notification_new (summary);
			g_notification_set_body (notif, body);
			g_notification_set_default_action_and_target (notif, "app.details", "(ss)",
								      gs_app_get_unique_id (notify_for_app), "");
			g_notification_add_button_with_target (notif, _("Enable"), "app.details", "(ss)",
							       gs_app_get_unique_id (notify_for_app), "");
			g_application_send_notification (G_APPLICATION (application), "akmods-key-pending", notif);
		}
	}

	g_task_return_boolean (task, TRUE);
}

static void
gs_plugin_akmods_refine_async (GsPlugin            *plugin,
                               GsAppList           *list,
                               GsPluginRefineFlags  flags,
                               GCancellable        *cancellable,
                               GAsyncReadyCallback  callback,
                               gpointer             user_data)
{
	GsPluginAkmods *self = GS_PLUGIN_AKMODS (plugin);
	g_autoptr(GTask) task = NULL;

	task = gs_plugin_refine_data_new_task (plugin, list, flags, cancellable, callback, user_data);
	g_task_set_source_tag (task, gs_plugin_akmods_refine_async);

	gs_worker_thread_queue (self->worker, G_PRIORITY_DEFAULT,
				gs_akmods_refine_thread_cb, g_steal_pointer (&task));
}

static gboolean
gs_plugin_akmods_refine_finish (GsPlugin      *plugin,
				GAsyncResult  *result,
				GError       **error)
{
	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
gs_plugin_akmods_dispose (GObject *object)
{
	GsPluginAkmods *self = GS_PLUGIN_AKMODS (object);

	g_clear_object (&self->worker);

	G_OBJECT_CLASS (gs_plugin_akmods_parent_class)->dispose (object);
}

static void
gs_plugin_akmods_init (GsPluginAkmods *self)
{
	GsPlugin *plugin = GS_PLUGIN (self);

	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "packagekit");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "rpm-ostree");
	gs_plugin_add_rule (plugin, GS_PLUGIN_RULE_RUN_AFTER, "flatpak");
}

static void
gs_plugin_akmods_class_init (GsPluginAkmodsClass *klass)
{
	GObjectClass *object_class;
	GsPluginClass *plugin_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->dispose = gs_plugin_akmods_dispose;

	plugin_class = GS_PLUGIN_CLASS (klass);
	plugin_class->reload = gs_plugin_akmods_reload;
	plugin_class->setup_async = gs_plugin_akmods_setup_async;
	plugin_class->setup_finish = gs_plugin_akmods_setup_finish;
	plugin_class->shutdown_async = gs_plugin_akmods_shutdown_async;
	plugin_class->shutdown_finish = gs_plugin_akmods_shutdown_finish;
	plugin_class->refine_async = gs_plugin_akmods_refine_async;
	plugin_class->refine_finish = gs_plugin_akmods_refine_finish;
}

GType
gs_plugin_query_type (void)
{
	return GS_TYPE_PLUGIN_AKMODS;
}
