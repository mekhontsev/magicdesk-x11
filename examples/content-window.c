/* Interactive protocol fixture: GTK owns selections, INCR and XDND exactly as an application does. */
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

static GtkTargetEntry targets[] = {{"UTF8_STRING", 0, 0}, {"text/plain;charset=utf-8", 0, 0},
    {"text/html", 0, 1}, {"image/png", 0, 2}, {"text/uri-list", 0, 3}};
static char* file_uri;
static GdkPixbuf* image;
static char* large_text;

static void provide(GtkSelectionData* data, guint info) {
    if (info == 2) gtk_selection_data_set_pixbuf(data, image);
    else if (info == 3) { char* uris[] = {file_uri, NULL}; gtk_selection_data_set_uris(data, uris); }
    else {
        const char* value = info == 1 ? "<b>MagicDesk</b> <i>X11 HTML</i>" : large_text;
        gtk_selection_data_set(data, gtk_selection_data_get_target(data), 8, (const guchar*)value, strlen(value));
    }
}
static void clipboard_get(GtkClipboard* clipboard, GtkSelectionData* data, guint info, gpointer user) { provide(data, info); }
static void clipboard_clear(GtkClipboard* clipboard, gpointer user) { }
static void copy_clicked(GtkWidget* widget, gpointer user) {
    int index = GPOINTER_TO_INT(user);
    GtkTargetEntry* entries = index < 0 ? targets : targets + index;
    int count = index < 0 ? G_N_ELEMENTS(targets) : 1;
    gtk_clipboard_set_with_data(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), entries, count, clipboard_get, clipboard_clear, NULL);
    printf("COPIED %s\n", index < 0 ? "all" : entries->target); fflush(stdout);
}
static void received(GtkClipboard* clipboard, GtkSelectionData* data, gpointer user) {
    int length = gtk_selection_data_get_length(data);
    const guchar* bytes = gtk_selection_data_get_data(data);
    char* digest = length >= 0 ? g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes, length) : g_strdup("rejected");
    printf("CLIPBOARD %s bytes=%d sha256=%s\n", (char*)user, length, digest);
    if (length > 0 && !strcmp(user, "UTF8_STRING")) printf("TEXT %.100s\n", bytes);
    fflush(stdout); g_free(digest);
}
static void paste_clicked(GtkWidget* widget, gpointer user) {
    for (guint i = 0; i < G_N_ELEMENTS(targets); i++)
        gtk_clipboard_request_contents(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                gdk_atom_intern(targets[i].target, FALSE), received, targets[i].target);
}
static void drag_get(GtkWidget* widget, GdkDragContext* drag, GtkSelectionData* data, guint info, guint time, gpointer user) {
    provide(data, info);
}
static void drag_received(GtkWidget* widget, GdkDragContext* drag, gint x, gint y, GtkSelectionData* data,
        guint info, guint time, gpointer user) {
    int length = gtk_selection_data_get_length(data);
    char* type = gdk_atom_name(gtk_selection_data_get_target(data));
    printf("DROP %s bytes=%d\n", type, length);
    if (info == 3 && length > 0) {
        char** uris = gtk_selection_data_get_uris(data);
        for (char** uri = uris; uri && *uri; uri++) {
            GError* error = NULL;
            char* path = g_filename_from_uri(*uri, NULL, &error);
            gchar* bytes = NULL; gsize size = 0;
            gboolean readable = path && g_file_get_contents(path, &bytes, &size, &error);
            printf("FILE readable=%d bytes=%zu path=%s error=%s\n", readable, size, path ?: "", error ? error->message : "");
            g_free(path); g_free(bytes); if (error) g_error_free(error);
        }
        g_strfreev(uris);
    }
    fflush(stdout); g_free(type);
    gtk_drag_finish(drag, length >= 0, FALSE, time);
}
static void drag_end(GtkWidget* widget, GdkDragContext* drag, gpointer user) { puts("DRAG ENDED"); fflush(stdout); }
static gboolean drag_failed(GtkWidget* widget, GdkDragContext* drag, GtkDragResult result, gpointer user) {
    printf("DRAG FAILED %d\n", result); fflush(stdout); return FALSE;
}

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    large_text = g_malloc0(700001);
    for (int i = 0; i < 700000; i++) large_text[i] = 'a' + i % 26;
    char* path = g_build_filename(g_get_tmp_dir(), "magicdesk-content-fixture.txt", NULL);
    g_file_set_contents(path, "MagicDesk X11 file transfer\n", -1, NULL);
    file_uri = g_filename_to_uri(path, NULL, NULL); g_free(path);
    image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, 640, 480);
    guchar* pixels = gdk_pixbuf_get_pixels(image);
    int stride = gdk_pixbuf_get_rowstride(image);
    for (int y = 0; y < 480; y++) for (int x = 0; x < 640; x++) {
        guchar* p = pixels + y * stride + x * 4;
        p[0] = (x * 37 + y * 7) % 256; p[1] = (y * 17 + x * 3) % 256; p[2] = (x + y) % 256; p[3] = 255;
    }
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), argc > 1 ? argv[1] : "X11 content fixture");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 700);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(window), box);
    const char* names[] = {"Copy large text", "Copy HTML", "Copy image", "Copy file", "Read clipboard"};
    int indexes[] = {0, 2, 3, 4};
    for (int i = 0; i < 5; i++) {
        GtkWidget* button = gtk_button_new_with_label(names[i]);
        gtk_widget_set_size_request(button, -1, 54);
        g_signal_connect(button, "clicked", i == 4 ? G_CALLBACK(paste_clicked) : G_CALLBACK(copy_clicked),
                GINT_TO_POINTER(i == 4 ? 0 : indexes[i]));
        gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    }
    const char* drags[] = {"Drag text", "Drag image", "Drag file"};
    int drag_indexes[] = {0, 3, 4};
    for (int i = 0; i < 3; i++) {
        GtkWidget* label = gtk_event_box_new();
        gtk_container_add(GTK_CONTAINER(label), gtk_label_new(drags[i]));
        gtk_widget_set_size_request(label, -1, 70);
        gtk_drag_source_set(label, GDK_BUTTON1_MASK, targets + drag_indexes[i], 1, GDK_ACTION_COPY);
        g_signal_connect(label, "drag-data-get", G_CALLBACK(drag_get), NULL);
        g_signal_connect(label, "drag-end", G_CALLBACK(drag_end), NULL);
        g_signal_connect(label, "drag-failed", G_CALLBACK(drag_failed), NULL);
        gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    }
    GtkWidget* destination = gtk_label_new("Drop text, image or file here");
    gtk_widget_set_size_request(destination, -1, 120);
    gtk_drag_dest_set(destination, GTK_DEST_DEFAULT_ALL, targets, G_N_ELEMENTS(targets), GDK_ACTION_COPY);
    g_signal_connect(destination, "drag-data-received", G_CALLBACK(drag_received), NULL);
    gtk_box_pack_start(GTK_BOX(box), destination, TRUE, TRUE, 0);
    gtk_widget_show_all(window);
    puts("READY"); fflush(stdout);
    gtk_main();
    g_object_unref(image); g_free(file_uri); g_free(large_text);
    return 0;
}
