#include <cstring>
#include <vector>

#include "check.h"
#include "yucca/switch_pro.h"

using namespace yucca::switchpro;

static const uint8_t kAddr[6] = {0x98, 0xb6, 0xe9, 0x12, 0x34, 0x56};
static const uint8_t kNeutral[8] = {0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};

// Output report 0x01 with neutral rumble and a subcommand.
static std::vector<uint8_t> sub(uint8_t id, std::vector<uint8_t> args = {})
{
    static uint8_t counter = 0;
    std::vector<uint8_t> p = {0x01, uint8_t(counter++ & 0x0f)};
    p.insert(p.end(), kNeutral, kNeutral + 8);
    p.push_back(id);
    p.insert(p.end(), args.begin(), args.end());
    p.resize(49, 0);
    return p;
}

static bool send(Protocol &pro, const std::vector<uint8_t> &p, uint8_t *reply)
{
    return pro.handle_output(p.data(), p.size(), reply);
}

static void decode_stick(const uint8_t *b, int *x, int *y)
{
    *x = b[0] | (b[1] & 0x0f) << 8;
    *y = (b[1] >> 4) | b[2] << 4;
}

static std::vector<uint8_t> spi_read(Protocol &pro, uint32_t addr, uint8_t len)
{
    uint8_t r[kReportSizeMcu];
    CHECK(send(pro, sub(0x10, {uint8_t(addr), uint8_t(addr >> 8), uint8_t(addr >> 16), uint8_t(addr >> 24), len}), r));
    CHECK(r[0] == 0x21 && r[13] == 0x90 && r[14] == 0x10);
    const uint8_t n = len > 0x1d ? 0x1d : len;
    CHECK(r[15] == uint8_t(addr) && r[16] == uint8_t(addr >> 8) && r[19] == n);
    return std::vector<uint8_t>(r + 20, r + 20 + n);
}

// The console's handshake, in the order of a real capture.
static void test_handshake()
{
    Protocol pro(kAddr);
    uint8_t r[kReportSizeMcu];

    // Before the console speaks: a neutral 0x3F hello.
    CHECK(!pro.heard());
    CHECK(pro.build_hello(r) == kHelloSize);
    CHECK(r[0] == 0x3f && r[1] == 0 && r[2] == 0 && r[3] == 0x08);
    CHECK(r[5] == 0x80 && r[7] == 0x80 && r[9] == 0x80 && r[11] == 0x80);

    CHECK(send(pro, sub(0x02), r));
    CHECK(pro.heard() && pro.build_hello(r) == 0);
    pro.reset();
    CHECK(!pro.heard());
    CHECK(send(pro, sub(0x02), r));
    CHECK(r[0] == 0x21 && r[13] == 0x82 && r[14] == 0x02);
    CHECK(r[17] == 0x03);                             // Pro Controller
    CHECK(std::memcmp(r + 19, kAddr, 6) == 0);        // our address, big-endian
    CHECK(r[2] == 0x80);                              // full battery, Pro, not charging

    CHECK(send(pro, sub(0x08, {0x00}), r));
    CHECK(r[13] == 0x80 && r[14] == 0x08);

    auto serial = spi_read(pro, 0x6000, 0x10);
    CHECK(serial[0] == 0xff);                         // no serial number

    auto cal = spi_read(pro, 0x603d, 0x19);
    int x, y;
    decode_stick(&cal[3], &x, &y);                    // left center
    CHECK(x == 0x800 && y == 0x800);
    decode_stick(&cal[0], &x, &y);                    // left max delta passes the console's check
    CHECK(x >= 0x4d5 && y >= 0x542);
    decode_stick(&cal[9], &x, &y);                    // right center comes first
    CHECK(x == 0x800 && y == 0x800);
    CHECK(cal[18] == 0xff);

    CHECK(pro.build_input(r) == 0);                   // nothing periodic before 03 30
    CHECK(send(pro, sub(0x03, {0x30}), r));
    CHECK(r[13] == 0x80 && r[14] == 0x03);
    CHECK(pro.full_mode());

    CHECK(send(pro, sub(0x04), r));
    CHECK(r[13] == 0x83);

    auto params = spi_read(pro, 0x6080, 0x18);
    CHECK(params[0] == 0x50 && params[1] == 0xfd && params[4] == 0xc6 && params[5] == 0x0f);
    decode_stick(&params[9], &x, &y);
    CHECK(x == 0x096 && y == 0xf33);                  // dead zone, range ratio
    CHECK(spi_read(pro, 0x6098, 0x12) == std::vector<uint8_t>(params.begin() + 6, params.end()));
    auto user = spi_read(pro, 0x8010, 0x18);
    CHECK(user[0] == 0xff && user[1] == 0xff);        // no user calibration
    auto imu = spi_read(pro, 0x6020, 0x18);
    CHECK(imu[6] == 0x00 && imu[7] == 0x40 && imu[18] == 0x3b && imu[19] == 0x34);
    CHECK(spi_read(pro, 0x6000, 0x40).size() == 0x1d);  // length capped

    CHECK(!pro.paired());
    send(pro, sub(0x48, {0x01}), r);
    send(pro, sub(0x40, {0x01}), r);
    send(pro, sub(0x48, {0x01}), r);
    CHECK(pro.vibration_enabled());
    CHECK(send(pro, sub(0x30, {0x01}), r));
    CHECK(pro.paired() && pro.player_leds() == 0x1);
    CHECK(send(pro, sub(0x30, {0x30}), r));           // flashing 1+2
    CHECK(pro.player_leds() == 0x3);

    CHECK(send(pro, sub(0x21, {0x21}), r));
    CHECK(r[13] == 0xa0 && r[15] == 0x01 && r[48] == 0xc8);
    CHECK(send(pro, sub(0x5a), r));                   // unknown: plain ACK, never NACK
    CHECK(r[13] == 0x80 && r[14] == 0x5a);

    CHECK(!pro.disconnect_requested());
    send(pro, sub(0x06, {0x00}), r);
    CHECK(pro.disconnect_requested());

    pro.reset();
    CHECK(!pro.full_mode() && !pro.paired() && !pro.vibration_enabled());
}

