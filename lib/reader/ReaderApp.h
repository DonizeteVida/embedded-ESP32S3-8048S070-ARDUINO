#pragma once
#include <Display.h>
#include <Touch.h>
#include <SdCard.h>
#include <ComicExtractor.h>
#include <DirectoryBrowser.h>
#include <PageDecoder.h>
#include <ReadingStatsStore.h>
#include <ReaderScreen.h>
namespace reader {
class ReaderApp {
public:
    enum class State { Browser, Extracting, Reading, Decoding, Stopped };
    ReaderApp() : directory(card),extractor(card),decoder(card),stats_store(card) {}
    ~ReaderApp() { end(); }
    bool begin();
    void service();
    void end();
    // The same queued commands serve UI callbacks and integration tests.
    void open_book(const std::string &path);
    void next_page();
    void previous_page();
    void back();
    void cancel();
    void refresh();
    State state() const { return mode; }
    const ReadingStats &statistics() const { return stats; }
    unsigned page_count() const { return extractor.pages().size(); }
    unsigned displayed_page() const { return stats.last_page; }
    const std::string &error() const { return last_error; }
    SdCard &storage() { return card; }
    Display &display_device() { return display; }
    Touch &touch_device() { return touch; }
    bool browser_busy() const { return directory.busy(); }
    const uint16_t *page_pixels() const { return decoder.pixels(); }
private:
    Display display;Touch touch;SdCard card;
    DirectoryBrowser directory;ComicExtractor extractor;PageDecoder decoder;ReadingStatsStore stats_store;
    ReaderScreen screen;ReadingStats stats;
    State mode=State::Stopped;
    Action pending=Action::None;
    unsigned pending_index=0,navigation_index=0;
    bool opening_counted=false;
    uint32_t reading_tick=0,last_save=0,last_pump=0,last_diagnostic=0,last_frames=0;
    std::string pending_path,book_path,current_key,last_error,progress_warning;
    void browse(const std::string &path,unsigned offset=0);
    void open_now(const std::string &path);
    void decode_page(unsigned index);
    void handle_action();
    void account_time();
    void save_progress();
    void diagnostics();
    static void action(void *context,Action action,unsigned index);
    static bool pump(void *context);
};
}
