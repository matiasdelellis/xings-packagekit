/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2007 Richard Hughes <richard@hughsie.com>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n.h>

#include <glade/glade.h>
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>

#include <pk-debug.h>
#include <pk-client.h>
#include <pk-control.h>
#include <pk-connection.h>
#include <pk-package-id.h>
#include <pk-common.h>
#include <pk-enum-list.h>

#include "gpk-common.h"
#include "gpk-progress.h"

static void     gpk_progress_class_init (GpkProgressClass *klass);
static void     gpk_progress_init       (GpkProgress      *progress);
static void     gpk_progress_finalize   (GObject	    *object);

#define GPK_PROGRESS_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_PROGRESS, GpkProgressPrivate))

struct GpkProgressPrivate
{
	GladeXML		*glade_xml;
	PkClient		*client;
	gboolean		 task_ended;
	guint			 no_percentage_evt;
	guint			 no_subpercentage_evt;
};

enum {
	ACTION_UNREF,
	ACTION_CLOSE,
	LAST_SIGNAL
};

static guint	     signals [LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GpkProgress, gpk_progress, G_TYPE_OBJECT)

/**
 * gpk_progress_class_init:
 * @klass: This graph class instance
 **/
static void
gpk_progress_class_init (GpkProgressClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_progress_finalize;
	g_type_class_add_private (klass, sizeof (GpkProgressPrivate));

	signals [ACTION_UNREF] =
		g_signal_new ("action-unref",
			      G_TYPE_FROM_CLASS (object_class),
			      G_SIGNAL_RUN_LAST,
			      0, NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
}

/**
 * gpk_progress_error_code_cb:
 **/
static void
gpk_progress_error_message (GpkProgress *progress, const gchar *title, const gchar *details)
{
	GtkWidget *main_window;
	GtkWidget *dialog;
	gchar *escaped_details = NULL;

	pk_warning ("error %s:%s", title, details);
	main_window = glade_xml_get_widget (progress->priv->glade_xml, "window_progress");

	dialog = gtk_message_dialog_new (GTK_WINDOW (main_window), GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", title);

	if (details != NULL) {
		escaped_details = g_markup_escape_text (details, -1);
		gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", escaped_details);
		g_free (escaped_details);
	}

	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

/**
 * gpk_progress_clean_exit:
 **/
static void
gpk_progress_clean_exit (GpkProgress *progress)
{
	/* remove the back and forth progress bar update */
	if (progress->priv->no_percentage_evt != 0) {
		g_source_remove (progress->priv->no_percentage_evt);
		progress->priv->no_percentage_evt = 0;
	}
	if (progress->priv->no_subpercentage_evt != 0) {
		g_source_remove (progress->priv->no_subpercentage_evt);
		progress->priv->no_subpercentage_evt = 0;
	}
	g_signal_emit (progress, signals [ACTION_UNREF], 0);
}

/**
 * gpk_progress_help_cb:
 **/
static void
gpk_progress_help_cb (GtkWidget  *widget,
		     GpkProgress *progress)
{
	pk_debug ("emitting help");
	gpk_progress_clean_exit (progress);
}

/**
 * gpk_progress_cancel_cb:
 **/
static void
gpk_progress_cancel_cb (GtkWidget  *widget,
		       GpkProgress *progress)
{
	gboolean ret;
	GError *error = NULL;

	pk_debug ("emitting cancel");
	ret = pk_client_cancel (progress->priv->client, &error);
	if (!ret) {
		pk_warning ("failed to cancel client: %s", error->message);
		g_error_free (error);
	}
}

/**
 * gpk_progress_hide_cb:
 **/
static void
gpk_progress_hide_cb (GtkWidget   *widget,
		     GpkProgress  *progress)
{
	pk_debug ("hide");
	gpk_progress_clean_exit (progress);
}

/**
 * gpk_progress_error_code_cb:
 **/
static void
gpk_progress_error_code_cb (PkClient *client, PkErrorCodeEnum code, const gchar *details, GpkProgress *progress)
{
	/* ignore some errors */
	if (code == PK_ERROR_ENUM_TRANSACTION_CANCELLED ||
	    code == PK_ERROR_ENUM_PROCESS_KILL) {
		pk_debug ("ignoring cancel error");
		return;
	}
	gpk_progress_error_message (progress, gpk_error_enum_to_localised_text (code), details);
}

/**
 * gpk_progress_finished_timeout:
 **/
static gboolean
gpk_progress_finished_timeout (gpointer data)
{
	GpkProgress *progress = GPK_PROGRESS (data);
	pk_debug ("emit unref");
	gpk_progress_clean_exit (progress);
	return FALSE;
}

/**
 * gpk_progress_finished_cb:
 **/
static void
gpk_progress_finished_cb (PkClient *client, PkExitEnum exit, guint runtime, GpkProgress *progress)
{
	GtkWidget *widget;
	pk_debug ("finished");

	/* we were cancelled */
	if (exit == PK_EXIT_ENUM_CANCELLED) {
		widget = glade_xml_get_widget (progress->priv->glade_xml, "label_status");
		gtk_label_set_label (GTK_LABEL (widget), _("The transaction was cancelled"));
		gtk_widget_show (widget);
		widget = glade_xml_get_widget (progress->priv->glade_xml, "label_package");
		gtk_widget_hide (widget);
	}

	/* we failed */
	if (exit == PK_EXIT_ENUM_FAILED) {
		widget = glade_xml_get_widget (progress->priv->glade_xml, "label_status");
		gtk_label_set_label (GTK_LABEL (widget), _("The transaction failed"));
		gtk_widget_show (widget);
		widget = glade_xml_get_widget (progress->priv->glade_xml, "label_package");
		gtk_widget_hide (widget);
	}

	progress->priv->task_ended = TRUE;
	/* wait 5 seconds */
	progress->priv->no_percentage_evt = g_timeout_add_seconds (30, gpk_progress_finished_timeout, progress);
}

/**
 * gpk_progress_package_cb:
 */
static void
gpk_progress_package_cb (PkClient *client, PkInfoEnum info, const gchar *package_id,
			const gchar *summary, GpkProgress *progress)
{
	GtkWidget *widget;
	PkPackageId *ident;

	widget = glade_xml_get_widget (progress->priv->glade_xml, "label_package");

	/* just use the package name, not the description */
	ident = pk_package_id_new_from_string (package_id);
	gtk_label_set_label (GTK_LABEL (widget), ident->name);
	pk_package_id_free (ident);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_status");
	gtk_widget_show (widget);
}

/**
 * gpk_progress_spin_timeout_percentage:
 **/
static gboolean
gpk_progress_spin_timeout_percentage (gpointer data)
{
	GtkWidget *widget;
	GpkProgress *progress = GPK_PROGRESS (data);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "progressbar_percentage");
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (widget));

	/* show the box */
	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_percentage");
	gtk_widget_show (widget);

	if (progress->priv->task_ended) {
		/* hide the box */
		widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_percentage");
		gtk_widget_hide (widget);
		return FALSE;
	}
	return TRUE;
}

/**
 * gpk_progress_spin_timeout_subpercentage:
 **/
static gboolean
gpk_progress_spin_timeout_subpercentage (gpointer data)
{
	GtkWidget *widget;
	GpkProgress *progress = GPK_PROGRESS (data);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "progressbar_subpercentage");
	gtk_progress_bar_pulse (GTK_PROGRESS_BAR (widget));

	/* show the box */
	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_subpercentage");
	gtk_widget_show (widget);

	if (progress->priv->task_ended) {
		/* hide the box */
		widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_subpercentage");
		gtk_widget_hide (widget);
		return FALSE;
	}
	return TRUE;
}

