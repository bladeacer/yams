#include "layout.h"
#include <ncurses.h>
#include <panel.h>
#include "ui.h"
#include "art_manager.h" // Needed for reset_art()

// 1. Correct the type name to match the header
Geometry geo; 

// 2. These match the header's externs
WINDOW *main_win = NULL;
PANEL  *main_panel = NULL;
WINDOW *lyr_sub = NULL;
WINDOW *meta_sub = NULL;

void recreate_layout() {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // Clean up old instances
    if (main_panel) { del_panel(main_panel); main_panel = NULL; }
    if (lyr_sub)    { delwin(lyr_sub);    lyr_sub = NULL; }
    if (meta_sub)   { delwin(meta_sub);   meta_sub = NULL; }
    if (main_win)   { delwin(main_win);   main_win = NULL; }

    // Physical screen management
    resizeterm(max_y, max_x);
    clearok(curscr, TRUE); // Force a complete redraw of the physical terminal
    erase();               // Clear ncurses internal buffer
    wnoutrefresh(stdscr);  // Stage the clear without pushing yet
    
    // Store new dimensions
    geo.max_x = max_x;
    geo.max_y = max_y;
    
    int safe_w = max_x - 2;
    int safe_h = max_y - 2;
    geo.horizontal = (max_x > 90);

    // Create the main window
    main_win = newwin(safe_h, safe_w, 1, 1);
    
    /* * leaveok prevents the cursor from jumping back to the window 
     * during updates, which prevents it from "flickering" over 
     * the area where the external art process is drawing.
     */
    leaveok(main_win, TRUE); 
    
    main_panel = new_panel(main_win);

    if (geo.horizontal) {
        geo.art_w = (safe_w * 0.4 > 54) ? 54 : (int)(safe_w * 0.4);
        geo.art_h = (int)(safe_h * 0.6);
        int split_x = geo.art_w + 4;
        
        // Metadata subwindow
        meta_sub = derwin(main_win, safe_h - geo.art_h - 2, split_x - 2, geo.art_h + 1, 1);
        // Lyrics subwindow
        lyr_sub = derwin(main_win, safe_h - 1, safe_w - split_x - 1, 0, split_x + 1);
    } else {
        geo.art_w = (safe_w - 4 > 50) ? 50 : safe_w - 4;
        geo.art_h = (int)(safe_h * 0.4);
        int meta_h = 11;
        
        // Metadata subwindow
        meta_sub = derwin(main_win, meta_h, safe_w - 2, geo.art_h + 1, 1);
        
        int lyr_y = geo.art_h + meta_h + 2;
        int lyr_h = safe_h - lyr_y;
        if (lyr_h > 0) {
            lyr_sub = derwin(main_win, lyr_h, safe_w - 2, lyr_y, 1);
        }
    }

    // Reset art tracking so the next draw_dashboard triggers a fresh render
    reset_art();
    resize_pending = false;
}