static void test_input_report()
{
    Protocol pro(kAddr);
    uint8_t r[kReportSizeMcu];
    send(pro, sub(0x03, {0x30}), r);
    State s;
    s.buttons = kA | kPlus | kZL | kLStick;
    s.lx = stick_from_unit(1.f);
    s.ly = stick_from_unit(-1.f);
    s.rx = 0x123;
    s.ry = 0xabc;
    s.battery = 4;
    s.charging = true;
    pro.set_state(s);
    CHECK(pro.build_input(r) == kReportSize);
    const uint8_t t0 = r[1];
    CHECK(r[0] == 0x30);
    CHECK(r[2] == 0x50);
    CHECK(r[3] == 0x08 && r[4] == 0x0a && r[5] == 0x80);
    int x, y;
    decode_stick(r + 6, &x, &y);
    CHECK(x == 0xe00 && y == 0x200);
    decode_stick(r + 9, &x, &y);
    CHECK(x == 0x123 && y == 0xabc);
    CHECK(r[12] == 0x80);
    CHECK(r[13] == 0 && r[48] == 0);                  // IMU off: zeros
    send(pro, sub(0x40, {0x01}), r);
    pro.build_input(r);
    CHECK(r[1] == uint8_t(t0 + 2));                   // timer counts every report
    CHECK(r[13] == 0x50 && r[14] == 0xfd && r[17] == 0xc6 && r[18] == 0x0f);
    CHECK(r[37] == 0x50);                             // third sample too
    CHECK(stick_from_unit(0.f) == 0x800);
}

static void test_rumble()
{
    Protocol pro(kAddr);
    uint8_t r[kReportSizeMcu];
    std::vector<uint8_t> p = {0x10, 0x00, 0x00, 0x01, 0x40, 0x40, 0x28, 0xc8, 0x81, 0x72};
    CHECK(!pro.handle_output(p.data(), p.size(), r));  // rumble only: no reply
    CHECK_NEAR(pro.rumble_right(), 0.0);               // vibration not enabled yet
    send(pro, sub(0x48, {0x01}), r);
    CHECK_NEAR(pro.rumble_left(), 0.0);
    pro.handle_output(p.data(), p.size(), r);
    CHECK_NEAR(pro.rumble_left(), 0.0);
    CHECK_NEAR(pro.rumble_right(), 1.0);
    const uint8_t mid[4] = {0x00, 0x64, 0x40, 0x40};
    CHECK_NEAR(decode_rumble(mid), 0.5);
    const uint8_t mcu[2] = {0x11, 0x00};
    CHECK(!pro.handle_output(mcu, sizeof(mcu), r));
}