/**
 * gpk_progress_action_percentage:
 **/
static void
gpk_progress_action_percentage (GpkProgress *progress, guint percentage)
{
	GtkWidget *widget_hbox;
	GtkWidget *widget_percentage;

	widget_hbox = glade_xml_get_widget (progress->priv->glade_xml, "hbox_percentage");
	widget_percentage = glade_xml_get_widget (progress->priv->glade_xml, "progressbar_percentage");

	/* hide */
	if (percentage == 0) {
		/* we've gone from unknown -> hidden - cancel the polling */
		if (progress->priv->no_percentage_evt != 0) {
			g_source_remove (progress->priv->no_percentage_evt);
			progress->priv->no_percentage_evt = 0;
		}
		gtk_widget_hide (widget_hbox);
		return;
	}

	/* spin */
	if (percentage == PK_CLIENT_PERCENTAGE_INVALID) {
		/* We have to spin */
		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (widget_percentage), NULL);
		/* don't queue duplicate events */
		if (progress->priv->no_percentage_evt == 0) {
			progress->priv->no_percentage_evt = g_timeout_add (GPK_PROGRESS_BAR_PULSE_DELAY,
									   gpk_progress_spin_timeout_percentage, progress);
		}
		return;
	}

	gtk_widget_show (widget_hbox);
	gtk_widget_show (widget_percentage);

	/* we've gone from unknown -> actual value - cancel the polling */
	if (progress->priv->no_percentage_evt != 0) {
		g_source_remove (progress->priv->no_percentage_evt);
		progress->priv->no_percentage_evt = 0;
	}

	/* just set the value */
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget_percentage), (gfloat) percentage / 100.0);
}

