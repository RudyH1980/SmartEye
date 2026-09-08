/**
 * @file lcd_st7701_rgb.cpp
 * @brief ST7701 LCD Controller + RGB Interface Driver Implementation
 * @details Waveshare yaklaşımını takip eder - Manuel SPI + Ayrı RGB panel
 */

#include "lcd_st7701_rgb.hpp"
#include <cstring>
#include "board_config.hpp"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace BoardDrivers::lcd {

using namespace BoardConfig;

constexpr static const char *TAG = "LCD_ST7701_RGB";

// =============================================================================
// STEP 1: Manuel SPI Komut Gönderme Fonksiyonları (Waveshare Tarzı)
// =============================================================================

/**
 * @brief ST7701'e SPI üzerinden komut gönder
 */
static esp_err_t st7701_send_command(spi_device_handle_t spi, uint8_t cmd) {
    spi_transaction_t trans = {};
    trans.length = 0;  // Sadece address (komut) gönder
    trans.rxlength = 0;
    trans.cmd = 0;     // D/C = 0 (command)
    trans.addr = cmd;  // Komut address line'a yazılır

    return spi_device_transmit(spi, &trans);
}

/**
 * @brief ST7701'e SPI üzerinden veri gönder
 */
static esp_err_t st7701_send_data(spi_device_handle_t spi, uint8_t data) {
    spi_transaction_t trans = {};
    trans.length = 0;
    trans.rxlength = 0;
    trans.cmd = 1;      // D/C = 1 (data)
    trans.addr = data;  // Veri address line'a yazılır

    return spi_device_transmit(spi, &trans);
}

// =============================================================================
// STEP 2: ST7701 Init Sequence (Waveshare'den Birebir)
// =============================================================================

/**
 * @brief ST7701 initialization komutlarını gönder (2.8" panel için)
 */
