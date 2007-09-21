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

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <dbus/dbus-glib.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <pk-enum-list.h>
#include <pk-task-client.h>

#include <pk-debug.h>

static GladeXML	*glade_xml;

/**
 * pk_updates_close_cb:
 **/
static void
pk_updates_close_cb (GtkWidget *widget, gpointer data)
{
	GMainLoop *loop = (GMainLoop *) data;
	pk_debug ("emitting action-close");
	g_main_loop_quit (loop);
}

/**
 * pk_updates_delete_event_cb:
 * @event: The event type, unused.
 **/
static gboolean
pk_updates_delete_event_cb (GtkWidget *widget, GdkEvent *event, gpointer data)
{
	pk_updates_close_cb (widget, data);
	return FALSE;
}

/**
 * main:
 **/
int
main (int argc, char *argv[])
{
	GMainLoop *loop;
	gboolean verbose = FALSE;
	GOptionContext *context;
	GtkWidget *widget;

	const GOptionEntry options[] = {
		{ "verbose", '\0', 0, G_OPTION_ARG_NONE, &verbose,
		  "Show extra debugging information", NULL },
		{ NULL}
	};

	if (! g_thread_supported ()) {
		g_thread_init (NULL);
	}
	dbus_g_thread_init ();
	g_type_init ();

	context = g_option_context_new (_("Software Update Viewer"));
	g_option_context_add_main_entries (context, options, NULL);
	g_option_context_parse (context, &argc, &argv, NULL);
	g_option_context_free (context);
	pk_debug_init (verbose);
	gtk_init (&argc, &argv);

	loop = g_main_loop_new (NULL, FALSE);

	glade_xml = glade_xml_new (PK_DATA "/pk-backend-status.glade", NULL, NULL);

	widget = glade_xml_get_widget (glade_xml, "button_close");
	g_signal_connect (widget, "clicked",
			  G_CALLBACK (pk_updates_close_cb), loop);

	widget = glade_xml_get_widget (glade_xml, "window_backend");
	g_signal_connect (widget, "delete_event",
			  G_CALLBACK (pk_updates_delete_event_cb), loop);
	gtk_widget_show (GTK_WIDGET (widget));

	PkEnumList *role_list;
	PkEnumList *filter_list;
	PkTaskClient *tclient;
	tclient = pk_task_client_new ();
	role_list = pk_task_client_get_actions (tclient);
	filter_list = pk_task_client_get_filters (tclient);

	/* actions */
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_CANCEL) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_cancel");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_DEPENDS) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_get_depends");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_UPDATE_DETAIL) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_get_update_detail");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_DESCRIPTION) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_get_description");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_REQUIRES) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_get_requires");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_GET_UPDATES) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_get_updates");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_SEARCH_DETAILS) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_search_details");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_SEARCH_FILE) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_search_file");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_SEARCH_GROUP) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_search_group");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_SEARCH_NAME) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_search_name");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_REFRESH_CACHE) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_refresh_cache");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_PACKAGE_REMOVE) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_package_remove");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_PACKAGE_INSTALL) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_package_install");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_FILE_INSTALL) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_file_install");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_PACKAGE_UPDATE) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_package_update");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (role_list, PK_ROLE_ENUM_SYSTEM_UPDATE) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_system_update");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}

	/* filters */
	if (pk_enum_list_contains (filter_list, PK_FILTER_ENUM_INSTALLED) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_installed");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (filter_list, PK_FILTER_ENUM_DEVELOPMENT) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_devel");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}
	if (pk_enum_list_contains (filter_list, PK_FILTER_ENUM_GUI) == TRUE) {
		widget = glade_xml_get_widget (glade_xml, "image_gui");
		gtk_image_set_from_icon_name (GTK_IMAGE (widget), "gtk-apply", GTK_ICON_SIZE_MENU);
	}

	g_object_unref (tclient);
	g_object_unref (role_list);
	g_object_unref (filter_list);

	g_main_loop_run (loop);
	g_main_loop_unref (loop);

	return 0;
}
