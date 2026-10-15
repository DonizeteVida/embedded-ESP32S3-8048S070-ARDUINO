#pragma once
#include <SdCard.h>
#include <lvgl.h>
#include <DirectoryBrowser.h>
#include <ReadingStatsStore.h>
namespace reader {
enum class Action { None, Row, Up, Refresh, PreviousFolderPage, NextFolderPage, PreviousPage, NextPage, ToggleControls, Back, Cancel, Info, Dismiss };
class ReaderScreen {
public:
    using Listener=void (*)(void *,Action,unsigned);
    void begin(Listener callback,void *context);
    void browser(const DirectoryBrowser &directory,ReadingStatsStore &stats,bool mounted);
    void loading(const char *title,const char *detail,bool cancellable=true);
    void reading(const uint16_t *pixels,const std::string &title,unsigned page,unsigned total);
    void toggle_controls();
    void message(const char *text);
    void dismiss();
    void clear();
private:
    enum class View { None, Browser, Loading, Reading } view=View::None;
    Listener listener=nullptr;void *context=nullptr;
    lv_obj_t *title=nullptr,*detail=nullptr,*footer=nullptr,*previous=nullptr,*next=nullptr;
    lv_obj_t *image=nullptr,*overlay=nullptr,*hint=nullptr,*modal=nullptr;
    lv_obj_t *rows[DirectoryBrowser::kRows]={},*names[DirectoryBrowser::kRows]={},*metadata[DirectoryBrowser::kRows]={};
    struct EventData { ReaderScreen *screen;Action action;unsigned index; } events[32];
    unsigned event_count=0;
    lv_image_dsc_t page_image={};
    lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,Action action,unsigned index=0);
    static void clicked(lv_event_t *event);
    void reset(View next_view);
};
}
