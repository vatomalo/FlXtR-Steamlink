#include <assert.h>
#include <stdio.h>
#include <string.h>
#define MEDIA_RECENTS_MODE 38
#define FAVORITES_MODE 25
#define OFFLINE_MODE 37
typedef struct {int mode,page,id,season,episode,selected;char query[2048],name[80];} Browse;
static Browse browse={38,1,0,0,0,0,"",""},history[4];
static int history_size;
#include "../src/grouped_library_tabs.h"
int main(void){
    assert(library_current_group()==LIB_GROUP_RECENTS);
    assert(!strcmp(library_watch_labels[2],"ANIME"));
    assert(!strcmp(library_files_labels[1],"DOWNLOADS"));
    library_tabs_focused=1;assert(library_tabs_focused);library_tabs_focused=0;
    assert(library_group_entry_mode(LIB_GROUP_WATCH)==1);
    assert(library_group_entry_mode(LIB_GROUP_FILES)==30);
    for(int i=0;i<3;i++){
        assert(library_group_tab_index(LIB_GROUP_WATCH,library_watch_modes[i])==i);
        assert(library_group_tab_index(LIB_GROUP_FILES,library_files_modes[i])==i);
    }
    browse.mode=1;
    assert(library_can_focus_tabs()&&library_current_tab()==0);
    library_tab_cursor=1;
    /* Moving the highlight never changes the browsing source. */
    assert(browse.mode==1);
    library_tab_remember(library_group_tab_mode(LIB_GROUP_WATCH,library_tab_cursor));
    assert(library_group_entry_mode(LIB_GROUP_WATCH)==2);
    browse.mode=7;assert(library_current_group()==LIB_GROUP_WATCH);
    library_tab_remember(7);assert(library_group_entry_mode(LIB_GROUP_WATCH)==7);
    browse.mode=30;
    library_tab_remember(OFFLINE_MODE);
    assert(library_group_entry_mode(LIB_GROUP_FILES)==OFFLINE_MODE);
    browse.mode=OFFLINE_MODE;assert(library_current_group()==LIB_GROUP_FILES);
    history[0].mode=30;history_size=1;
    assert(library_current_group()==LIB_GROUP_FILES&&!library_can_focus_tabs());
    history_size=0;browse.mode=0;
    assert(library_can_focus_tabs()&&library_current_group()==LIB_GROUP_FILES);
    assert(library_group_for_mode(8)==LIB_GROUP_WATCH);
    assert(library_group_for_mode(31)==LIB_GROUP_FILES);
    assert(library_group_for_mode(22)==LIB_GROUP_GAMES);
    puts("PASS: group map, tab highlights, saved selection, nested browsing");
    return 0;
}
