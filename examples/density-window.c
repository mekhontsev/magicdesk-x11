#include <gtk/gtk.h>
#include <stdio.h>

static void report(GtkWidget* window) {
    int dpi;
    g_object_get(gtk_settings_get_for_screen(gtk_widget_get_screen(window)), "gtk-xft-dpi", &dpi, NULL);
    printf("scale=%d fontDpi=%.3f screenDpi=%.3f\n", gtk_widget_get_scale_factor(window),
            dpi / 1024.0, gdk_screen_get_resolution(gtk_widget_get_screen(window)));
    fflush(stdout);
}

static void changed(GObject* object, GParamSpec* spec, gpointer window) {
    (void)object; (void)spec;
    report(GTK_WIDGET(window));
}

static void clicked(GtkButton* button, gpointer window) {
    gtk_button_set_label(button, "Clicked");
    report(GTK_WIDGET(window));
}

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "X11 density fixture");
    gtk_window_set_default_size(GTK_WINDOW(window), 360, 240);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);
    gtk_container_add(GTK_CONTAINER(window), box);
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new("Logical DPI / live scaling"), FALSE, FALSE, 0);
    GtkWidget* button = gtk_button_new_with_label("Check input");
    gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(window, "notify::scale-factor", G_CALLBACK(changed), window);
    g_signal_connect(gtk_settings_get_default(), "notify::gtk-xft-dpi", G_CALLBACK(changed), window);
    g_signal_connect(button, "clicked", G_CALLBACK(clicked), window);
    gtk_widget_show_all(window);
    report(window);
    gtk_main();
}