static void test_edges()
{
    Protocol pro(kAddr);
    uint8_t r[kReportSizeMcu];

    // Short packets: nothing read past the end, no reply.
    const uint8_t tiny[9] = {0x01};
    CHECK(!pro.handle_output(tiny, sizeof(tiny), r));
    auto ten = sub(0x02);
    CHECK(!pro.handle_output(ten.data(), 10, r));        // no subcommand byte
    std::vector<uint8_t> spi = sub(0x10, {0x00, 0x60});
    CHECK(pro.handle_output(spi.data(), 13, r));        // SPI read with 2 of 5 args
    CHECK(r[13] == 0x90 && r[19] == 0);

    // Input modes.
    send(pro, sub(0x03, {0x31}), r);
    CHECK(pro.full_mode() && pro.input_mode() == 0x31);
    std::memset(r, 0xee, sizeof(r));
    CHECK(pro.build_input(r) == kReportSizeMcu);
    CHECK(r[0] == 0x31 && r[49] == 0 && r[kReportSizeMcu - 1] == 0);
    send(pro, sub(0x03, {0x3f}), r);
    CHECK(!pro.full_mode() && pro.build_input(r) == 0);
    send(pro, sub(0x03, {0x77}), r);                   // unknown mode: unchanged
    CHECK(pro.input_mode() == 0x3f);

    // IMU and vibration off again.
    send(pro, sub(0x03, {0x30}), r);
    send(pro, sub(0x40, {0x01}), r);
    send(pro, sub(0x40, {0x00}), r);
    pro.build_input(r);
    CHECK(r[13] == 0 && r[14] == 0);
    send(pro, sub(0x48, {0x01}), r);
    const uint8_t buzz[10] = {0x10, 0, 0x00, 0xc8, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40};
    pro.handle_output(buzz, sizeof(buzz), r);
    CHECK(pro.rumble_left() > 0.9f);
    send(pro, sub(0x48, {0x00}), r);
    CHECK_NEAR(pro.rumble_left(), 0.0);

    send(pro, sub(0x06, {0x01}), r);                   // reboot-reconnect, not a disconnect
    CHECK(!pro.disconnect_requested());

    // Battery: odd levels never set the charging bit; above 8 is full.
    State s;
    s.battery = 7;
    pro.set_state(s);
    pro.build_input(r);
    CHECK(r[2] == 0x60);
    s.battery = 200;
    pro.set_state(s);
    pro.build_input(r);
    CHECK(r[2] == 0x80);

    CHECK(stick_from_unit(0.0f / 0.0f) == 0x200);        // NaN: clamped, no UB
    CHECK(stick_from_unit(5.f) == 0xe00);
}

static void test_spi_image()
{
    // A read across regions, and the full calibration images byte for byte.
    CHECK(Protocol::spi_byte(0x6011) == 0xff && Protocol::spi_byte(0x6012) == 0x03);
    CHECK(Protocol::spi_byte(0x601a) == 0xff && Protocol::spi_byte(0x601b) == 0x01);
    const uint8_t cal[0x19] = {0x00, 0x06, 0x60, 0x00, 0x08, 0x80, 0x00, 0x06, 0x60,  // left: max, center, min
                               0x00, 0x08, 0x80, 0x00, 0x06, 0x60, 0x00, 0x06, 0x60,  // right: center, min, max
                               0xff, 0x32, 0x32, 0x32, 0xff, 0xff, 0xff};
    for (uint32_t i = 0; i < sizeof(cal); i++) {
        CHECK(Protocol::spi_byte(0x603d + i) == cal[i]);
    }
    const uint8_t params[0x12] = {0x0f, 0x30, 0x61, 0x96, 0x30, 0xf3, 0xd4, 0x14, 0x54,
                                  0x41, 0x15, 0x54, 0xc7, 0x79, 0x9c, 0x33, 0x36, 0x63};
    for (uint32_t i = 0; i < sizeof(params); i++) {
        CHECK(Protocol::spi_byte(0x6086 + i) == params[i]);
        CHECK(Protocol::spi_byte(0x6098 + i) == params[i]);
    }
    CHECK(Protocol::spi_byte(0x60aa) == 0xff);
    CHECK(Protocol::spi_byte(0x8026) == 0xff);
}

int main()
{
    RUN(test_handshake);
    RUN(test_input_report);
    RUN(test_rumble);
    RUN(test_edges);
    RUN(test_spi_image);
    DONE("switch_pro");
}
