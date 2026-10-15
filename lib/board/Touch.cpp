#include "Touch.h"
#include "Display.h"

constexpr uint16_t kProductIdRegister = 0x8140;
constexpr uint16_t kResolutionRegister = 0x8048;
constexpr uint16_t kStatusRegister = 0x814e;
constexpr uint16_t kPointsRegister = 0x814f;
constexpr uint8_t kMaxContacts = 5;
constexpr uint32_t kStalePressMs = 150;
static uint16_t little_endian(const uint8_t *bytes)
{
    return static_cast<uint16_t>(bytes[0]) | (static_cast<uint16_t>(bytes[1]) << 8);
}

bool Touch::read_register(uint16_t reg, uint8_t *bytes, size_t length)
{
    Wire.beginTransmission(address);
    Wire.write(static_cast<uint8_t>(reg >> 8));
    Wire.write(static_cast<uint8_t>(reg));
    if (Wire.endTransmission(false) != 0) {
        return false;
    }
    const size_t received = Wire.requestFrom(address, length, true);
    if (received != length) {
        while (Wire.available()) {
            Wire.read();
        }
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        bytes[i] = static_cast<uint8_t>(Wire.read());
    }
    return true;
}

bool Touch::acknowledge_report()
{
    Wire.beginTransmission(address);
    Wire.write(static_cast<uint8_t>(kStatusRegister >> 8));
    Wire.write(static_cast<uint8_t>(kStatusRegister));
    Wire.write(static_cast<uint8_t>(0));
    return Wire.endTransmission() == 0;
}

bool Touch::detect(char *product)
{
    const uint8_t candidates[] = {0x5d, 0x14};
    for (uint8_t candidate : candidates) {
        address = candidate;
        if (read_register(kProductIdRegister, reinterpret_cast<uint8_t *>(product), 4) &&
            product[0] == '9' && product[1] == '1' && product[2] == '1') {
            product[4] = '\0';
            return true;
        }
    }
    address = 0;
    return false;
}

void Touch::release(const char *reason)
{
    if (current.pressed) {
        Serial.printf("Touch up: %u,%u (%s)\n", current.x, current.y, reason);
    }
    current.pressed = false;
    current.contacts = 0;
    current.contact_id = 0xff;
    active_id = 0xff;
}

void Touch::read_error()
{
    ++current.errors;
    release("invalid report or I2C error");
    if (current.errors == 1 || static_cast<uint32_t>(millis() - last_log_ms) >= 5000) {
        Serial.printf("Touch: invalid report or I2C failure (%lu total)\n",
                      static_cast<unsigned long>(current.errors));
        last_log_ms = millis();
    }
}

bool Touch::map_point(uint16_t x, uint16_t y, lv_point_t &point)
{
    // Reject corrupt coordinates rather than turning them into edge presses.
    if (x >= raw_width || y >= raw_height) {
        return false;
    }
    uint16_t width = raw_width;
    uint16_t height = raw_height;
    if (kSwapXY) {
        const uint16_t old_x = x;
        x = y;
        y = old_x;
        width = raw_height;
        height = raw_width;
    }
    point.x = static_cast<uint32_t>(x) * (Display::kWidth - 1) / (width - 1);
    point.y = static_cast<uint32_t>(y) * (Display::kHeight - 1) / (height - 1);
    if (kInvertX) {
        point.x = Display::kWidth - 1 - point.x;
    }
    if (kInvertY) {
        point.y = Display::kHeight - 1 - point.y;
    }
    return true;
}

void Touch::read(lv_indev_t *input, lv_indev_data_t *data)
{
    auto *self = static_cast<Touch *>(lv_indev_get_user_data(input));
    self->poll(data);
}

