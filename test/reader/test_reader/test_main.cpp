#include <unity.h>
#include <ReaderApp.h>
#include "fixtures/reader_fixtures.h"
using namespace reader;
static ReaderApp app;
static File file;
static std::string test_root, first_book, second_book;
static bool fixture_owned=false;
static void wait_for(ReaderApp::State desired,uint32_t timeout=90000) {
    uint32_t start=millis();
    do { app.service();if(app.state()==desired && (desired!=ReaderApp::State::Browser || !app.browser_busy())) return; }
    while(uint32_t(millis()-start)<timeout);
    TEST_FAIL_MESSAGE(app.error().empty()?"Reader action timed out":app.error().c_str());
}
static void wait_page(unsigned page) {
    wait_for(ReaderApp::State::Reading);TEST_ASSERT_EQUAL(page,app.displayed_page());
    TEST_ASSERT_LESS_OR_EQUAL(2,app.cached_page_count());
}
static void wait_prefetch() {
    uint32_t start=millis();
    while(app.prefetch_busy() && uint32_t(millis()-start)<30000) app.service();
    TEST_ASSERT_FALSE(app.prefetch_busy());TEST_ASSERT_LESS_OR_EQUAL(2,app.cached_page_count());
}
static void put(const std::string &path,const uint8_t *bytes,size_t size) {
    TEST_ASSERT_FALSE(app.storage().exists(path.c_str()));
    file=app.storage().open(path.c_str(),FILE_WRITE);TEST_ASSERT_TRUE(file);
    for(size_t offset=0;offset<size;) {
        size_t chunk=std::min<size_t>(4096,size-offset);
        TEST_ASSERT_EQUAL(chunk,file.write(bytes+offset,chunk));offset+=chunk;app.service();
    }
    file.close();
}
void setUp() {
    TEST_ASSERT_TRUE(app.begin());TEST_ASSERT_TRUE(app.storage().is_mounted());wait_for(ReaderApp::State::Browser);
    TEST_ASSERT_EQUAL(480,lv_display_get_horizontal_resolution(app.display_device().handle()));
    TEST_ASSERT_EQUAL(800,lv_display_get_vertical_resolution(app.display_device().handle()));
    for(unsigned i=0;i<1000;++i) {
        test_root="/reader-test-"+std::to_string(millis())+"-"+std::to_string(i);
        if(!app.storage().exists(test_root.c_str())) break;
    }
    TEST_ASSERT_FALSE(app.storage().exists(test_root.c_str()));
    TEST_ASSERT_TRUE(app.storage().mkdir(test_root.c_str()));fixture_owned=true;
    first_book=test_root+"/Numbered.cbz";second_book=test_root+"/Second.cbz";
    put(first_book,fixtures::ordered,sizeof(fixtures::ordered));put(second_book,fixtures::second,sizeof(fixtures::second));
}
void tearDown() {
    file.close();app.back();app.service();
    if(fixture_owned) {
        bool first=!app.storage().exists(first_book.c_str()) || app.storage().remove(first_book.c_str());
        bool second=!app.storage().exists(second_book.c_str()) || app.storage().remove(second_book.c_str());
        bool removed=first && second && app.storage().rmdir(test_root.c_str());fixture_owned=!removed;
        if(!removed) Serial.printf("Retained test fixture: %s\n",test_root.c_str());
    }
    app.end();TEST_ASSERT_FALSE_MESSAGE(fixture_owned,"Could not remove generated comic fixtures");
}
static void test_reader_extract_decode_resume_cancel_and_one_minute_stability() {
    app.open_book(first_book);wait_for(ReaderApp::State::Reading);
    TEST_ASSERT_EQUAL(3,app.page_count());TEST_ASSERT_EQUAL(0,app.displayed_page());
    uint16_t color=app.page_pixels()[500*480+240];TEST_ASSERT_TRUE((color>>11)>(color&31));
    TEST_ASSERT_FALSE(app.page_cached(2));wait_prefetch();
    TEST_ASSERT_TRUE(app.page_cached(0));TEST_ASSERT_TRUE(app.page_cached(1));
    app.next_page();wait_page(1);
    color=app.page_pixels()[500*480+240];TEST_ASSERT_TRUE(((color>>5)&63)>(color>>11));
    app.back();wait_for(ReaderApp::State::Browser);
    app.open_book(second_book);wait_for(ReaderApp::State::Reading);TEST_ASSERT_EQUAL(2,app.page_count());
    app.next_page();wait_page(1);TEST_ASSERT_TRUE(app.statistics().completed);
    app.back();wait_for(ReaderApp::State::Browser);
    app.open_book(first_book);app.service();TEST_ASSERT_EQUAL(int(ReaderApp::State::Extracting),int(app.state()));
    app.cancel();wait_for(ReaderApp::State::Browser);
    app.open_book(first_book);wait_for(ReaderApp::State::Reading);TEST_ASSERT_EQUAL(1,app.displayed_page());
    TEST_ASSERT_EQUAL(2,app.statistics().opening_count);TEST_ASSERT_FALSE(app.page_cached(0));
    app.end();TEST_ASSERT_TRUE(app.begin());wait_for(ReaderApp::State::Browser);
    app.open_book(first_book);wait_for(ReaderApp::State::Reading);TEST_ASSERT_EQUAL(1,app.displayed_page());
    TEST_ASSERT_EQUAL(3,app.statistics().opening_count);
    uint32_t start=millis(),last_turn=start,last_frames=app.display_device().frame_count();unsigned turns=0,target=app.displayed_page();bool turn_pending=false;
    uint32_t initial_internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    Serial.println("READER SOAK START: inspect portrait pages and tap center for controls; 60 seconds.");
    while(uint32_t(millis()-start)<60000) {
        app.service();TEST_ASSERT_TRUE(app.display_device().healthy());TEST_ASSERT_EQUAL(0,app.touch_device().state().errors);
        TEST_ASSERT_LESS_OR_EQUAL(2,app.cached_page_count());
        if(turn_pending && app.state()==ReaderApp::State::Reading) {
            TEST_ASSERT_EQUAL(target,app.displayed_page());TEST_ASSERT_TRUE_MESSAGE(app.error().empty(),app.error().c_str());
            ++turns;turn_pending=false;
        }
        if(!turn_pending && uint32_t(millis()-last_turn)>=1000) {
            last_turn=millis();turn_pending=true;
            if(app.displayed_page()+1<app.page_count()) { target=app.displayed_page()+1;app.next_page(); }
            else { target=app.displayed_page()-1;app.previous_page(); }
            app.service();
        }
    }
    if(turn_pending) { wait_page(target);++turns; }
    uint32_t elapsed=millis()-start,frames=app.display_device().frame_count()-last_frames;
    uint32_t final_internal=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    Serial.printf("READER SOAK RESULT: %u turns, %.2f Hz, internal %u -> %u, PSRAM %u, reading_ms %llu\n",turns,double(frames)*1000/elapsed,initial_internal,final_internal,(unsigned)ESP.getFreePsram(),(unsigned long long)app.statistics().reading_ms);
    // Throughput is measured, not a universal 1 MHz card requirement. Every
    // turn includes a verified progress transaction and synchronous SD latency.
    TEST_ASSERT_GREATER_OR_EQUAL(10,turns);TEST_ASSERT_TRUE(double(frames)*1000/elapsed>25);
    TEST_ASSERT_TRUE(final_internal+8192>=initial_internal);TEST_ASSERT_TRUE(app.statistics().completed);
}
void setup() { Serial.begin(115200);delay(2000);UNITY_BEGIN();RUN_TEST(test_reader_extract_decode_resume_cancel_and_one_minute_stability);UNITY_END(); }
void loop() { delay(100); }