static esp_err_t st7701SendInitCommands(spi_device_handle_t spi) {
    ESP_LOGI(TAG, "Sending ST7701 init commands (Waveshare 2.8inch sequence)...");

// Macro for easier command sending
#define CMD(c) st7701_send_command(spi, c)  // SPI_WriteComm
#define DATA(d) st7701_send_data(spi, d)    // DATA
#define DELAY(ms) vTaskDelay(pdMS_TO_TICKS(ms))

    // Verified ST7701S sequence for the 2.8C (round 480x480) panel, transcribed
    // from stefankn/esp32-s3-lcd-2.8c-template (src/Display_ST7701.cpp), whose
    // pin map and RGB timings match this board exactly. The previous sequence
    // came from a 2.8B (480x640) project and produced a scrambled image.
    CMD(0xFF);
    DATA(0x77);
    DATA(0x01);
    DATA(0x00);
    DATA(0x00);
    DATA(0x13);
    CMD(0xEF);
    DATA(0x08);
    CMD(0xFF);
    DATA(0x77);
    DATA(0x01);
    DATA(0x00);
    DATA(0x00);
    DATA(0x10);
    CMD(0xC0);
    DATA(0x3B);
    DATA(0x00);
    CMD(0xC1);
    DATA(0x10);
    DATA(0x0C);
    CMD(0xC2);
    DATA(0x07);
    DATA(0x0A);
    CMD(0xC7);
    DATA(0x00);
    CMD(0xCC);
    DATA(0x10);
    CMD(0xCD);
    DATA(0x08);
    CMD(0xB0);
    DATA(0x05);
    DATA(0x12);
    DATA(0x98);
    DATA(0x0E);
    DATA(0x0F);
    DATA(0x07);
    DATA(0x07);
    DATA(0x09);
    DATA(0x09);
    DATA(0x23);
    DATA(0x05);
    DATA(0x52);
    DATA(0x0F);
    DATA(0x67);
    DATA(0x2C);
    DATA(0x11);
    CMD(0xB1);
    DATA(0x0B);
    DATA(0x11);
    DATA(0x97);
    DATA(0x0C);
    DATA(0x12);
    DATA(0x06);
    DATA(0x06);
    DATA(0x08);
    DATA(0x08);
    DATA(0x22);
    DATA(0x03);
    DATA(0x51);
    DATA(0x11);
    DATA(0x66);
    DATA(0x2B);
    DATA(0x0F);
    CMD(0xFF);
    DATA(0x77);
    DATA(0x01);
    DATA(0x00);
    DATA(0x00);
    DATA(0x11);
    CMD(0xB0);
    DATA(0x5D);
    CMD(0xB1);
    DATA(0x3E);
    CMD(0xB2);
    DATA(0x81);
    CMD(0xB3);
    DATA(0x80);
    CMD(0xB5);
    DATA(0x4E);
    CMD(0xB7);
    DATA(0x85);
    CMD(0xB8);
    DATA(0x20);
    CMD(0xC1);
    DATA(0x78);
    CMD(0xC2);
    DATA(0x78);
    CMD(0xD0);
    DATA(0x88);
    CMD(0xE0);
    DATA(0x00);
    DATA(0x00);
    DATA(0x02);
    CMD(0xE1);
    DATA(0x06);
    DATA(0x30);
    DATA(0x08);
    DATA(0x30);
    DATA(0x05);
    DATA(0x30);
    DATA(0x07);
    DATA(0x30);
    DATA(0x00);
    DATA(0x33);
    DATA(0x33);
    CMD(0xE2);
    DATA(0x11);
    DATA(0x11);
    DATA(0x33);
    DATA(0x33);
    DATA(0xF4);
    DATA(0x00);
    DATA(0x00);
    DATA(0x00);
    DATA(0xF4);
    DATA(0x00);
    DATA(0x00);
    DATA(0x00);
    CMD(0xE3);
    DATA(0x00);
    DATA(0x00);
    DATA(0x11);
    DATA(0x11);
    CMD(0xE4);
    DATA(0x44);
    DATA(0x44);
    CMD(0xE5);
    DATA(0x0D);
    DATA(0xF5);
    DATA(0x30);
    DATA(0xF0);
    DATA(0x0F);
    DATA(0xF7);
    DATA(0x30);
    DATA(0xF0);
    DATA(0x09);
    DATA(0xF1);
    DATA(0x30);
    DATA(0xF0);
    DATA(0x0B);
    DATA(0xF3);
    DATA(0x30);
    DATA(0xF0);
    CMD(0xE6);
    DATA(0x00);
    DATA(0x00);
    DATA(0x11);
    DATA(0x11);
    CMD(0xE7);
    DATA(0x44);
    DATA(0x44);
    CMD(0xE8);
    DATA(0x0C);
    DATA(0xF4);
    DATA(0x30);
    DATA(0xF0);
    DATA(0x0E);
    DATA(0xF6);
    DATA(0x30);
    DATA(0xF0);
    DATA(0x08);
    DATA(0xF0);
    DATA(0x30);
    DATA(0xF0);
    DATA(0x0A);
    DATA(0xF2);
    DATA(0x30);
    DATA(0xF0);
    CMD(0xE9);
    DATA(0x36);
    DATA(0x01);
    CMD(0xEB);
    DATA(0x00);
    DATA(0x01);
    DATA(0xE4);
    DATA(0xE4);
    DATA(0x44);
    DATA(0x88);
    DATA(0x40);
    CMD(0xED);
    DATA(0xFF);
    DATA(0x10);
    DATA(0xAF);
    DATA(0x76);
    DATA(0x54);
    DATA(0x2B);
    DATA(0xCF);
    DATA(0xFF);
    DATA(0xFF);
    DATA(0xFC);
    DATA(0xB2);
    DATA(0x45);
    DATA(0x67);
    DATA(0xFA);
    DATA(0x01);
    DATA(0xFF);
    CMD(0xEF);
    DATA(0x08);
    DATA(0x08);
    DATA(0x08);
    DATA(0x45);
    DATA(0x3F);
    DATA(0x54);
    CMD(0xFF);
    DATA(0x77);
    DATA(0x01);
    DATA(0x00);
    DATA(0x00);
    DATA(0x00);
    CMD(0x11);
    DELAY(120);
    CMD(0x3A);
    DATA(0x66);
    CMD(0x36);
    DATA(0x00);
    CMD(0x35);
    DATA(0x00);
    CMD(0x29);

#undef CMD
#undef DATA
#undef DELAY

    ESP_LOGI(TAG, "✓ ST7701 init commands sent successfully");
    return ESP_OK;
}

// =============================================================================
// STEP 3: Public API Implementation
// =============================================================================

