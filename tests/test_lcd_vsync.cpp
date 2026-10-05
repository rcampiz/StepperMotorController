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