/**
 * gpk_progress_action_subpercentage:
 **/
static void
gpk_progress_action_subpercentage (GpkProgress *progress, guint percentage)
{
	GtkWidget *widget_hbox;
	GtkWidget *widget_percentage;

	widget_hbox = glade_xml_get_widget (progress->priv->glade_xml, "hbox_subpercentage");
	widget_percentage = glade_xml_get_widget (progress->priv->glade_xml, "progressbar_subpercentage");

	/* hide */
	if (percentage == 0) {
		/* we've gone from unknown -> hidden - cancel the polling */
		if (progress->priv->no_subpercentage_evt != 0) {
			g_source_remove (progress->priv->no_subpercentage_evt);
			progress->priv->no_subpercentage_evt = 0;
		}
		return;
	}

	gtk_widget_show (widget_hbox);
	gtk_widget_show (widget_percentage);

	/* spin */
	if (percentage == PK_CLIENT_PERCENTAGE_INVALID) {
		/* We have to spin */
		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (widget_percentage), NULL);
		/* don't queue duplicate events */
		if (progress->priv->no_subpercentage_evt == 0) {
			progress->priv->no_subpercentage_evt = g_timeout_add (GPK_PROGRESS_BAR_PULSE_DELAY,
									      gpk_progress_spin_timeout_subpercentage, progress);
		}
		return;
	}

	/* we've gone from unknown -> actual value - cancel the polling */
	if (progress->priv->no_subpercentage_evt != 0) {
		g_source_remove (progress->priv->no_subpercentage_evt);
		progress->priv->no_subpercentage_evt = 0;
	}

	/* just set the value */
	gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (widget_percentage), (gfloat) percentage / 100.0);
}

/**
 * gpk_progress_progress_changed_cb:
 **/
static void
gpk_progress_progress_changed_cb (PkClient *client, guint percentage, guint subpercentage,
				 guint elapsed, guint remaining, GpkProgress *progress)
{
	GtkWidget *widget;
	gchar *time;

	gpk_progress_action_percentage (progress, percentage);
	gpk_progress_action_subpercentage (progress, subpercentage);

	/* set some localised text if we have time */
	widget = glade_xml_get_widget (progress->priv->glade_xml, "progressbar_percentage");
	if (remaining == 0) {
		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (widget), NULL);
	} else {
		time = gpk_time_to_localised_string (remaining);
		gtk_progress_bar_set_text (GTK_PROGRESS_BAR (widget), time);
		g_free (time);
	}
}

/**
 * gpk_progress_allow_cancel_cb:
 **/
static void
gpk_progress_allow_cancel_cb (PkClient *client, gboolean allow_cancel, GpkProgress *progress)
{
	GtkWidget *widget;
	widget = glade_xml_get_widget (progress->priv->glade_xml, "button_cancel");
	gtk_widget_set_sensitive (widget, allow_cancel);
}

/**
 * gpk_progress_status_changed_cb:
 */
