/*
 * ttrv's window (ttrv/DESIGN.md section 1): the menu bar, the toolbar,
 * the location bar, the file list in the centre, the folder tree,
 * preview and information as dock panes around it, and ACDSee's status
 * bar. It turns what is on screen into the loader's wants and puts the
 * results where they belong.
 */
#ifndef TTRV_APP_H
#define TTRV_APP_H

#include "filelist.h"
#include "folders.h"
#include "imageview.h"
#include "loader.h"

#include "../../tx/dock.h"
#include "core/str.h"
#include "core/vector.h"

#include <Xm/Xm.h>

namespace ttrv {

struct Options {
    const char* start;          /* the folder (or a file in it) to open */
    const char* layout;         /* the docks; "" for the default */
    const char* list_font;
    int sort_key;
    bool sort_reverse;
    bool show_hidden;
    int cache_mb;               /* for previews */
    Options()
        : start(""), layout(""), list_font(""), sort_key(0), sort_reverse(false),
          show_hidden(false), cache_mb(48)
    {
    }
};

enum Command {
    CMD_BACK,
    CMD_FORWARD,
    CMD_UP,
    CMD_HOME,
    CMD_REFRESH,
    CMD_LOCATION,
    CMD_HIDDEN,
    CMD_SORT_NAME,
    CMD_SORT_SIZE,
    CMD_SORT_IMAGE,
    CMD_SORT_TYPE,
    CMD_SORT_TIME,
    CMD_PANE_FOLDERS,
    CMD_PANE_PREVIEW,
    CMD_PANE_INFO,
    CMD_MARK_ALL,
    CMD_UNMARK_ALL,
    CMD_ABOUT,
    CMD_EXIT
};

ttr::Str settings_path();

class App : public LoaderClient,
            public FileListHost,
            public FolderTreeHost,
            public ImageViewHost,
            public tx::PaneBuilder {
public:
    App(Widget top, const Options& o);
    ~App();

    void realized();
    void command(Command c);
    /* Show a folder; 'select' puts the cursor on that name. */
    void go(const char* path, const char* select = 0, bool remember = true);

    /* LoaderClient */
    void loaded(Result* r);
    /* FileListHost */
    void list_cursor(FileList* l, int row);
    void list_activate(FileList* l, int row);
    void list_back(FileList* l);
    void list_visible(FileList* l, int first, int last);
    /* FolderTreeHost */
    void tree_list(const char* path);
    void tree_chosen(const char* path);
    /* ImageViewHost */
    void view_resized(ImageView* v);
    /* tx::PaneBuilder */
    Widget build_pane(tx::Pane* p, Widget parent);
    void drop_pane(tx::Pane* p);
    void layout_changed();

    /* Callbacks (public for the C trampolines). */
    void loader_ready();
    void location_entered();
    void wants_timer();

private:
    App(const App&);
    App& operator=(const App&);

    /* A place in the history: the folder and the file the cursor was on. */
    struct Place {
        ttr::Str dir, name;
    };

    struct Cached {
        ttr::Str path;
        int w, h;
        Result* r;
        unsigned long used;
    };

    void build_menus();
    void build_toolbar();
    void build_status();
    void fill_list(const Result* r);
    void update_wants();
    void schedule_wants();
    void show_preview();
    void show_info();
    void show_status();
    Cached* cached(const char* path, int w, int h);
    Cached* cached_any(const char* path);
    void cache_put(Result* r);
    ttr::Str path_of(const Row* r) const;
    void save_settings();
    ListColors list_colors();
    void set_pane(tx::Pane* p, bool on);
    Place* here() const;

    Widget top_, form_, menubar_, toolbar_, locbar_, location_, body_;
    Widget status_[4];
    Widget info_;                   /* the information pane's text */
    tx::Docks* docks_;
    tx::Pane* pane_folders_;
    tx::Pane* pane_preview_;
    tx::Pane* pane_info_;
    FileList* list_;
    FolderTree* tree_;
    ImageView* preview_;
    Loader loader_;
    XtInputId input_;
    XtIntervalId wants_timer_;

    Options opt_;
    ttr::Str layout_;
    ttr::Str dir_;                  /* the folder shown */
    ttr::Str listing_;              /* the folder asked for, not here yet */
    ttr::Str select_;               /* the name to put the cursor on then */
    int list_err_;
    ttr::Vector<Place*> back_, forward_;
    ttr::Vector<ttr::Str*> tree_wants_;
    ttr::Vector<Cached*> cache_;
    unsigned long cache_bytes_, use_clock_;
    bool show_hidden_;
    int vis_first_, vis_last_;
};

} /* namespace ttrv */

#endif
