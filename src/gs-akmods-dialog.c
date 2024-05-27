/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 * vi:set noexpandtab tabstop=8 shiftwidth=8:
 *
 * Copyright (C) 2024 Red Hat <www.redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "config.h"

#include <adwaita.h>
#include <glib/gi18n.h>

#include "gs-common.h"
#include "gs-akmods-private.h"

#include "gs-akmods-dialog.h"

struct _GsAkmodsDialog
{
	AdwWindow parent_instance;

	GtkWidget *cancel_button;
	GtkWidget *apply_button;
	GtkEditable *password_entry_row;
	GtkWidget *password_error_image;
	GtkLabel *top_info;
	GtkLabel *bottom_info;

	GsApp *app;
	GCancellable *cancellable;
};

typedef enum {
	PROP_APP = 1,
} GsAkmodsDialogProperty;

static GParamSpec *obj_props[PROP_APP + 1] = { NULL, };

G_DEFINE_TYPE (GsAkmodsDialog, gs_akmods_dialog, ADW_TYPE_WINDOW)

static void
gs_akmods_dialog_cancel_button_clicked_cb (GtkWidget *button,
					   GsAkmodsDialog *self)
{
	if (self->cancellable != NULL)
		g_cancellable_cancel (self->cancellable);

	gtk_window_destroy (GTK_WINDOW (self));
}