static void
gpk_progress_status_changed_cb (PkClient *client, PkStatusEnum status, GpkProgress *progress)
{
	GtkWidget *widget;
	const gchar *icon_name;

	widget = glade_xml_get_widget (progress->priv->glade_xml, "label_status");
	gtk_label_set_label (GTK_LABEL (widget), gpk_status_enum_to_localised_text (status));

	widget = glade_xml_get_widget (progress->priv->glade_xml, "image_status");
	gtk_widget_show (widget);
	icon_name = gpk_status_enum_to_icon_name (status);
	gtk_image_set_from_icon_name (GTK_IMAGE (widget), icon_name, GTK_ICON_SIZE_DIALOG);
	pk_debug ("setting icon %s\n", icon_name);
}

/**
 * gpk_progress_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
gpk_progress_delete_event_cb (GtkWidget	*widget,
			     GdkEvent	*event,
			     GpkProgress	*progress)
{
	gpk_progress_clean_exit (progress);
	return FALSE;
}

/**
 * pk_common_get_role_text:
 **/
static gchar *
pk_common_get_role_text (PkClient *client)
{
	const gchar *role_text;
	gchar *package_id;
	gchar *text;
	gchar *package;
	PkRoleEnum role;
	GError *error = NULL;
	gboolean ret;

	/* get role and text */
	ret = pk_client_get_role (client, &role, &package_id, &error);
	if (!ret) {
		pk_warning ("failed to get role: %s", error->message);
		g_error_free (error);
		return NULL;
	}

	/* backup */
	role_text = gpk_role_enum_to_localised_present (role);

	if (!pk_strzero (package_id) && role != PK_ROLE_ENUM_UPDATE_PACKAGES) {
		package = gpk_package_get_name (package_id);
		text = g_strdup_printf ("%s: %s", role_text, package);
		g_free (package);
	} else {
		text = g_strdup_printf ("%s", role_text);
	}
	g_free (package_id);

	return text;
}

/**
 * gpk_progress_monitor_tid:
 **/
