#include "ReaderScreen.h"
#include <JobPump.h>
#include <cstdio>
#include <cstring>
namespace reader {
static lv_obj_t *text(lv_obj_t *parent,const char *value,int x,int y,int width,const lv_font_t *font=&lv_font_montserrat_14) {
    auto *label=lv_label_create(parent);lv_label_set_text(label,value);lv_obj_set_pos(label,x,y);lv_obj_set_width(label,width);
    lv_obj_set_style_text_font(label,font,0);lv_obj_set_style_text_color(label,lv_color_hex(0xe2e8f0),0);
    return label;
}
void ReaderScreen::begin(Listener callback,void *user) { listener=callback;context=user; }
void ReaderScreen::clear() {
    dismiss();
    if (lv_display_get_default()) lv_obj_clean(lv_screen_active());
    view=View::None;title=detail=footer=previous=next=image=overlay=hint=modal=nullptr;
    memset(rows,0,sizeof(rows));memset(names,0,sizeof(names));memset(metadata,0,sizeof(metadata));event_count=0;page_image={};
}
void ReaderScreen::reset(View next_view) {
    clear();view=next_view;
    auto *screen=lv_screen_active();lv_obj_set_style_bg_color(screen,lv_color_hex(0x0b1220),0);
    lv_obj_set_style_bg_opa(screen,LV_OPA_COVER,0);lv_obj_set_scrollable(screen,false);
}
void ReaderScreen::clicked(lv_event_t *event) {
    auto *data=static_cast<EventData *>(lv_event_get_user_data(event));
    if (data && data->screen->listener) data->screen->listener(data->screen->context,data->action,data->index);
}
lv_obj_t *ReaderScreen::button(lv_obj_t *parent,const char *caption,int x,int y,int w,int h,Action action,unsigned index) {
    auto *b=lv_button_create(parent);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);
    lv_obj_set_style_bg_color(b,lv_color_hex(0x1e293b),0);lv_obj_set_style_radius(b,7,0);
    lv_obj_set_style_pad_all(b,0,0);lv_obj_set_scrollable(b,false);
    if (caption && *caption) { auto *label=text(b,caption,0,0,w-8);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(label); }
    if (event_count<32) { auto &e=events[event_count++];e={this,action,index};lv_obj_add_event_cb(b,clicked,LV_EVENT_CLICKED,&e); }
    return b;
}
void ReaderScreen::browser(const DirectoryBrowser &directory,ReadingStatsStore &stats,bool mounted) {
    if (view!=View::Browser) {
        reset(View::Browser);
        title=text(lv_screen_active(),"Comic reader",16,15,280,&lv_font_montserrat_28);
        button(lv_screen_active(),"Up",302,10,70,42,Action::Up);
        button(lv_screen_active(),"Refresh",380,10,86,42,Action::Refresh);
        detail=text(lv_screen_active(),"/",16,62,448);
        lv_label_set_long_mode(detail,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_set_height(detail,24);
        for (unsigned i=0;i<DirectoryBrowser::kRows;++i) {
            rows[i]=button(lv_screen_active(),"",12,104+i*59,456,54,Action::Row,i);
            names[i]=text(rows[i],"",10,5,436,&lv_font_montserrat_20);
            lv_label_set_long_mode(names[i],LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_set_height(names[i],24);
            metadata[i]=text(rows[i],"",10,32,436);
        }
        previous=button(lv_screen_active(),"Previous",12,720,100,44,Action::PreviousFolderPage);
        next=button(lv_screen_active(),"Next",368,720,100,44,Action::NextFolderPage);
        footer=text(lv_screen_active(),"",120,727,240);
        lv_obj_set_style_text_align(footer,LV_TEXT_ALIGN_CENTER,0);
    }
    lv_label_set_text(detail,directory.path().c_str());
    for (unsigned i=0;i<DirectoryBrowser::kRows;++i) {
        bool show=mounted && !directory.busy() && i<directory.count();
        lv_obj_set_hidden(rows[i],!show);
        if (!show) continue;
        const auto &entry=directory.entry(i);lv_label_set_text(names[i],entry.name.c_str());
        char size[48],line[192];
        if (entry.directory) snprintf(size,sizeof(size),"Folder | size --");
        else if (entry.bytes<1024) snprintf(size,sizeof(size),"%llu B",(unsigned long long)entry.bytes);
        else if (entry.bytes<1024*1024) snprintf(size,sizeof(size),"%.1f KiB",double(entry.bytes)/1024);
        else snprintf(size,sizeof(size),"%.1f MiB",double(entry.bytes)/(1024*1024));
        auto ext=entry.directory?"DIR":extension(entry.name);
        ReadingStats progress;bool saved=false;
        if (ext=="cbz") saved=stats.for_path(canonical_path(directory.path()+"/"+entry.name),entry.bytes,progress);
        if (saved) snprintf(line,sizeof(line),"%s | %s | %lu/%lu%s",size,ext.c_str(),(unsigned long)(progress.last_page+1),(unsigned long)progress.total_pages,progress.completed?" | Finished":"");
        else snprintf(line,sizeof(line),"%s | %s%s",size,ext.empty()?"FILE":ext.c_str(),ext=="cbz"?" | Unread":"");
        lv_label_set_text(metadata[i],line);
        pump_job();
    }
    lv_obj_set_hidden(previous,directory.busy() || directory.offset()==0);
    lv_obj_set_hidden(next,directory.busy() || directory.offset()+directory.count()>=directory.total());
    if (!mounted) lv_label_set_text(footer,"SD unavailable\nInsert card and reboot");
    else if (directory.busy()) lv_label_set_text(footer,"Reading folder...");
    else if (!directory.error().empty()) lv_label_set_text(footer,directory.error().c_str());
    else if (!directory.total()) lv_label_set_text(footer,"Empty folder");
    else lv_label_set_text_fmt(footer,"%u-%u of %u",directory.offset()+1,directory.offset()+directory.count(),directory.total());
}
void ReaderScreen::loading(const char *caption,const char *value,bool cancellable) {
    if (view!=View::Loading) {
        reset(View::Loading);title=text(lv_screen_active(),caption,24,240,432,&lv_font_montserrat_28);
        detail=text(lv_screen_active(),value,24,315,432,&lv_font_montserrat_20);
        footer=button(lv_screen_active(),"Cancel",170,470,140,54,Action::Cancel);
    }
    lv_label_set_text(title,caption);lv_label_set_text(detail,value);lv_obj_set_hidden(footer,!cancellable);
}
void ReaderScreen::reading(const uint16_t *pixels,const std::string &book,unsigned page,unsigned total) {
    if (view!=View::Reading) {
        reset(View::Reading);
        image=lv_image_create(lv_screen_active());lv_obj_set_pos(image,0,0);lv_obj_set_clickable(image,false);
        for (int i=0;i<3;++i) {
            Action action=i==0?Action::PreviousPage:i==1?Action::ToggleControls:Action::NextPage;
            auto *zone=button(lv_screen_active(),"",i*160,0,160,800,action);
            lv_obj_remove_style_all(zone);lv_obj_set_pos(zone,i*160,0);lv_obj_set_size(zone,160,800);
        }
        overlay=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(overlay);lv_obj_set_size(overlay,480,108);
        lv_obj_set_style_bg_color(overlay,lv_color_hex(0x0f172a),0);lv_obj_set_style_bg_opa(overlay,LV_OPA_90,0);lv_obj_set_scrollable(overlay,false);
        button(overlay,"Back",8,8,78,44,Action::Back);button(overlay,"Stats",394,8,78,44,Action::Info);
        title=text(overlay,book.c_str(),98,12,284,&lv_font_montserrat_20);lv_label_set_long_mode(title,LV_LABEL_LONG_SCROLL_CIRCULAR);lv_obj_set_height(title,24);
        detail=text(overlay,"",98,60,280);
        hint=text(lv_screen_active(),"Left/right: page    Center: controls",12,770,456);
        lv_obj_set_style_bg_color(hint,lv_color_hex(0x0f172a),0);lv_obj_set_style_bg_opa(hint,LV_OPA_90,0);lv_obj_set_style_text_align(hint,LV_TEXT_ALIGN_CENTER,0);
    }
    if (pixels) {
        page_image.header.magic=LV_IMAGE_HEADER_MAGIC;page_image.header.cf=LV_COLOR_FORMAT_RGB565;
        page_image.header.w=480;page_image.header.h=800;page_image.header.stride=960;
        page_image.data_size=480*800*2;page_image.data=reinterpret_cast<const uint8_t *>(pixels);
        lv_image_set_src(image,&page_image);lv_obj_set_hidden(image,false);lv_obj_invalidate(image);
    } else lv_obj_set_hidden(image,true);
    lv_label_set_text(title,book.c_str());lv_label_set_text_fmt(detail,"Page %u of %u",page,total);
}
void ReaderScreen::toggle_controls() { if (overlay) { bool hidden=!lv_obj_is_hidden(overlay);lv_obj_set_hidden(overlay,hidden);lv_obj_set_hidden(hint,hidden); } }
void ReaderScreen::message(const char *value) {
    dismiss();
    modal=lv_obj_create(lv_layer_top());lv_obj_remove_style_all(modal);lv_obj_set_size(modal,480,800);lv_obj_set_style_bg_color(modal,lv_color_black(),0);lv_obj_set_style_bg_opa(modal,LV_OPA_80,0);
    auto *panel=lv_obj_create(modal);lv_obj_set_size(panel,432,300);lv_obj_center(panel);lv_obj_set_scrollable(panel,false);lv_obj_set_style_bg_color(panel,lv_color_hex(0x1e293b),0);
    text(panel,value,4,8,380,&lv_font_montserrat_20);
    button(panel,"OK",140,220,100,44,Action::Dismiss);
}
void ReaderScreen::dismiss() { if (modal) { lv_obj_delete(modal);modal=nullptr; if (event_count && events[event_count-1].action==Action::Dismiss) --event_count; } }
}
