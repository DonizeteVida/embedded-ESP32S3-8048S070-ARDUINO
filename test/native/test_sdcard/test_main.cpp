#include <unity.h>
#include <SdCard.h>
static SdCard card;
static File file;
void setUp() { file.close(); card.end(); fake::reset_core(); fake::reset_sd(); }
void tearDown() { file.close(); card.end(); TEST_ASSERT_EQUAL(0,fake::open_files); TEST_ASSERT_EQUAL(fake::sd_begins,fake::sd_ends); TEST_ASSERT_EQUAL(fake::spi_begins,fake::spi_ends); }
static void mount_and_lifecycle() {
    fake::root_entries=20; TEST_ASSERT_TRUE(card.begin()); TEST_ASSERT_TRUE(card.is_mounted());
    TEST_ASSERT_FALSE(fake::format_requested); TEST_ASSERT_EQUAL_STRING("SD: SDHC/SDXC | 8192 MiB | SPI 1 MHz",card.status_text());
    const int pins[]={12,13,11,10}; TEST_ASSERT_EQUAL_INT_ARRAY(pins,fake::spi_pins.data(),4);
    TEST_ASSERT_EQUAL(0,fake::open_files); TEST_ASSERT_TRUE(card.begin()); TEST_ASSERT_EQUAL(1,fake::sd_begins);
    SdCard other; TEST_ASSERT_FALSE(other.begin()); other.end(); TEST_ASSERT_TRUE(card.is_mounted());
    card.end(); card.end(); TEST_ASSERT_FALSE(card.is_mounted()); TEST_ASSERT_EQUAL(HIGH,fake::pins[10]);
    TEST_ASSERT_EQUAL_STRING("SD: unavailable - see serial",card.status_text()); TEST_ASSERT_TRUE(card.begin());
}
static void mount_failures_and_retry() {
    fake::sd_ok=false; TEST_ASSERT_FALSE(card.begin()); fake::sd_ok=true;
    fake::card_type=CARD_NONE; TEST_ASSERT_FALSE(card.begin()); fake::card_type=CARD_SDHC;
    fake::root_ok=false; TEST_ASSERT_FALSE(card.begin()); fake::root_ok=true;
    fake::root_directory=false; TEST_ASSERT_FALSE(card.begin()); fake::root_directory=true;
    TEST_ASSERT_FALSE(card.is_mounted()); TEST_ASSERT_FALSE(fake::format_requested); TEST_ASSERT_TRUE(card.begin());
}
static void checked_files_and_failures() {
    TEST_ASSERT_FALSE(card.open("/test")); TEST_ASSERT_FALSE(card.exists("/test")); TEST_ASSERT_FALSE(card.remove("/test"));
    TEST_ASSERT_TRUE(card.begin()); TEST_ASSERT_FALSE(card.open(nullptr)); TEST_ASSERT_FALSE(card.open("/test",nullptr)); TEST_ASSERT_FALSE(card.exists(nullptr)); TEST_ASSERT_FALSE(card.remove(nullptr));
    TEST_ASSERT_FALSE(card.open("/missing"));
    const uint8_t bytes[]={0,1,0xff,0x55}; file=card.open("/test",FILE_WRITE); TEST_ASSERT_TRUE(file); TEST_ASSERT_EQUAL(sizeof(bytes),file.write(bytes,sizeof(bytes))); file.close();
    TEST_ASSERT_TRUE(card.exists("/test")); file=card.open("/test"); uint8_t read[4]; TEST_ASSERT_EQUAL(4,file.read(read,4)); TEST_ASSERT_EQUAL_UINT8_ARRAY(bytes,read,4);
    fake::read_error=true; TEST_ASSERT_EQUAL(-1,file.read(read,4)); file.close();
    fake::open_error=true; TEST_ASSERT_FALSE(card.open("/test")); fake::open_error=false;
    file=card.open("/test",FILE_WRITE); fake::write_error=true; TEST_ASSERT_EQUAL(0,file.write(bytes,4)); file.close();
    fake::remove_error=true; TEST_ASSERT_FALSE(card.remove("/test")); fake::remove_error=false; TEST_ASSERT_TRUE(card.remove("/test")); TEST_ASSERT_FALSE(card.exists("/test"));
}
int main() { UNITY_BEGIN(); RUN_TEST(mount_and_lifecycle); RUN_TEST(mount_failures_and_retry); RUN_TEST(checked_files_and_failures); return UNITY_END(); }
