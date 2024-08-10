#include <gtk/gtk.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <libexif/exif-data.h>

typedef struct {
    GtkTreeStore *store;
    GtkWidget *image;
    GtkWidget *text_view;
    GtkWidget *search_entry;
    gchar *folder;
} AppWidgets;


gboolean is_image_file(const char *filename) {
    const char *extensions[] = {".png", ".jpg", ".jpeg", ".gif", ".bmp", NULL};
    for (int i = 0; extensions[i] != NULL; i++) {
        if (g_str_has_suffix(filename, extensions[i])) {
            return TRUE;
        }
    }
    return FALSE;
}


gboolean is_regular_file(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}


gchar* get_exif_data_as_string(const gchar *filepath) {
    ExifData *ed = exif_data_new_from_file(filepath);
    if (!ed) return NULL;

    GString *exif_string = g_string_new("");

    for (int i = 0; i < EXIF_IFD_COUNT; i++) {
        ExifIfd ifd = (ExifIfd)i;
        ExifEntry *entry;
        for (entry = exif_content_get_entry(ed->ifd[ifd], 0); entry; entry = exif_content_get_entry(ed->ifd[ifd], entry->tag + 1)) {
            char value[1024];
            exif_entry_get_value(entry, value, sizeof(value));
            g_string_append_printf(exif_string, "%s ", value);
        }
    }

    exif_data_unref(ed);
    return g_string_free(exif_string, FALSE);
}


void update_exif_data(GtkTextView *text_view, const gchar *filepath) {
    ExifData *ed = exif_data_new_from_file(filepath);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(text_view);
    gtk_text_buffer_set_text(buffer, "", -1); 

    if (!ed) {
        gtk_text_buffer_set_text(buffer, "No EXIF data found.", -1);
        return;
    }

    
    for (int i = 0; i < EXIF_IFD_COUNT; i++) {
        ExifIfd ifd = (ExifIfd)i;
        ExifEntry *entry;
        for (entry = exif_content_get_entry(ed->ifd[ifd], 0); entry; entry = exif_content_get_entry(ed->ifd[ifd], entry->tag + 1)) {
            char entry_str[1024];
            char value[1024];

            exif_entry_get_value(entry, value, sizeof(value));
            snprintf(entry_str, sizeof(entry_str), "%s: %s\n", exif_tag_get_name(entry->tag), value);

            GtkTextIter iter;
            gtk_text_buffer_get_end_iter(buffer, &iter);
            gtk_text_buffer_insert(buffer, &iter, entry_str, -1);
        }
    }

    exif_data_unref(ed);
}


void on_file_selected(GtkTreeSelection *selection, gpointer data) {
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *filename;
    AppWidgets *widgets = (AppWidgets*)data;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gtk_tree_model_get(model, &iter, 0, &filename, -1);

        
        gchar *filepath = g_build_filename(widgets->folder, filename, NULL);

        
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(filepath, NULL);
        if (pixbuf) {
            int original_width = gdk_pixbuf_get_width(pixbuf);
            int original_height = gdk_pixbuf_get_height(pixbuf);
            int new_width = (original_width * 800) / original_height;

            GdkPixbuf *scaled_pixbuf = gdk_pixbuf_scale_simple(pixbuf, new_width, 800, GDK_INTERP_BILINEAR);
            gtk_image_set_from_pixbuf(GTK_IMAGE(widgets->image), scaled_pixbuf);

            g_object_unref(pixbuf);
            g_object_unref(scaled_pixbuf);
        }

        
        update_exif_data(GTK_TEXT_VIEW(widgets->text_view), filepath);

        g_free(filepath);
        g_free(filename);
    } else {
        
        gtk_image_clear(GTK_IMAGE(widgets->image));
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(widgets->text_view)), "", -1);
    }
}