static void
gs_akmods_dialog_prepare_reboot_cb (GObject *source_object,
				    GAsyncResult *result,
				    gpointer user_data)
{
	g_autoptr(GError) local_error = NULL;

	if (!g_task_propagate_boolean (G_TASK (result), &local_error)) {
		if (g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
			return;
		g_debug ("akmods-dialog: Failed to prepare reboot: %s", local_error->message);
		/* The code 126 is returned when the admin/root password prompt is dismissed */
		if (!g_error_matches (local_error, G_SPAWN_EXIT_ERROR, 126)) {
			gs_utils_show_error_dialog (GTK_WINDOW (source_object),
						    _("Failed to prepare reboot"),
						    "",
						    local_error->message);
		}
	} else {
		gs_utils_invoke_reboot_async (NULL, NULL, NULL);
		gtk_window_destroy (GTK_WINDOW (source_object));
	}
}

static void
gs_akmods_dialog_prepare_reboot_thread (GTask *task,
					gpointer source_object,
					gpointer task_data,
					GCancellable *cancellable)
{
	g_autoptr(GError) local_error = NULL;
	const GString *password = task_data;
	GsAkmodsState state;

	state = gs_akmods_enroll_sync (password, cancellable, &local_error);

	if (state == GS_AKMODS_STATE_ERROR)
		g_task_return_error (task, g_steal_pointer (&local_error));
	else
		g_task_return_boolean (task, TRUE);
}

static void
gs_akmods_dialog_free_password (gpointer ptr)
{
	GString *password = ptr;
	if (password != NULL) {
		for (guint i = 0; i < password->len; i++) {
			password->str[i] = '\0';
		}
		g_string_free (password, TRUE);
	}
}

static void
gs_akmods_dialog_apply_button_clicked_cb (GtkWidget *button,
					  GsAkmodsDialog *self)
{
	g_autoptr(GTask) task = NULL;
	g_autoptr(GString) password = g_string_new (gtk_editable_get_text (self->password_entry_row));

	if (self->cancellable != NULL) {
		g_cancellable_cancel (self->cancellable);
		g_clear_object (&self->cancellable);
	}

	self->cancellable = g_cancellable_new ();

	task = g_task_new (self, self->cancellable, gs_akmods_dialog_prepare_reboot_cb, NULL);
	g_task_set_source_tag (task, gs_akmods_dialog_apply_button_clicked_cb);
	g_task_set_task_data (task, g_steal_pointer (&password), gs_akmods_dialog_free_password);
	g_task_run_in_thread (task, gs_akmods_dialog_prepare_reboot_thread);
}

static void
gs_akmods_dialog_password_changed_cb (GtkEditable *editable,
				      GsAkmodsDialog *self)
{
	const gchar *text = gtk_editable_get_text (editable);
	guint n_letters;
	gboolean correct = TRUE;

	for (n_letters = 0; text != NULL && text[n_letters] != '\0'; n_letters++) {
		const gchar chr = text[n_letters];
		correct = correct && (
			(chr >= 'a' && chr <= 'z') ||
			(chr >= 'A' && chr <= 'Z') ||
			(chr >= '0' && chr <= '9'));
	}

	gtk_widget_set_visible (self->password_error_image, !correct);
	gtk_widget_set_sensitive (self->apply_button, correct && n_letters > 0);
}

static void
gs_akmods_dialog_get_property (GObject *object,
			       guint prop_id,
			       GValue *value,
			       GParamSpec *pspec)
{
	GsAkmodsDialog *self = GS_AKMODS_DIALOG (object);

	switch ((GsAkmodsDialogProperty) prop_id) {
	case PROP_APP:
		g_value_set_object (value, self->app);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_akmods_dialog_set_property (GObject *object,
			       guint prop_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	GsAkmodsDialog *self = GS_AKMODS_DIALOG (object);

	switch ((GsAkmodsDialogProperty) prop_id) {
	case PROP_APP:
		g_assert (self->app == NULL);
		self->app = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
gs_akmods_dialog_constructed (GObject *object)
{
	GsAkmodsDialog *self = GS_AKMODS_DIALOG (object);
	g_autofree gchar *text = NULL;

	G_OBJECT_CLASS (gs_akmods_dialog_parent_class)->constructed (object);

	/* Translators: the '%s' is replaced with the driver name */
	text = g_markup_printf_escaped (_("Your machine is configured to use <b>Secure Boot</b> preventing unknown drivers to"
					  " be installed. Please provide a password for a newly generated machine owner key to"
					  " be installed to authenticate <b>%s</b> and future custom drivers."),
					gs_app_get_name (self->app));
	gtk_label_set_markup (self->top_info, text);
	g_clear_pointer (&text, g_free);

	/* Translators: the '%s' is replaced with actual URL where more information can be found */
	text = g_markup_printf_escaped (_("Please make a note of the single use password provided above; you"
					  " will need it once the system reboots. Use only upper case letters,"
					  " lower case letters, and numbers."
					  " <a href=\"%s\" title=\"Learn more\">Learn more.</a>"),
					"https://docs.fedoraproject.org/workstation-docs/nvidia-install/");
	gtk_label_set_markup (self->bottom_info, text);

	self->password_error_image = g_object_ref_sink (gtk_image_new_from_icon_name ("dialog-warning-symbolic"));
	gtk_widget_set_tooltip_text (self->password_error_image, _("Use only uppercase, lowercase letters and numbers."));
	adw_entry_row_add_suffix (ADW_ENTRY_ROW (self->password_entry_row), self->password_error_image);

	/* to initialize sensitivity of the apply button */
	gs_akmods_dialog_password_changed_cb (self->password_entry_row, self);
}

static void
gs_akmods_dialog_dispose (GObject *object)
{
	GsAkmodsDialog *self = GS_AKMODS_DIALOG (object);

	if (self->cancellable) {
		g_cancellable_cancel (self->cancellable);
		g_clear_object (&self->cancellable);
	}

	g_clear_object (&self->app);
	g_clear_object (&self->password_error_image);

	G_OBJECT_CLASS (gs_akmods_dialog_parent_class)->dispose (object);
}

static void
gs_akmods_dialog_class_init (GsAkmodsDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	object_class->get_property = gs_akmods_dialog_get_property;
	object_class->set_property = gs_akmods_dialog_set_property;
	object_class->constructed = gs_akmods_dialog_constructed;
	object_class->dispose = gs_akmods_dialog_dispose;

	/*
	 * GsAkmodsDialog:app: (nullable)
	 *
	 * The app to display the dialog for.
	 *
	 * This may be %NULL; if so, the content of the widget will be
	 * undefined.
	 *
	 * Since: 47
	 */
	obj_props[PROP_APP] =
		g_param_spec_object ("app", NULL, NULL,
				     GS_TYPE_APP,
				     G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, G_N_ELEMENTS (obj_props), obj_props);

	gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/Software/gs-akmods-dialog.ui");

	gtk_widget_class_bind_template_child (widget_class, GsAkmodsDialog, cancel_button);
	gtk_widget_class_bind_template_child (widget_class, GsAkmodsDialog, apply_button);
	gtk_widget_class_bind_template_child (widget_class, GsAkmodsDialog, password_entry_row);
	gtk_widget_class_bind_template_child (widget_class, GsAkmodsDialog, top_info);
	gtk_widget_class_bind_template_child (widget_class, GsAkmodsDialog, bottom_info);
	gtk_widget_class_bind_template_callback (widget_class, gs_akmods_dialog_cancel_button_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_akmods_dialog_apply_button_clicked_cb);
	gtk_widget_class_bind_template_callback (widget_class, gs_akmods_dialog_password_changed_cb);

	gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Escape, 0, "window.close", NULL);
}

static void
gs_akmods_dialog_init (GsAkmodsDialog *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));

	/* hide leftover notification, if any */
	g_application_withdraw_notification (g_application_get_default (), "akmods-key-pending");
}

void
gs_akmods_dialog_run (GtkWindow *parent,
		      GsShell *shell,
		      GsApp *app)
{
	GsAkmodsDialog *self;

	self = g_object_new (GS_TYPE_AKMODS_DIALOG,
			     "modal", TRUE,
			     "transient-for", parent,
			     "app", app,
			     NULL);
	gs_shell_modal_dialog_present (shell, GTK_WINDOW (self));
}
