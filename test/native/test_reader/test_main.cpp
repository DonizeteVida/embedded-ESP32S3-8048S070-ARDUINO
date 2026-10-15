#include <unity.h>
#include <Display.h>
#include <Touch.h>
#include <SdCard.h>
#include <ReaderTypes.h>
#include <DirectoryBrowser.h>
#include <ComicExtractor.h>
#include <PageDecoder.h>
#include <ReadingStatsStore.h>
#include <ReaderApp.h>
#include <JobPump.h>
#include "fixtures/reader_fixtures.h"
using namespace reader;
static SdCard card;
static ComicExtractor extractor(card);
static PageDecoder decoder(card);
static ReadingStatsStore store(card);
static DirectoryBrowser directory(card);
static ReaderApp app;
static File file;
static void put(const char *path,const uint8_t *data,size_t size) {
    file=card.open(path,FILE_WRITE);TEST_ASSERT_TRUE(file);
    TEST_ASSERT_EQUAL(size,file.write(data,size));file.close();
}
static void finish_extraction() {
    for (unsigned i=0;i<10000 && extractor.busy();++i) extractor.step();
    TEST_ASSERT_FALSE(extractor.busy());
}
void setUp() {
    app.end();file.close();directory.close();decoder.release();extractor.close();card.end();
    fake::reset_core();fake::reset_lcd();fake::reset_wire();fake::reset_sd();
    fake::on_delay=[](uint32_t) { fake::frame(); };
    TEST_ASSERT_TRUE(card.begin());
}
void tearDown() {
    app.end();file.close();directory.close();decoder.release();extractor.close();card.end();
    TEST_ASSERT_EQUAL(0,fake::open_files);TEST_ASSERT_EQUAL(fake::allocations,fake::frees);
}
static void paths_natural_order_and_fit() {
    TEST_ASSERT_EQUAL_STRING("/books/issue.cbz",canonical_path("//books/a/.././issue.cbz").c_str());
    TEST_ASSERT_TRUE(natural_less("chapter2/2.jpg","chapter2/10.jpg"));
    TEST_ASSERT_TRUE(natural_less("chapter2/1.jpg","chapter10/1.jpg"));
    TEST_ASSERT_TRUE(safe_archive_path("chapter/01.JPG"));
    for (const char *path:{"../page.jpg","/page.jpg","C:/page.jpg","a/../page.jpg","a\\page.jpg","a/./page.jpg"}) TEST_ASSERT_FALSE(safe_archive_path(path));
    TEST_ASSERT_FALSE(image_name("__MACOSX/._1.jpg"));TEST_ASSERT_FALSE(image_name("chapter/._2.jpg"));
    TEST_ASSERT_TRUE(image_name("chapter/2.PNG"));
    auto fit=fit_page(1000,2000);TEST_ASSERT_EQUAL(400,fit.width);TEST_ASSERT_EQUAL(800,fit.height);TEST_ASSERT_EQUAL(40,fit.x);
    fit=fit_page(2000,1000);TEST_ASSERT_EQUAL(480,fit.width);TEST_ASSERT_EQUAL(240,fit.height);TEST_ASSERT_EQUAL(280,fit.y);
    TEST_ASSERT_EQUAL(0,fit_page(0,10).width);
}
static void bounded_directory_and_reserved_storage() {
    std::string error;TEST_ASSERT_TRUE(prepare_private_storage(card,error));
    TEST_ASSERT_TRUE(card.mkdir("/books"));
    for (int i=0;i<47;++i) { auto path="/item"+std::to_string(i)+".txt";put(path.c_str(),reinterpret_cast<const uint8_t *>("x"),1); }
    TEST_ASSERT_TRUE(directory.open("/"));while (directory.busy()) directory.step();
    TEST_ASSERT_EQUAL(48,directory.total());TEST_ASSERT_EQUAL(10,directory.count());
    for (unsigned i=0;i<directory.count();++i) TEST_ASSERT_NOT_EQUAL(0,strcmp(".comic-reader",directory.entry(i).name.c_str()));
    TEST_ASSERT_TRUE(directory.open("/",40));while (directory.busy()) directory.step();TEST_ASSERT_EQUAL(8,directory.count());
    TEST_ASSERT_TRUE(directory.open("/books"));while (directory.busy()) directory.step();TEST_ASSERT_EQUAL(0,directory.total());
    std::string long_name;for(unsigned i=0;i<120;++i) long_name+="本";long_name+=".cbz";
    put(("/books/"+long_name).c_str(),reinterpret_cast<const uint8_t *>("x"),1);
    TEST_ASSERT_TRUE(directory.open("/books"));while(directory.busy()) directory.step();
    TEST_ASSERT_EQUAL_STRING(long_name.c_str(),directory.entry(0).name.c_str());
    TEST_ASSERT_FALSE(directory.open("/missing"));
}
static void extraction_sort_and_real_image_decoders() {
    put("/comic.cbz",fixtures::ordered,sizeof(fixtures::ordered));
    TEST_ASSERT_TRUE(extractor.start("/comic.cbz"));finish_extraction();
    TEST_ASSERT_EQUAL(int(ComicExtractor::Phase::Ready),int(extractor.phase()));
    TEST_ASSERT_EQUAL(3,extractor.pages().size());
    TEST_ASSERT_EQUAL_STRING("chapter/1.jpg",extractor.pages()[0].name);
    TEST_ASSERT_EQUAL_STRING("chapter/2.png",extractor.pages()[1].name);
    TEST_ASSERT_EQUAL_STRING("chapter/10.jpg",extractor.pages()[2].name);
    std::string first_key=extractor.key();TEST_ASSERT_EQUAL(32,first_key.size());
    TEST_ASSERT_TRUE_MESSAGE(decoder.decode(extractor.pages()[0]),decoder.error().c_str());
    uint16_t red=decoder.pixels()[500*480+240];TEST_ASSERT_TRUE((red>>11)>((red>>5)&63)/2);
    TEST_ASSERT_TRUE_MESSAGE(decoder.decode(extractor.pages()[1]),decoder.error().c_str());
    uint16_t green=decoder.pixels()[500*480+240];TEST_ASSERT_TRUE(((green>>5)&63)>(green>>11));
    TEST_ASSERT_TRUE_MESSAGE(decoder.decode(extractor.pages()[2]),decoder.error().c_str());
    uint16_t blue=decoder.pixels()[500*480+240];TEST_ASSERT_TRUE((blue&31)>(blue>>11));
    put("/other.cbz",fixtures::second,sizeof(fixtures::second));
    TEST_ASSERT_TRUE(extractor.start("/other.cbz"));finish_extraction();
    TEST_ASSERT_EQUAL(2,extractor.pages().size());TEST_ASSERT_NOT_EQUAL(0,strcmp(first_key.c_str(),extractor.key().c_str()));
}
static void corrupt_unsafe_archives_and_cancellation() {
    const uint8_t *data[]={fixtures::unsafe,fixtures::empty,fixtures::bad_crc,fixtures::encrypted,fixtures::truncated,fixtures::zip64,fixtures::unsupported_method};
    const size_t sizes[]={sizeof(fixtures::unsafe),sizeof(fixtures::empty),sizeof(fixtures::bad_crc),sizeof(fixtures::encrypted),sizeof(fixtures::truncated),sizeof(fixtures::zip64),sizeof(fixtures::unsupported_method)};
    put("/escape.jpg",reinterpret_cast<const uint8_t *>("keep"),4);
    for (unsigned i=0;i<7;++i) {
        put("/bad.cbz",data[i],sizes[i]);TEST_ASSERT_TRUE(extractor.start("/bad.cbz"));finish_extraction();
        TEST_ASSERT_EQUAL_MESSAGE(int(ComicExtractor::Phase::Failed),int(extractor.phase()),extractor.error().c_str());
        TEST_ASSERT_FALSE(extractor.error().empty());TEST_ASSERT_TRUE(card.exists("/escape.jpg"));
        File cache=card.open(kCache);File entry=cache.openNextFile(FILE_READ);TEST_ASSERT_FALSE(entry);entry.close();cache.close();
    }
    put("/good.cbz",fixtures::ordered,sizeof(fixtures::ordered));TEST_ASSERT_TRUE(extractor.start("/good.cbz"));
    for (unsigned i=0;i<1000 && extractor.done()==0;++i) extractor.step();
    TEST_ASSERT_GREATER_THAN(0,extractor.done());extractor.cancel();finish_extraction();
    TEST_ASSERT_EQUAL(int(ComicExtractor::Phase::Cancelled),int(extractor.phase()));
    TEST_ASSERT_EQUAL(0,extractor.pages().size());
}
static void cache_ownership_space_and_memory_failures() {
    TEST_ASSERT_TRUE(card.mkdir(kRoot));put("/.comic-reader/user.txt",reinterpret_cast<const uint8_t *>("keep"),4);
    put("/good.cbz",fixtures::ordered,sizeof(fixtures::ordered));
    TEST_ASSERT_FALSE(extractor.start("/good.cbz"));TEST_ASSERT_TRUE(card.exists("/.comic-reader/user.txt"));
    TEST_ASSERT_TRUE(card.remove("/.comic-reader/user.txt"));TEST_ASSERT_TRUE(card.rmdir(kRoot));
    fake::capacity=100000;TEST_ASSERT_TRUE(extractor.start("/good.cbz"));finish_extraction();TEST_ASSERT_EQUAL(int(ComicExtractor::Phase::Failed),int(extractor.phase()));
    fake::capacity=8ULL*1024*1024*1024;TEST_ASSERT_TRUE(extractor.start("/good.cbz"));fake::allocation_failure=true;
    finish_extraction();fake::allocation_failure=false;TEST_ASSERT_EQUAL(int(ComicExtractor::Phase::Failed),int(extractor.phase()));
}
static void statistics_backup_temporary_recovery_and_identity() {
    std::string error;TEST_ASSERT_TRUE(prepare_private_storage(card,error));
    std::string key=book_key("/a.cbz",123);ReadingStats s;s.total_pages=3;s.source_bytes=100;s.opening_count=1;s.reading_ms=5000;
    TEST_ASSERT_TRUE(store.save("/a.cbz",key,s));TEST_ASSERT_EQUAL(1,s.generation);
    s.last_page=1;TEST_ASSERT_TRUE(store.save("/a.cbz",key,s));TEST_ASSERT_EQUAL(2,s.generation);
    ReadingStats loaded;TEST_ASSERT_TRUE(store.load(key,loaded));TEST_ASSERT_EQUAL(1,loaded.last_page);
    auto latest=std::string(kStats)+"/"+key+".bak";put(latest.c_str(),reinterpret_cast<const uint8_t *>("broken"),6);
    TEST_ASSERT_TRUE(store.load(key,loaded));TEST_ASSERT_EQUAL(0,loaded.last_page);
    s=loaded;s.last_page=2;s.completed=true;fake::rename_error=true;TEST_ASSERT_FALSE(store.save("/a.cbz",key,s));
    fake::rename_error=false;TEST_ASSERT_TRUE(store.load(key,loaded));TEST_ASSERT_EQUAL(2,loaded.last_page);TEST_ASSERT_TRUE(loaded.completed);
    TEST_ASSERT_TRUE(store.save("/a.cbz",key,loaded));TEST_ASSERT_TRUE(store.for_path("/a.cbz",100,s));TEST_ASSERT_EQUAL(2,s.last_page);
    TEST_ASSERT_FALSE(store.for_path("/a.cbz",101,s));TEST_ASSERT_FALSE(store.for_path("/renamed.cbz",100,s));
    TEST_ASSERT_FALSE(store.load(book_key("/a.cbz",124),s));
}
static void initialization_retry_and_extraction_io_errors() {
    put("/good.cbz",fixtures::ordered,sizeof(fixtures::ordered));fake::write_error=true;
    TEST_ASSERT_FALSE(extractor.start("/good.cbz"));fake::write_error=false;TEST_ASSERT_FALSE(card.exists(kRoot));
    TEST_ASSERT_TRUE(extractor.start("/good.cbz"));
    while(extractor.busy() && extractor.phase()!=ComicExtractor::Phase::Extracting) extractor.step();
    fake::write_error=true;finish_extraction();fake::write_error=false;
    TEST_ASSERT_EQUAL(int(ComicExtractor::Phase::Failed),int(extractor.phase()));
    TEST_ASSERT_TRUE(extractor.start("/good.cbz"));
    while(extractor.busy() && extractor.phase()!=ComicExtractor::Phase::Extracting) extractor.step();
    fake::read_error=true;finish_extraction();fake::read_error=false;
    TEST_ASSERT_EQUAL(int(ComicExtractor::Phase::Failed),int(extractor.phase()));
}
static void failed_page_does_not_publish_staging() {
    put("/badpage.cbz",fixtures::corrupt_page,sizeof(fixtures::corrupt_page));TEST_ASSERT_TRUE(extractor.start("/badpage.cbz"));finish_extraction();
    TEST_ASSERT_TRUE(decoder.decode(extractor.pages()[0]));auto *front=decoder.pixels();uint16_t pixel=front[500*480+240];
    TEST_ASSERT_FALSE(decoder.decode(extractor.pages()[1]));TEST_ASSERT_EQUAL_PTR(front,decoder.pixels());TEST_ASSERT_EQUAL(pixel,decoder.pixels()[500*480+240]);
    put("/progressive.cbz",fixtures::progressive,sizeof(fixtures::progressive));TEST_ASSERT_TRUE(extractor.start("/progressive.cbz"));finish_extraction();
    TEST_ASSERT_FALSE(decoder.decode(extractor.pages()[0]));TEST_ASSERT_NOT_NULL(strstr(decoder.error().c_str(),"Progressive"));
}
static void png_width_interlace_crc_and_size_limits() {
    put("/wide.cbz",fixtures::wide_png,sizeof(fixtures::wide_png));TEST_ASSERT_TRUE(extractor.start("/wide.cbz"));finish_extraction();
    TEST_ASSERT_TRUE_MESSAGE(decoder.decode(extractor.pages()[0]),decoder.error().c_str());
    uint16_t center=decoder.pixels()[399*480+240];TEST_ASSERT_GREATER_THAN(0,(center>>5)&63);
    const uint8_t *data[]={fixtures::oversize_png,fixtures::interlaced,fixtures::corrupt_png};
    const size_t sizes[]={sizeof(fixtures::oversize_png),sizeof(fixtures::interlaced),sizeof(fixtures::corrupt_png)};
    auto *front=decoder.pixels();
    for(unsigned i=0;i<3;++i) {
        put("/png.cbz",data[i],sizes[i]);TEST_ASSERT_TRUE(extractor.start("/png.cbz"));finish_extraction();
        TEST_ASSERT_FALSE(decoder.decode(extractor.pages()[0]));TEST_ASSERT_EQUAL_PTR(front,decoder.pixels());
    }
}
static bool wait_app(ReaderApp::State state,unsigned max=20000) {
    for (unsigned i=0;i<max;++i) { app.service();if (app.state()==state && (state!=ReaderApp::State::Browser || !app.browser_busy())) return true; }
    return false;
}
static void app_resume_reextract_and_multiple_books() {
    put("/a.cbz",fixtures::ordered,sizeof(fixtures::ordered));put("/b.cbz",fixtures::second,sizeof(fixtures::second));card.end();
    TEST_ASSERT_TRUE(app.begin());TEST_ASSERT_TRUE(wait_app(ReaderApp::State::Browser));
    app.open_book("/a.cbz");TEST_ASSERT_TRUE(wait_app(ReaderApp::State::Reading));TEST_ASSERT_EQUAL(0,app.displayed_page());
    app.next_page();app.service();TEST_ASSERT_EQUAL(1,app.displayed_page());
    app.back();TEST_ASSERT_TRUE(wait_app(ReaderApp::State::Browser));
    app.open_book("/b.cbz");TEST_ASSERT_TRUE(wait_app(ReaderApp::State::Reading));TEST_ASSERT_EQUAL(0,app.displayed_page());
    app.back();TEST_ASSERT_TRUE(wait_app(ReaderApp::State::Browser));app.end();
    TEST_ASSERT_TRUE(app.begin());TEST_ASSERT_TRUE(wait_app(ReaderApp::State::Browser));
    app.open_book("/a.cbz");TEST_ASSERT_TRUE(wait_app(ReaderApp::State::Reading));
    TEST_ASSERT_EQUAL(1,app.displayed_page());TEST_ASSERT_EQUAL(2,app.statistics().opening_count);
    TEST_ASSERT_GREATER_THAN(0,app.statistics().reading_ms);
}
static void panel_copy_failure_preserves_saved_page() {
    put("/copy.cbz",fixtures::ordered,sizeof(fixtures::ordered));card.end();
    TEST_ASSERT_TRUE(app.begin());app.open_book("/copy.cbz");TEST_ASSERT_TRUE(wait_app(ReaderApp::State::Reading));
    set_job_pump([](void *) { if(app.state()==ReaderApp::State::Decoding) fake::copy_error=ESP_FAIL;return true; },nullptr);
    app.next_page();app.service();
    TEST_ASSERT_EQUAL(int(ReaderApp::State::Stopped),int(app.state()));TEST_ASSERT_EQUAL(0,app.displayed_page());
    ReadingStatsStore persisted(app.storage());ReadingStats saved;
    TEST_ASSERT_TRUE(persisted.for_path("/copy.cbz",sizeof(fixtures::ordered),saved));TEST_ASSERT_EQUAL(0,saved.last_page);
    TEST_ASSERT_EQUAL(LOW,fake::pins[2]);
}
static void failed_page_skip_and_failed_save_warning() {
    put("/broken.cbz",fixtures::corrupt_page,sizeof(fixtures::corrupt_page));card.end();
    TEST_ASSERT_TRUE(app.begin());app.open_book("/broken.cbz");TEST_ASSERT_TRUE(wait_app(ReaderApp::State::Reading));
    app.next_page();app.service();TEST_ASSERT_EQUAL(0,app.displayed_page());TEST_ASSERT_FALSE(app.error().empty());
    app.next_page();app.service();TEST_ASSERT_EQUAL(2,app.displayed_page());TEST_ASSERT_TRUE(app.error().empty());
    TEST_ASSERT_TRUE(app.statistics().completed);fake::write_error=true;
    app.back();TEST_ASSERT_TRUE(wait_app(ReaderApp::State::Browser));
    TEST_ASSERT_FALSE(app.error().empty());TEST_ASSERT_GREATER_THAN(0,lv_obj_get_child_count(lv_layer_top()));
    fake::write_error=false;
}
int main() {
    UNITY_BEGIN();RUN_TEST(paths_natural_order_and_fit);RUN_TEST(bounded_directory_and_reserved_storage);
    RUN_TEST(extraction_sort_and_real_image_decoders);RUN_TEST(corrupt_unsafe_archives_and_cancellation);
    RUN_TEST(cache_ownership_space_and_memory_failures);RUN_TEST(statistics_backup_temporary_recovery_and_identity);
    RUN_TEST(initialization_retry_and_extraction_io_errors);
    RUN_TEST(failed_page_does_not_publish_staging);RUN_TEST(png_width_interlace_crc_and_size_limits);RUN_TEST(app_resume_reextract_and_multiple_books);
    RUN_TEST(panel_copy_failure_preserves_saved_page);
    RUN_TEST(failed_page_skip_and_failed_save_warning);
    return UNITY_END();
}
