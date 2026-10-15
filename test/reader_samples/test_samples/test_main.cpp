#include <unity.h>
#include <ReaderApp.h>
#include "fixtures/reader_fixtures.h"
using namespace reader;
static ReaderApp app;
static File file;
static const char *folder="/Reader samples",*marker="/Reader samples/reader-owned-v1";
static const uint8_t owner[]={0x43,0x42,0x5a,0x31};
static bool matches(const std::string &path,const uint8_t *bytes,size_t size) {
    file=app.storage().open(path.c_str());bool ok=file && file.size()==size;uint8_t buffer[1024];
    for(size_t offset=0;ok && offset<size;) {size_t n=std::min<size_t>(sizeof(buffer),size-offset);ok=file.read(buffer,n)==int(n) && !memcmp(buffer,bytes+offset,n);offset+=n;app.service();}
    file.close();return ok;
}
static void clean_abandoned_fixtures() {
    // Interrupted tests can leave directories. Claim one only when its entire
    // contents are exactly the two generated fixture books, verified bytewise.
    while(app.browser_busy()) app.service();
    auto &card=app.storage();File root=card.open("/");
    for(File entry=root.openNextFile();entry;entry=root.openNextFile()) {
        std::string name=reader::basename(entry.name());bool candidate=entry.isDirectory() && name.find("reader-test-")==0;entry.close();
        if(!candidate) continue;
        std::string path="/"+name;File dir=card.open(path.c_str());unsigned count=0;bool known=true;
        for(File item=dir.openNextFile();item;item=dir.openNextFile()) {std::string base=reader::basename(item.name());known=known && !item.isDirectory() && (base=="Numbered.cbz" || base=="Second.cbz");++count;item.close();}
        dir.close();
        if(known && count==2 && matches(path+"/Numbered.cbz",fixtures::ordered,sizeof(fixtures::ordered)) && matches(path+"/Second.cbz",fixtures::second,sizeof(fixtures::second))) {
            bool ok=card.remove((path+"/Numbered.cbz").c_str()) && card.remove((path+"/Second.cbz").c_str()) && card.rmdir(path.c_str());
            Serial.printf("Abandoned synthetic fixture cleanup: %s %s\n",path.c_str(),ok?"removed":"incomplete");
        }
    }
    root.close();
}
static void sample(const char *path,const uint8_t *bytes,size_t size) {
    auto &card=app.storage();
    if(card.exists(path)) {
        file=card.open(path);TEST_ASSERT_TRUE(file);TEST_ASSERT_EQUAL(size,file.size());
        uint8_t buffer[1024];
        for(size_t offset=0;offset<size;) {size_t n=std::min<size_t>(sizeof(buffer),size-offset);TEST_ASSERT_EQUAL(n,file.read(buffer,n));TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes+offset,buffer,n);offset+=n;app.service();}
        file.close();return;
    }
    file=card.open(path,FILE_WRITE);TEST_ASSERT_TRUE(file);
    for(size_t offset=0;offset<size;) {size_t n=std::min<size_t>(4096,size-offset);TEST_ASSERT_EQUAL(n,file.write(bytes+offset,n));offset+=n;app.service();}
    file.close();
}
void setUp() { TEST_ASSERT_TRUE(app.begin());TEST_ASSERT_TRUE(app.storage().is_mounted()); }
void tearDown() { file.close();app.refresh();app.service(); }
static void test_provision_synthetic_books_for_manual_validation() {
    auto &card=app.storage();
    clean_abandoned_fixtures();
    if(card.exists(folder)) {file=card.open(marker);uint8_t value[4]={};TEST_ASSERT_TRUE(file);TEST_ASSERT_EQUAL(4,file.size());TEST_ASSERT_EQUAL(4,file.read(value,4));file.close();TEST_ASSERT_EQUAL_UINT8_ARRAY(owner,value,4);}
    else {TEST_ASSERT_TRUE(card.mkdir(folder));sample(marker,owner,sizeof(owner));}
    sample("/Reader samples/Numbered.cbz",fixtures::ordered,sizeof(fixtures::ordered));
    sample("/Reader samples/Second.cbz",fixtures::second,sizeof(fixtures::second));
    sample("/Reader samples/Corrupt page.cbz",fixtures::corrupt_page,sizeof(fixtures::corrupt_page));
    sample("/Reader samples/Long book.cbz",fixtures::long_book,sizeof(fixtures::long_book));
    sample("/Reader samples/Unsupported.cbr",reinterpret_cast<const uint8_t *>("CBR support coming later\n"),25);
    Serial.println("MANUAL FIXTURES READY: /Reader samples; synthetic books intentionally retained for controls and power-cycle checks.");
}
void setup() { Serial.begin(115200);delay(2000);UNITY_BEGIN();RUN_TEST(test_provision_synthetic_books_for_manual_validation);UNITY_END(); }
void loop() { app.service(); }