esp_err_t st7701RgbInit(const ST7701Config &config, ST7701Handle **outHandle) {
    ESP_LOGI(TAG, "=== Initializing ST7701 + RGB (Waveshare Style) ===");

    esp_err_t ret = 0;

    // Allocate handle
    auto *handle = (ST7701Handle *)calloc(1, sizeof(ST7701Handle));

    if (handle == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return ESP_ERR_NO_MEM;
    }

    handle->ioExpander = config.ioExpander;
    handle->width = config.width;
    handle->height = config.height;

    // --- STEP 1: LCD Reset (IO Expander) ---
    // NOT: Waveshare LCD power'ı TCA9554 ile kontrol etmiyor, doğrudan powered
    ESP_LOGI(TAG, "Step 1: LCD Reset via IO Expander (EXIO1 = P0_0)");

    // Reset: LOW -> HIGH (Waveshare sequence)
    ESP_ERROR_CHECK(esp_io_expander_set_level(handle->ioExpander, TCA9554_LCD_RESET_MASK, 0));
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_ERROR_CHECK(esp_io_expander_set_level(handle->ioExpander, TCA9554_LCD_RESET_MASK, 1));
    vTaskDelay(pdMS_TO_TICKS(10));

    // CS Enable (LOW = active) - CRITICAL: EXIO3 = P0_2 NOT P0_1!
    ESP_LOGI(TAG, "Step 2: CS Enable (EXIO3 = P0_2, mask 0x04)");
    ESP_ERROR_CHECK(esp_io_expander_set_level(handle->ioExpander, TCA9554_LCD_CS_MASK, 0));
    vTaskDelay(pdMS_TO_TICKS(10));

    // --- STEP 2: SPI Bus for ST7701 Commands ---
    ESP_LOGI(TAG, "Step 3: Initialize SPI for ST7701 commands");

    spi_bus_config_t busConfig = {};
    busConfig.mosi_io_num = config.spiMosi;
    busConfig.miso_io_num = GPIO_NUM_NC;
    busConfig.sclk_io_num = config.spiSclk;
    busConfig.quadwp_io_num = GPIO_NUM_NC;
    busConfig.quadhd_io_num = GPIO_NUM_NC;
    busConfig.max_transfer_sz = 0;

    vTaskDelay(pdMS_TO_TICKS(100));
    ret = spi_bus_initialize(SPI2_HOST, &busConfig, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {  // INVALID_STATE = already initialized
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        free(handle);
        return ret;
    }

    // SPI Device config (3-wire SPI for ST7701)
    spi_device_interface_config_t devConfig = {};
    devConfig.command_bits = 1;                  // D/C bit
    devConfig.address_bits = 8;                  // Command/data byte
    devConfig.mode = 0;                          // SPI mode 0
    devConfig.clock_speed_hz = 4 * 1000 * 1000;  // 4MHz (Waveshare)
    devConfig.spics_io_num = -1;                 // CS controlled by IO expander
    devConfig.queue_size = 1;

    ret = spi_bus_add_device(SPI2_HOST, &devConfig, &handle->spiDevice);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        free(handle);
        return ret;
    }

    ESP_LOGI(TAG, "✓ SPI configured");

    // --- STEP 3: Send ST7701 Init Commands ---
    ESP_LOGI(TAG, "Step 4: Sending ST7701 init commands");
    ret = st7701SendInitCommands(handle->spiDevice);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ST7701 init failed");
        spi_bus_remove_device(handle->spiDevice);
        free(handle);
        return ret;
    }

    // KRİTİK: CS'i henüz kapatma! Waveshare sırası:
    // 1. ST7701 init (CS enable)
    // 2. RGB panel oluştur
    // 3. RGB panel reset/init
    // 4. CS disable

    // --- STEP 4: Create RGB Panel (Ayrı!) ---
    ESP_LOGI(TAG, "Step 5: Creating RGB panel (separate from ST7701)");

    esp_lcd_rgb_panel_config_t rgbConfig = {};
    rgbConfig.clk_src = LCD_CLK_SRC_PLL240M;
    rgbConfig.timings.pclk_hz = config.pixelClockHz;
    // 2.8C variant: h_res=480, v_res=480 (round panel)
    rgbConfig.timings.h_res = 480;
    rgbConfig.timings.v_res = 480;
    rgbConfig.timings.hsync_pulse_width = 8;
    rgbConfig.timings.hsync_back_porch = 10;
    rgbConfig.timings.hsync_front_porch = 50;
    rgbConfig.timings.vsync_pulse_width = 2;
    rgbConfig.timings.vsync_back_porch = 18;
    rgbConfig.timings.vsync_front_porch = 8;
    rgbConfig.timings.flags.hsync_idle_low = 0;
    rgbConfig.timings.flags.vsync_idle_low = 0;
    rgbConfig.timings.flags.de_idle_high = 0;
    rgbConfig.timings.flags.pclk_active_neg = 0;
    rgbConfig.data_width = 16;
    rgbConfig.in_color_format = LCD_COLOR_FMT_RGB565;
    rgbConfig.out_color_format = LCD_COLOR_FMT_RGB565;
    // Single framebuffer fed through bounce buffers, matching the 2.8C
    // reference. Letting the DMA read the PSRAM framebuffer directly cannot
    // keep up with the scanline here: without a GDMA restart the image drifts
    // sideways, and with one it flickers. The bounce buffers remove the
    // underrun, and LVGL's VSYNC-synced flush (see lvgl_wrapper) the tearing.
    rgbConfig.num_fbs = 1;
    rgbConfig.bounce_buffer_size_px = 20 * 480;
    rgbConfig.dma_burst_size = 64;               // CRITICAL: DMA burst size (replaces psram_trans_align)
    rgbConfig.hsync_gpio_num = LCD_PIN_HSYNC;
    rgbConfig.vsync_gpio_num = LCD_PIN_VSYNC;
    rgbConfig.de_gpio_num = LCD_PIN_DE;
    rgbConfig.pclk_gpio_num = LCD_PIN_PCLK;
    rgbConfig.disp_gpio_num = GPIO_NUM_NC;
    rgbConfig.data_gpio_nums[0] = LCD_PIN_DATA0;
    rgbConfig.data_gpio_nums[1] = LCD_PIN_DATA1;
    rgbConfig.data_gpio_nums[2] = LCD_PIN_DATA2;
    rgbConfig.data_gpio_nums[3] = LCD_PIN_DATA3;
    rgbConfig.data_gpio_nums[4] = LCD_PIN_DATA4;
    rgbConfig.data_gpio_nums[5] = LCD_PIN_DATA5;
    rgbConfig.data_gpio_nums[6] = LCD_PIN_DATA6;
    rgbConfig.data_gpio_nums[7] = LCD_PIN_DATA7;
    rgbConfig.data_gpio_nums[8] = LCD_PIN_DATA8;
    rgbConfig.data_gpio_nums[9] = LCD_PIN_DATA9;
    rgbConfig.data_gpio_nums[10] = LCD_PIN_DATA10;
    rgbConfig.data_gpio_nums[11] = LCD_PIN_DATA11;
    rgbConfig.data_gpio_nums[12] = LCD_PIN_DATA12;
    rgbConfig.data_gpio_nums[13] = LCD_PIN_DATA13;
    rgbConfig.data_gpio_nums[14] = LCD_PIN_DATA14;
    rgbConfig.data_gpio_nums[15] = LCD_PIN_DATA15;
    rgbConfig.flags.fb_in_psram = 1;

    ret = esp_lcd_new_rgb_panel(&rgbConfig, &handle->rgbPanel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel create failed: %s", esp_err_to_name(ret));
        spi_bus_remove_device(handle->spiDevice);
        free(handle);
        return ret;
    }

    ESP_LOGI(TAG, "✓ RGB panel created");

    // --- Step 6: RGB panel reset & init (Waveshare yapar!) ---
    ESP_LOGI(TAG, "Step 6: RGB panel reset & init");
    ret = esp_lcd_panel_reset(handle->rgbPanel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel reset failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(handle->rgbPanel);
        spi_bus_remove_device(handle->spiDevice);
        free(handle);
        return ret;
    }

    ret = esp_lcd_panel_init(handle->rgbPanel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RGB panel init failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(handle->rgbPanel);
        spi_bus_remove_device(handle->spiDevice);
        free(handle);
        return ret;
    }

    ESP_LOGI(TAG, "✓ RGB panel initialized");

    // --- Step 7: CS Disable (RGB init'ten SONRA!) ---
    ESP_LOGI(TAG, "Step 7: CS Disable (EXIO3 = P0_2, mask 0x04 -> HIGH)");
    ret = esp_io_expander_set_level(handle->ioExpander, TCA9554_LCD_CS_MASK, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable CS: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(handle->rgbPanel);
        spi_bus_remove_device(handle->spiDevice);
        free(handle);
        return ret;
    }

    // KRİTİK GECİKME: Waveshare'de backlight açmadan önce 10ms bekleniyor
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "✓ ST7701 + RGB initialization complete (Waveshare style)");

    *outHandle = handle;
    return ESP_OK;
}

