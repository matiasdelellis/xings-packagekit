/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2009 Richard Hughes <richard@hughsie.com>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gconf/gconf-client.h>

#include "gpk-helper-deps-update.h"
#include "gpk-marshal.h"
#include "gpk-gnome.h"
#include "gpk-common.h"
#include "gpk-dialog.h"

#include "egg-debug.h"

static void     gpk_helper_deps_update_finalize	(GObject	  *object);

#define GPK_HELPER_DEPS_UPDATE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), GPK_TYPE_HELPER_DEPS_UPDATE, GpkHelperDepsUpdatePrivate))

struct GpkHelperDepsUpdatePrivate
{
	GtkWindow		*window;
	GConfClient		*gconf_client;
	PkPackageList		*list;
};

enum {
	GPK_HELPER_DEPS_UPDATE_EVENT,
	GPK_HELPER_DEPS_UPDATE_LAST_SIGNAL
};

static guint signals [GPK_HELPER_DEPS_UPDATE_LAST_SIGNAL] = { 0 };
G_DEFINE_TYPE (GpkHelperDepsUpdate, gpk_helper_deps_update, G_TYPE_OBJECT)

/**
 * gpk_helper_deps_update_show:
 *
 * Return value: if we agreed
 **/
gboolean
gpk_helper_deps_update_show (GpkHelperDepsUpdate *helper, PkPackageList *deps_list)
{
	gchar *title = NULL;
	const gchar *message = NULL;
	guint length;
	gboolean ret;
	GtkWidget *dialog;
	GtkResponseType response;
	const PkPackageObj *obj;
	guint i;

	/* save deps list */
	if (helper->priv->list != NULL)
		g_object_unref (helper->priv->list);
	helper->priv->list = pk_package_list_new ();;

	/* copy only installing, updating etc */
	length = pk_package_list_get_size (deps_list);
	for (i=0; i<length; i++) {
		obj = pk_package_list_get_obj (deps_list, i);
		if (obj->info == PK_INFO_ENUM_INSTALLING ||
		    obj->info == PK_INFO_ENUM_UPDATING)
			pk_package_list_add (helper->priv->list, obj->info, obj->id, obj->summary);
	}

	/* empty list */
	length = pk_package_list_get_size (helper->priv->list);
	if (length == 0) {
		g_signal_emit (helper, signals [GPK_HELPER_DEPS_UPDATE_EVENT], 0, GTK_RESPONSE_YES, helper->priv->list);
		goto out;
	}

	/* have we previously said we don't want to be shown the confirmation */
	ret = gconf_client_get_bool (helper->priv->gconf_client, GPK_CONF_SHOW_DEPENDS, NULL);
	if (!ret) {
		egg_debug ("we've said we don't want the dep dialog");
		g_signal_emit (helper, signals [GPK_HELPER_DEPS_UPDATE_EVENT], 0, GTK_RESPONSE_YES, helper->priv->list);
		goto out;
	}

	/* TRANSLATORS: title: tell the user we have to install additional updates */
	title = g_strdup_printf (ngettext ("%i additional update also has to be installed",
					   "%i additional update also have to be installed",
					   length), length);

	/* TRANSLATORS: message: describe in detail why it must happen */
	message = ngettext ("To perform this update, an additional package also has to be downloaded.",
			    "To perform this update, additional packages also have to be downloaded.",
			    length);

	dialog = gtk_message_dialog_new (helper->priv->window, GTK_DIALOG_DESTROY_WITH_PARENT,
					 GTK_MESSAGE_INFO, GTK_BUTTONS_CANCEL, "%s", title);
	gtk_message_dialog_format_secondary_markup (GTK_MESSAGE_DIALOG (dialog), "%s", message);
	gpk_dialog_embed_package_list_widget (GTK_DIALOG (dialog), helper->priv->list);
	gpk_dialog_embed_do_not_show_widget (GTK_DIALOG (dialog), GPK_CONF_SHOW_DEPENDS);
	/* TRANSLATORS: this is button text */
	gtk_dialog_add_button (GTK_DIALOG (dialog), _("Install"), GTK_RESPONSE_YES);

	/* set icon name */
	gtk_window_set_icon_name (GTK_WINDOW (dialog), GPK_ICON_SOFTWARE_INSTALLER);

	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (GTK_WIDGET (dialog));

	/* yes / no */
	if (response == GTK_RESPONSE_YES) {
		g_signal_emit (helper, signals [GPK_HELPER_DEPS_UPDATE_EVENT], 0, response, helper->priv->list);
	} else {
		g_signal_emit (helper, signals [GPK_HELPER_DEPS_UPDATE_EVENT], 0, GTK_RESPONSE_NO, helper->priv->list);
	}
out:
	g_free (title);
	return TRUE;
}

/**
 * gpk_helper_deps_update_set_parent:
 **/
gboolean
gpk_helper_deps_update_set_parent (GpkHelperDepsUpdate *helper, GtkWindow *window)
{
	g_return_val_if_fail (GPK_IS_HELPER_DEPS_UPDATE (helper), FALSE);
	g_return_val_if_fail (window != NULL, FALSE);

	helper->priv->window = window;
	return TRUE;
}

/**
 * gpk_helper_deps_update_class_init:
 * @klass: The GpkHelperDepsUpdateClass
 **/
static void
gpk_helper_deps_update_class_init (GpkHelperDepsUpdateClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = gpk_helper_deps_update_finalize;
	g_type_class_add_private (klass, sizeof (GpkHelperDepsUpdatePrivate));
	signals [GPK_HELPER_DEPS_UPDATE_EVENT] =
		g_signal_new ("event",
			      G_TYPE_FROM_CLASS (object_class), G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (GpkHelperDepsUpdateClass, event),
			      NULL, NULL, g_cclosure_marshal_VOID__UINT_POINTER,
			      G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);
}

/**
 * gpk_helper_deps_update_init:
 **/
static void
gpk_helper_deps_update_init (GpkHelperDepsUpdate *helper)
{
	helper->priv = GPK_HELPER_DEPS_UPDATE_GET_PRIVATE (helper);
	helper->priv->window = NULL;
	helper->priv->list = NULL;
	helper->priv->gconf_client = gconf_client_get_default ();
}

/**
 * gpk_helper_deps_update_finalize:
 **/
static void
gpk_helper_deps_update_finalize (GObject *object)
{
	GpkHelperDepsUpdate *helper;

	g_return_if_fail (GPK_IS_HELPER_DEPS_UPDATE (object));

	helper = GPK_HELPER_DEPS_UPDATE (object);
	g_object_unref (helper->priv->gconf_client);
	if (helper->priv->list != NULL)
		g_object_unref (helper->priv->list);

	G_OBJECT_CLASS (gpk_helper_deps_update_parent_class)->finalize (object);
}

/**
 * gpk_helper_deps_update_new:
 **/
GpkHelperDepsUpdate *
gpk_helper_deps_update_new (void)
{
	GpkHelperDepsUpdate *helper;
	helper = g_object_new (GPK_TYPE_HELPER_DEPS_UPDATE, NULL);
	return GPK_HELPER_DEPS_UPDATE (helper);
}

