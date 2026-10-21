/**
 * @file test_lcd_vsync.cpp
 * @brief Step 3: ICanvas::waitForVSync delegates to the Board tearing-sync object through L4.
 */
#include "minitest.hpp"
#include "fakes/fake_board.hpp"
#include "L4_drivers/devices/lcd_st7789.hpp"
#include "L4_drivers/devices/internal_flash_config_store.hpp"

using namespace fakes;

TEST(lcd_waits_on_tearing_sync_when_provided) {
    FakeSPIBus spi; FakeGPIO cs, dc, nreset, te; FakeTearingSync sync;
    Drivers::LCD lcd(spi, cs, dc, nreset, te, &sync);
    Drivers::ICanvas& canvas = lcd;
    canvas.waitForVSync();
    canvas.waitForVSync();
    EXPECT_EQ(sync.waits, 2);
}

// Phase 4 step 3: a streamed bitmap never holds SPI1 between chunks
TEST(bitmap_stream_locks_the_bus_per_chunk_and_continues_with_ramwrc) {
    FakeSPIBus spi; FakeGPIO cs, dc, nreset, te;
    Drivers::LCD lcd(spi, cs, dc, nreset, te);
    lcd.streamBitmapStart(0, 0, 4, 1);
    EXPECT_EQ(spi.locks, 1);
    EXPECT_EQ(spi.unlocks, 1);                       // the window set-up released the bus
    EXPECT_TRUE(lcd.isStreaming());
    const uint8_t px[8] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0};
    const size_t before = spi.tx.size();
    lcd.streamBitmapData(px, 4);
    EXPECT_EQ(spi.locks, 2);
    EXPECT_EQ(spi.unlocks, 2);                       // free again after the chunk
    EXPECT_EQ(spi.tx[before], 0x3C);                 // RAMWRC, then the pixels as given
    for (size_t i = 0; i < 4; i++) EXPECT_EQ(spi.tx[before + 1 + i], px[i]);
    lcd.streamBitmapData(px + 4, 4);
    EXPECT_EQ(spi.locks, 3);
    EXPECT_EQ(spi.unlocks, 3);
    lcd.streamBitmapEnd();
    EXPECT_EQ(spi.locks, 3);                         // nothing left to release
    EXPECT_FALSE(lcd.isStreaming());
}

TEST(lcd_init_turns_the_picture_180_degrees_and_says_so) {
    FakeSPIBus spi; FakeGPIO cs, dc, nreset, te;
    Drivers::LCD lcd(spi, cs, dc, nreset, te);
    lcd.init();
    bool madctl = false;
    for (size_t i = 0; i + 1 < spi.tx.size(); i++) {
        if (spi.tx[i] == 0x36 && spi.tx[i + 1] == 0x88) madctl = true;
    }
    EXPECT_TRUE(madctl);                                  // MY + BGR
    // Inversion off (0x20) after the colour mode: the pixels go out as they are, no copy
    bool invoff = false;
    for (size_t i = 0; i + 2 < spi.tx.size(); i++) {
        if (spi.tx[i] == 0x3A && spi.tx[i + 1] == 0x55 && spi.tx[i + 2] == 0x20) invoff = true;
    }
    EXPECT_TRUE(invoff);
    EXPECT_TRUE(static_cast<Drivers::ICanvas&>(lcd).isRotated180());
    const uint8_t vcom[1] = {0x19};
    EXPECT_TRUE(lcd.writePanelRegister(0xBB, vcom, 1));   // VCOM is tunable
    EXPECT_FALSE(lcd.writePanelRegister(0x36, vcom, 1));  // MADCTL is not
}

TEST(lcd_without_tearing_sync_is_a_no_op) {
    FakeSPIBus spi; FakeGPIO cs, dc, nreset, te;
    Drivers::LCD lcd(spi, cs, dc, nreset, te);
    lcd.waitForVSync();                 // must not dereference null
    EXPECT_TRUE(spi.tx.empty());
}

TEST(config_store_maps_erase_and_write_to_its_sector) {
    FakeInternalFlash flash;
    Drivers::InternalFlashConfigStore store(flash, 0x0807F000u, 7, 64);
    const uint8_t blob[4] = {1, 2, 3, 4};
    EXPECT_TRUE(store.erase());
    EXPECT_EQ(flash.erased, 7);
    EXPECT_TRUE(store.write(blob, sizeof blob));
    EXPECT_EQ(flash.writtenAddr, 0x0807F000u);
    EXPECT_EQ(flash.written.size(), 4u);
    EXPECT_FALSE(store.write(blob, 65));   // over capacity is rejected
}