esp_err_t st7701RgbGetFrameBuffer(ST7701Handle *handle, uint32_t fbNum, void **frameBuffer) {
    if ((handle == nullptr) || (handle->rgbPanel == nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_lcd_rgb_panel_get_frame_buffer(handle->rgbPanel, fbNum, frameBuffer);
}

esp_err_t st7701RgbDrawBitmap(
    ST7701Handle *handle,
    int xStart,
    int yStart,
    int xEnd,
    int yEnd,
    const void *colorData
) {
    if ((handle == nullptr) || (handle->rgbPanel == nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_lcd_panel_draw_bitmap(handle->rgbPanel, xStart, yStart, xEnd, yEnd, colorData);
}

esp_err_t st7701RgbDisplayOnOff(ST7701Handle *handle, bool on) {
    if ((handle == nullptr) || (handle->rgbPanel == nullptr)) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_lcd_panel_disp_on_off(handle->rgbPanel, on);
}

esp_err_t st7701RgbDel(ST7701Handle *handle) {
    if (handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (handle->spiDevice != nullptr) {
        spi_bus_remove_device(handle->spiDevice);
    }

    if (handle->rgbPanel != nullptr) {
        esp_lcd_panel_del(handle->rgbPanel);
    }

    free(handle);
    return ESP_OK;
}

}  // namespace BoardDrivers::lcd