void Touch::poll(lv_indev_data_t *data)
{
    uint8_t status = 0;
    if (!read_register(kStatusRegister, &status, 1)) {
        read_error();
    } else if ((status & 0x80) != 0) {
        const uint8_t count = status & 0x0f;
        uint8_t points[kMaxContacts * 8] = {};
        bool valid = count <= kMaxContacts;
        if (valid && count != 0) {
            valid = read_register(kPointsRegister, points, count * 8);
        }
        // Only clear a ready report, including empty releases or malformed packets.
        const bool acknowledged = acknowledge_report();
        if (!valid || !acknowledged) {
            read_error();
        } else if (count == 0) {
            release("empty controller report");
            last_report_ms = millis();
        } else {
            // Follow one contact ID so reordered multi-touch packets cannot jump it.
            const uint8_t wanted = current.pressed ? active_id : points[0];
            const uint8_t *selected = nullptr;
            for (uint8_t i = 0; i < count; ++i) {
                if (points[i * 8] == wanted) {
                    selected = &points[i * 8];
                    break;
                }
            }
            lv_point_t point;
            if (selected == nullptr) {
                release("tracked contact missing");
            } else if (!map_point(little_endian(selected + 1),
                                  little_endian(selected + 3), point)) {
                read_error();
            } else {
                current.x = point.x;
                current.y = point.y;
                if (!current.pressed) {
                    ++current.presses;
                    Serial.printf("Touch down: %u,%u (id %u)\n", current.x, current.y, wanted);
                }
                current.contacts = count;
                current.contact_id = wanted;
                current.pressed = true;
                active_id = wanted;
            }
            last_report_ms = millis();
        }
    } else if (current.pressed &&
               static_cast<uint32_t>(millis() - last_report_ms) >= kStalePressMs) {
        // A lost release or disconnected controller must not leave LVGL pressed.
        release("150 ms stale report");
    }
    data->point.x = current.x;
    data->point.y = current.y;
    data->state = current.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = false;
}

bool Touch::begin(lv_display_t *display)
{
    if (current.available) return display == attached_display;
    if (!display || (owner && owner != this)) return false;
    owner = this;
    attached_display = display;
    if (!Wire.begin(kSdaGpio, kSclGpio, 100000)) {
        Serial.print("Touch unavailable: cannot initialize I2C\n");
        end();
        return false;
    }
    wire_started = true;
    Wire.setTimeOut(20);
    char product[5] = {};
    // The music example uses an already-running controller without reset/config writes.
    if (!detect(product)) {
        // Fallback to the TAMC reset sequence with real INT GPIO18, never uint8_t(-1).
        pinMode(kInterruptGpio, OUTPUT);
        pinMode(kResetGpio, OUTPUT);
        digitalWrite(kInterruptGpio, LOW); // Select 7-bit address 0x5d.
        digitalWrite(kResetGpio, LOW);
        delay(10);
        digitalWrite(kResetGpio, HIGH);
        delay(5);
        digitalWrite(kInterruptGpio, LOW);
        delay(50);
        pinMode(kInterruptGpio, INPUT);
        delay(50);
        if (!detect(product)) {
            Serial.print("Touch unavailable: GT911 not found at 0x5d or 0x14; display remains usable\n");
            end();
            return false;
        }
    }
    uint8_t resolution[4] = {};
    if (!read_register(kResolutionRegister, resolution, sizeof(resolution))) {
        Serial.print("Touch unavailable: cannot read controller resolution\n");
        end();
        return false;
    }
    raw_width = little_endian(resolution);
    raw_height = little_endian(resolution + 2);
    if (raw_width < 2 || raw_height < 2 || raw_width > 4096 || raw_height > 4096 ||
        !acknowledge_report()) {
        Serial.print("Touch unavailable: invalid resolution or status acknowledgement\n");
        end();
        return false;
    }
    input = lv_indev_create();
    if (input == nullptr) {
        Serial.print("Touch unavailable: cannot allocate LVGL pointer\n");
        end();
        return false;
    }
    lv_indev_set_type(input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(input, display);
    lv_indev_set_user_data(input, this);
    lv_indev_set_read_cb(input, read);
    lv_timer_set_period(lv_indev_get_read_timer(input), 16);
    current.available = true;
    Serial.printf("Touch: GT911 %.4s at 0x%02x, SDA%d/SCL%d, raw %ux%u, polling 16 ms\n",
                  product, address, kSdaGpio, kSclGpio, raw_width, raw_height);
    return true;
}

Touch::State Touch::state() const
{
    State result = current;
    if (attached_display) {
        lv_point_t point = {current.x, current.y};
        lv_display_rotate_point(attached_display, &point);
        result.x = point.x;
        result.y = point.y;
    }
    return result;
}

Touch *Touch::owner = nullptr;

void Touch::end()
{
    if (owner != this) return;
    if (input) lv_indev_delete(input);
    input = nullptr;
    if (wire_started) Wire.end();
    wire_started = false;
    attached_display = nullptr;
    address = 0;
    active_id = 0xff;
    raw_width = raw_height = 0;
    last_report_ms = last_log_ms = 0;
    current = State{};
    owner = nullptr;
}