void on_search_entry_changed(GtkEntry *entry, gpointer data) {
    AppWidgets *widgets = (AppWidgets*)data;
    const gchar *search_text = gtk_entry_get_text(entry);

    
    gtk_tree_store_clear(widgets->store);

    
    DIR *dir;
    struct dirent *entry_dir;
    GtkTreeIter iter;
    gboolean any_matches = FALSE;

    if ((dir = opendir(widgets->folder)) != NULL) {
        while ((entry_dir = readdir(dir)) != NULL) {
            char filepath[1024];
            snprintf(filepath, sizeof(filepath), "%s/%s", widgets->folder, entry_dir->d_name);

            if (is_regular_file(filepath) && is_image_file(entry_dir->d_name)) {
                
                gchar *exif_data = get_exif_data_as_string(filepath);

                
                if (g_strrstr(entry_dir->d_name, search_text) || (exif_data && g_strrstr(exif_data, search_text))) {
                    gtk_tree_store_append(widgets->store, &iter, NULL);
                    gtk_tree_store_set(widgets->store, &iter, 0, entry_dir->d_name, -1);
                    any_matches = TRUE;
                }

                g_free(exif_data);
            }
        }
        closedir(dir);
    }

    
    if (!any_matches) {
        gtk_image_clear(GTK_IMAGE(widgets->image));
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(widgets->text_view)), "", -1);
    }
}


void on_button_clicked(GtkWidget *widget, gpointer data) {
    AppWidgets *widgets = (AppWidgets*)data;
    GtkWidget *dialog;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
    gint res;

    dialog = gtk_file_chooser_dialog_new("Select Directory",
                                         GTK_WINDOW(widget),
                                         action,
                                         "_Cancel", GTK_RESPONSE_CANCEL,
                                         "_Open", GTK_RESPONSE_ACCEPT,
                                         NULL);

    res = gtk_dialog_run(GTK_DIALOG(dialog));

    if (res == GTK_RESPONSE_ACCEPT) {
        if (widgets->folder) g_free(widgets->folder);
        widgets->folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        g_print("Selected folder: %s\n", widgets->folder);

        
        on_search_entry_changed(GTK_ENTRY(widgets->search_entry), widgets);
    }

    gtk_widget_destroy(dialog);
}

int main(int argc, char *argv[]) {
    GtkWidget *window;
    GtkWidget *hbox;
    GtkWidget *vbox_left;
    GtkWidget *button;
    GtkWidget *search_entry;
    GtkWidget *image;
    GtkWidget *vbox_right;
    GtkWidget *text_view;
    GtkWidget *scroll_window;
    GtkWidget *tree_view;
    GtkTreeSelection *selection;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeStore *store;
    AppWidgets *widgets;

    gtk_init(&argc, &argv);

    widgets = g_slice_new(AppWidgets);
    widgets->folder = NULL;

    
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Image Viewer with Filter");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 600);

    
    hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_container_add(GTK_CONTAINER(window), hbox);

    
    vbox_left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(vbox_left, 240, -1); 
    gtk_box_pack_start(GTK_BOX(hbox), vbox_left, FALSE, TRUE, 0);

    
    search_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(search_entry), "Search...");
    gtk_box_pack_start(GTK_BOX(vbox_left), search_entry, FALSE, FALSE, 0);

    
    button = gtk_button_new_with_label("Open Directory");
    gtk_box_pack_start(GTK_BOX(vbox_left), button, FALSE, FALSE, 0);

    
    store = gtk_tree_store_new(1, G_TYPE_STRING);
    widgets->store = store;

    
    tree_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Image File Name", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);

    
    scroll_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll_window), tree_view);
    gtk_box_pack_start(GTK_BOX(vbox_left), scroll_window, TRUE, TRUE, 0);

    
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
    g_signal_connect(selection, "changed", G_CALLBACK(on_file_selected), widgets);

    
    vbox_right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_start(GTK_BOX(hbox), vbox_right, TRUE, TRUE, 0);

    
    image = gtk_image_new(); 
    gtk_box_pack_start(GTK_BOX(vbox_right), image, FALSE, FALSE, 0);
    widgets->image = image;

    
    scroll_window = gtk_scrolled_window_new(NULL, NULL);
    text_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(text_view), FALSE);
    gtk_container_add(GTK_CONTAINER(scroll_window), text_view);
    gtk_box_pack_start(GTK_BOX(vbox_right), scroll_window, TRUE, TRUE, 0);
    widgets->text_view = text_view;

    
    g_signal_connect(button, "clicked", G_CALLBACK(on_button_clicked), widgets);

    
    widgets->search_entry = search_entry;
    g_signal_connect(search_entry, "changed", G_CALLBACK(on_search_entry_changed), widgets);

    
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    
    gtk_widget_show_all(window);


    gtk_main();

    if (widgets->folder) g_free(widgets->folder);
    g_slice_free(AppWidgets, widgets);

    return 0;
}