gboolean
gpk_progress_monitor_tid (GpkProgress *progress, const gchar *tid)
{
	GtkWidget *widget;
	PkStatusEnum status;
	gboolean ret;
	gboolean allow_cancel;
	gchar *text;
	guint percentage;
	guint subpercentage;
	guint elapsed;
	guint remaining;
	GError *error = NULL;

	g_return_val_if_fail (PK_IS_PROGRESS (progress), FALSE);

	ret = pk_client_set_tid (progress->priv->client, tid, &error);
	if (!ret) {
		pk_warning ("could not set tid: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	/* fill in role */
	text = pk_common_get_role_text (progress->priv->client);
	widget = glade_xml_get_widget (progress->priv->glade_xml, "window_progress");
	gtk_window_set_title (GTK_WINDOW (widget), text);
	g_free (text);

	/* coldplug */
	ret = pk_client_get_status (progress->priv->client, &status, NULL);
	/* no such transaction? */
	if (ret == FALSE) {
		g_signal_emit (progress, signals [ACTION_UNREF], 0);
		return FALSE;
	}

	/* are we cancellable? */
	pk_client_get_allow_cancel (progress->priv->client, &allow_cancel, NULL);
	widget = glade_xml_get_widget (progress->priv->glade_xml, "button_cancel");
	gtk_widget_set_sensitive (widget, allow_cancel);

	gpk_progress_status_changed_cb (progress->priv->client, status, progress);

	/* coldplug */
	ret = pk_client_get_progress (progress->priv->client, &percentage, &subpercentage, &elapsed, &remaining, NULL);
	if (ret) {
		gpk_progress_progress_changed_cb (progress->priv->client, percentage,
						 subpercentage, elapsed, remaining, progress);
	} else {
		pk_warning ("GetProgress failed");
		gpk_progress_progress_changed_cb (progress->priv->client,
						 PK_CLIENT_PERCENTAGE_INVALID,
						 PK_CLIENT_PERCENTAGE_INVALID, 0, 0, progress);
	}

	/* do the best we can */
	ret = pk_client_get_package (progress->priv->client, &text, NULL);
	if (ret) {
		gpk_progress_package_cb (progress->priv->client, 0, text, NULL, progress);
	}

	widget = glade_xml_get_widget (progress->priv->glade_xml, "window_progress");
	gtk_widget_show (widget);
	return TRUE;
}

/**
 * gpk_progress_init:
 **/
static void
gpk_progress_init (GpkProgress *progress)
{
	GtkWidget *main_window;
	GtkWidget *widget;
	PkEnumList *role_list;
	PkControl *control;

	progress->priv = GPK_PROGRESS_GET_PRIVATE (progress);
	progress->priv->task_ended = FALSE;
	progress->priv->no_percentage_evt = 0;
	progress->priv->no_subpercentage_evt = 0;

	progress->priv->client = pk_client_new ();
	g_signal_connect (progress->priv->client, "error-code",
			  G_CALLBACK (gpk_progress_error_code_cb), progress);
	g_signal_connect (progress->priv->client, "finished",
			  G_CALLBACK (gpk_progress_finished_cb), progress);
	g_signal_connect (progress->priv->client, "package",
			  G_CALLBACK (gpk_progress_package_cb), progress);
	g_signal_connect (progress->priv->client, "progress-changed",
			  G_CALLBACK (gpk_progress_progress_changed_cb), progress);
	g_signal_connect (progress->priv->client, "status-changed",
			  G_CALLBACK (gpk_progress_status_changed_cb), progress);
	g_signal_connect (progress->priv->client, "allow-cancel",
			  G_CALLBACK (gpk_progress_allow_cancel_cb), progress);

	progress->priv->glade_xml = glade_xml_new (PK_DATA "/gpk-progress.glade", NULL, NULL);
	main_window = glade_xml_get_widget (progress->priv->glade_xml, "window_progress");

	widget = glade_xml_get_widget (progress->priv->glade_xml, "progressbar_percentage");
	gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR (widget), 0.025);

	/* Hide window first so that the dialogue resizes itself without redrawing */
	gtk_widget_hide (main_window);
	gtk_window_set_icon_name (GTK_WINDOW (main_window), "system-software-installer");

	/* hide icon until we have a status */
	widget = glade_xml_get_widget (progress->priv->glade_xml, "image_status");
	gtk_widget_hide (widget);

	/* Get the main window quit */
	g_signal_connect (main_window, "delete_event",
			  G_CALLBACK (gpk_progress_delete_event_cb), progress);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_percentage");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_subpercentage");
	gtk_widget_hide (widget);
	widget = glade_xml_get_widget (progress->priv->glade_xml, "hbox_status");
	gtk_widget_hide (widget);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "button_help");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_progress_help_cb), progress);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "button_cancel");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_progress_cancel_cb), progress);

	/* get actions */
	control = pk_control_new ();
	role_list = pk_control_get_actions (control);
	g_object_unref (control);

	/* can we ever do the action? */
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_CANCEL) == FALSE) {
		gtk_widget_hide (widget);
	}

	g_object_unref (role_list);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "button_hide");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (gpk_progress_hide_cb), progress);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "button_help");
	gtk_widget_set_sensitive (widget, FALSE);
}

/**
 * gpk_progress_finalize:
 * @object: This graph class instance
 **/
static void
gpk_progress_finalize (GObject *object)
{
	GpkProgress *progress;
	GtkWidget *widget;

	g_return_if_fail (PK_IS_PROGRESS (object));

	progress = GPK_PROGRESS (object);
	progress->priv = GPK_PROGRESS_GET_PRIVATE (progress);

	widget = glade_xml_get_widget (progress->priv->glade_xml, "window_progress");
	gtk_widget_hide (widget);

	g_object_unref (progress->priv->client);
	g_object_unref (progress->priv->glade_xml);

	G_OBJECT_CLASS (gpk_progress_parent_class)->finalize (object);
}

/**
 * gpk_progress_new:
 * Return value: new GpkProgress instance.
 **/
GpkProgress *
gpk_progress_new (void)
{
	GpkProgress *progress;
	progress = g_object_new (GPK_TYPE_PROGRESS, NULL);
	return GPK_PROGRESS (progress);
}

