#pragma once

void startup (GtkApplication *app, gpointer);

void activate (GtkApplication *app, gpointer);

void quit (GSimpleAction *, GVariant *, gpointer);

void about (GSimpleAction *, GVariant *, gpointer);

GdkPixbuf *create_logo (gboolean);

GtkWidget *create_main_window (GtkApplication *app);