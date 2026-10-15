#include "ReaderApp.h"
#include <JobPump.h>
#include <reader_config.h>
#include <cstdio>
namespace reader {
bool ReaderApp::begin() {
    if (mode!=State::Stopped) return display.healthy();
    Serial.begin(115200);
    Serial.printf("Portrait CBZ reader built %s %s; LVGL %d.%d.%d\n",__DATE__,__TIME__,lv_version_major(),lv_version_minor(),lv_version_patch());
    if (!display.begin(LV_DISPLAY_ROTATION_90)) return false;
    touch.begin(display.handle());card.begin(READER_SD_FREQUENCY_HZ);
    screen.begin(action,this);set_job_pump(pump,this);
    browse("/");bool visible=display.show();
    if (!visible) mode=State::Stopped;
    last_diagnostic=millis();last_frames=display.frame_count();
    Serial.printf("Reader: 480x800 portrait; SD %lu Hz; UI ready\n",(unsigned long)card.frequency());
    return visible;
}
void ReaderApp::end() {
    if (mode==State::Stopped && !display.handle()) return;
    account_time();save_progress();screen.clear();decoder.release();extractor.close();directory.close();
    set_job_pump(nullptr,nullptr);card.end();touch.end();display.end();mode=State::Stopped;
    opening_counted=false;current_key.clear();pending=Action::None;pending_path.clear();
}
void ReaderApp::action(void *context,Action value,unsigned index) {
    auto *app=static_cast<ReaderApp *>(context);
    // These callbacks never perform SD I/O, decompression or page allocation.
    app->pending=value;app->pending_index=index;
}
void ReaderApp::open_book(const std::string &path) { pending_path=path;pending=Action::Row;pending_index=UINT32_MAX; }
void ReaderApp::next_page() { pending=Action::NextPage; }
void ReaderApp::previous_page() { pending=Action::PreviousPage; }
void ReaderApp::back() { pending=Action::Back; }
void ReaderApp::cancel() { pending=Action::Cancel; }
void ReaderApp::refresh() { pending=Action::Refresh; }
bool ReaderApp::pump(void *context) {
    auto *app=static_cast<ReaderApp *>(context);
    if (uint32_t(millis()-app->last_pump)>=16) { app->last_pump=millis();lv_timer_handler();delay(1); }
    return app->display.healthy() && app->pending!=Action::Cancel && app->pending!=Action::Back;
}
void ReaderApp::account_time() {
    uint32_t now=millis();
    if (mode==State::Reading && opening_counted) stats.reading_ms+=uint32_t(now-reading_tick);
    reading_tick=now;
}
void ReaderApp::save_progress() {
    if (!opening_counted || current_key.empty()) return;
    if (!stats_store.save(book_path,current_key,stats)) {
        last_error=stats_store.error();Serial.printf("Progress save failed: %s\n",last_error.c_str());
        progress_warning=last_error;
    }
    last_save=millis();
}
void ReaderApp::browse(const std::string &path,unsigned offset) {
    screen.clear();directory.open(path,offset);mode=State::Browser;metadata_ready=false;
    screen.browser(directory,stats_store,card.is_mounted());
}
void ReaderApp::open_now(const std::string &path) {
    std::string source=canonical_path(path);auto ext=extension(source);
    if (ext=="cbr") { screen.message("CBR support coming later. Please use a CBZ comic.");return; }
    if (ext!="cbz") { screen.message("This file cannot be opened. Choose a CBZ comic.");return; }
    account_time();save_progress();opening_counted=false;current_key.clear();progress_warning.clear();last_error.clear();
    screen.loading("Opening comic","Cleaning previous page cache...");
    directory.close();decoder.release();extractor.close();
    book_path=source;navigation_index=0;stats=ReadingStats{};metadata_ready=false;prefetch_failed=UINT32_MAX;
    if (!extractor.start(source)) { last_error=extractor.error();browse(directory.path(),directory.offset());screen.message(last_error.c_str());return; }
    mode=State::Extracting;
}
void ReaderApp::page_failure(const std::string &error) {
    last_error=error;mode=State::Reading;reading_tick=millis();
    screen.reading(opening_counted?decoder.pixels():nullptr,basename(book_path),opening_counted?stats.last_page+1:0,extractor.pages().size());
    screen.message(("Page "+std::to_string(navigation_index+1)+": "+last_error+"\nTap right to try the next page.").c_str());
}
void ReaderApp::load_page(unsigned index) {
    if (index>=extractor.pages().size()) return;
    account_time();navigation_index=index;
    // Reuse an in-flight prefetch of this exact page; redirects stop and remove
    // the partial job before reading any other archive member.
    if (!extractor.request_page(index,opening_counted?stats.last_page:UINT32_MAX)) { page_failure(extractor.error());return; }
    if (extractor.cached(index)) { decode_page(index);return; }
    mode=State::Extracting;
    screen.loading("Loading page",extractor.pages()[index].name);
}
void ReaderApp::prefetch_next() {
    if (!opening_counted) return;
    unsigned next=stats.last_page+1;
    if (next>=extractor.pages().size()) {
        extractor.request_page(stats.last_page,stats.last_page);return;
    }
    if (prefetch_failed==next) return;
    if (!extractor.request_page(next,stats.last_page)) {
        prefetch_failed=next;
        Serial.printf("Reader prefetch failed: %s\n",extractor.error().c_str());
    }
}
void ReaderApp::decode_page(unsigned index) {
    if (index>=extractor.pages().size()) return;
    account_time();mode=State::Decoding;navigation_index=index;
    screen.loading("Loading page",extractor.pages()[index].name);
    // This is called after lv_timer_handler has returned, never from an event.
    uint32_t start=millis();bool ok=decoder.decode(extractor.pages()[index]);
    if (pending==Action::Back || pending==Action::Cancel) {
        account_time();mode=State::Reading;reading_tick=millis();return;
    }
    mode=State::Reading;reading_tick=millis();
    if (ok) {
        screen.reading(decoder.pixels(),basename(book_path),index+1,extractor.pages().size());
        // The synchronous panel copy must succeed before this page can become
        // saved progress. A successful codec alone does not prove presentation.
        lv_refr_now(display.handle());
        if (!display.healthy()) { last_error="Page presentation failed; progress retained.";mode=State::Stopped;return; }
        last_error.clear();
        reading_tick=millis();
        if (!opening_counted) { ++stats.opening_count;opening_counted=true; }
        stats.last_page=index;stats.total_pages=extractor.pages().size();stats.completed=stats.completed || index+1==stats.total_pages;
        save_progress();
        if (!progress_warning.empty()) { screen.message(progress_warning.c_str());progress_warning.clear(); }
        Serial.printf("Reader page: %u/%u decoded in %lu ms\n",index+1,(unsigned)stats.total_pages,(unsigned long)(millis()-start));
        prefetch_next();
    } else page_failure(decoder.error());
}
void ReaderApp::handle_action() {
    Action command=pending;unsigned index=pending_index;pending=Action::None;
    if (command==Action::None) return;
    if (command==Action::Dismiss) { screen.dismiss();return; }
    if (command==Action::Cancel && mode==State::Extracting) {
        if (metadata_ready && opening_counted) {
            prefetch_failed=navigation_index;
            if (!extractor.stop_page()) { page_failure(extractor.error());return; }
            navigation_index=stats.last_page;mode=State::Reading;reading_tick=millis();
            screen.reading(decoder.pixels(),basename(book_path),stats.last_page+1,stats.total_pages);
        } else extractor.cancel();
        return;
    }
    if ((command==Action::Back || command==Action::Cancel) && (mode==State::Reading || mode==State::Decoding || (command==Action::Back && mode==State::Extracting && metadata_ready))) {
        account_time();save_progress();std::string warning=progress_warning;progress_warning.clear();
        opening_counted=false;current_key.clear();screen.clear();decoder.release();extractor.close();
        browse(directory.path(),directory.offset());if(!warning.empty()) screen.message(warning.c_str());return;
    }
    if (command==Action::Row && index==UINT32_MAX) { auto path=pending_path;pending_path.clear();open_now(path);return; }
    if (mode==State::Browser) {
        if (command==Action::Refresh) {
            if (!card.is_mounted()) card.begin(READER_SD_FREQUENCY_HZ);
            browse(directory.path(),directory.offset());
        } else if (command==Action::Up) browse(canonical_path(directory.path()+"/.."));
        else if (command==Action::PreviousFolderPage && directory.offset()>=DirectoryBrowser::kRows) browse(directory.path(),directory.offset()-DirectoryBrowser::kRows);
        else if (command==Action::NextFolderPage && directory.offset()+directory.count()<directory.total()) browse(directory.path(),directory.offset()+DirectoryBrowser::kRows);
        else if (command==Action::Row && !directory.busy() && index<directory.count()) {
            const auto &item=directory.entry(index);auto path=canonical_path(directory.path()+"/"+item.name);
            if (item.directory) browse(path);else open_now(path);
        }
    } else if (mode==State::Reading) {
        if (command==Action::NextPage && navigation_index+1<extractor.pages().size()) load_page(navigation_index+1);
        else if (command==Action::PreviousPage && navigation_index>0) load_page(navigation_index-1);
        else if (command==Action::ToggleControls) screen.toggle_controls();
        else if (command==Action::Info) {
            account_time();char text[240];
            snprintf(text,sizeof(text),"Page %lu of %lu\nOpened %lu times\nReading time: %llu min %llu sec\n%s",(unsigned long)(stats.last_page+1),(unsigned long)stats.total_pages,(unsigned long)stats.opening_count,(unsigned long long)(stats.reading_ms/60000),(unsigned long long)(stats.reading_ms/1000%60),stats.completed?"Finished":"In progress");
            screen.message(text);
        }
    }
}
void ReaderApp::diagnostics() {
    uint32_t now=millis(),elapsed=now-last_diagnostic;
    if (elapsed<5000) return;
    uint32_t frames=display.frame_count();
    Serial.printf("Reader health: %.2f Hz; internal %u largest %u; PSRAM %u; touch errors %lu; state %u\n",double(uint32_t(frames-last_frames))*1000/elapsed,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)ESP.getFreePsram(),(unsigned long)touch.state().errors,unsigned(mode));
    if (mode==State::Reading && opening_counted) Serial.printf("Reader progress: %s page %lu/%lu opens %lu visible_ms %llu completed %u\n",book_path.c_str(),(unsigned long)(stats.last_page+1),(unsigned long)stats.total_pages,(unsigned long)stats.opening_count,(unsigned long long)stats.reading_ms,unsigned(stats.completed));
    if (metadata_ready) Serial.printf("Reader cache: %u verified pages; prefetch %u\n",extractor.cached_count(),unsigned(prefetch_busy()));
    last_diagnostic=now;last_frames=frames;
}
void ReaderApp::service() {
    if (mode==State::Stopped || !display.healthy()) { delay(5);return; }
    lv_timer_handler();account_time();handle_action();
    if (mode==State::Browser && directory.busy()) {
        if (directory.step()) screen.browser(directory,stats_store,card.is_mounted());
    } else if (mode==State::Extracting) {
        extractor.step();
        char progress[160];
        if (extractor.phase()==ComicExtractor::Phase::Indexing) snprintf(progress,sizeof(progress),"Indexing archive: %u entries",extractor.scanned());
        else if (metadata_ready && navigation_index<extractor.pages().size()) snprintf(progress,sizeof(progress),"Page %u / %u: %lu / %lu KiB",navigation_index+1,(unsigned)extractor.pages().size(),(unsigned long)(extractor.extracted_bytes()/1024),(unsigned long)(extractor.pages()[navigation_index].bytes/1024));
        else snprintf(progress,sizeof(progress),"Preparing comic index...");
        screen.loading(metadata_ready?"Loading page":"Opening comic",progress,extractor.phase()!=ComicExtractor::Phase::Cancelling);
        if (extractor.phase()==ComicExtractor::Phase::Ready) {
            if (!metadata_ready) {
                current_key=extractor.key();stats_store.load(current_key,stats);progress_warning=stats_store.error();
                stats.source_bytes=0;File source=card.open(book_path.c_str());if (source) stats.source_bytes=source.size();source.close();
                stats.total_pages=extractor.pages().size();metadata_ready=true;
                load_page(std::min<uint32_t>(stats.last_page,stats.total_pages-1));
            } else decode_page(navigation_index);
        } else if (extractor.phase()==ComicExtractor::Phase::PageFailed) {
            page_failure(extractor.error());
        } else if (extractor.phase()==ComicExtractor::Phase::Failed || extractor.phase()==ComicExtractor::Phase::Cancelled) {
            last_error=extractor.error();browse(directory.path(),directory.offset());
            if (!last_error.empty()) screen.message(last_error.c_str());
        }
    } else if (mode==State::Reading) {
        // A single bounded ZIP step per loop, with the current page still visible.
        if (extractor.busy()) {
            extractor.step();
            if (extractor.phase()==ComicExtractor::Phase::PageFailed) {
                prefetch_failed=extractor.requested_page();
                Serial.printf("Reader prefetch failed: %s\n",extractor.error().c_str());
            }
        }
        if (uint32_t(millis()-last_save)>=30000) {
            save_progress();
            if(!progress_warning.empty()) { screen.message(progress_warning.c_str());progress_warning.clear(); }
        }
    }
    diagnostics();delay(5);
}
}
