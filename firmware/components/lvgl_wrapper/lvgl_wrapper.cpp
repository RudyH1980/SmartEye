/**
 * @file lvgl_wrapper.cpp
 * @brief LVGL initialization and driver implementation
 */

#include "lvgl_wrapper.hpp"

#include "board_config.hpp"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "gt911_simple.hpp"

constexpr static const char *TAG = "LVGL_WRAPPER";

namespace lvgl_wrapper {

// Forward declarations for driver callbacks
static void displayFlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap);
static void touchpadReadCb(lv_indev_t *indev, lv_indev_data_t *data);

// Static hardware handles reference (for callbacks)
static BoardDrivers::HardwareHandles sHw = {};

// Frame buffer swaps are held back until the panel signals VSYNC, otherwise
// the swap lands mid-scanout and the screen flickers. sGuiReady says a
// finished frame is waiting; sVsyncEnd releases the flush once VSYNC hit.
static SemaphoreHandle_t sGuiReady = nullptr;
static SemaphoreHandle_t sVsyncEnd = nullptr;

static bool IRAM_ATTR onVsyncEvent(
    esp_lcd_panel_handle_t panel,
    const esp_lcd_rgb_panel_event_data_t *eventData,
    void *userCtx
) {
    LV_UNUSED(panel);
    LV_UNUSED(eventData);
    LV_UNUSED(userCtx);

    BaseType_t highTaskAwoken = pdFALSE;
    if (sGuiReady != nullptr && xSemaphoreTakeFromISR(sGuiReady, &highTaskAwoken) == pdTRUE) {
        xSemaphoreGiveFromISR(sVsyncEnd, &highTaskAwoken);
    }
    return highTaskAwoken == pdTRUE;
}

esp_err_t init(const BoardDrivers::HardwareHandles &hw, LvglHandles &lvglHandles) {
    ESP_LOGI(
        TAG,
        "Initializing LVGL v%d.%d.%d",
        lv_version_major(),
        lv_version_minor(),
        lv_version_patch()
    );

    // Store hardware handles for callbacks
    sHw = hw;

    // 1. Initialize LVGL library
    lv_init();

    // 2. Create LVGL port configuration
    const lvgl_port_cfg_t LVGL_CFG = {
        .task_priority = 4,        // LVGL task priority
        .task_stack = 6144,        // Stack size in bytes
        .task_affinity = -1,       // Core affinity (-1 = no affinity)
        .task_max_sleep_ms = 500,  // Maximum sleep time
        .task_stack_caps = 0,      // Default stack capabilities (MALLOC_CAP_DEFAULT)
        .timer_period_ms = 5       // Timer period (5ms = 200Hz refresh)
    };

    ESP_ERROR_CHECK(lvgl_port_init(&LVGL_CFG));

    // 3. Create LVGL display manually (RGB panels don't work well with lvgl_port_add_disp)
    // Draw buffers live in internal RAM, a band of lines at a time. Putting
    // them in PSRAM instead means every repaint crosses that bus three times
    // over: rendering into it, copying it to the frame buffer, and the panel
    // reading the frame buffer out. That contention is what puts stripes on
    // the screen. Internal RAM takes the first two out of the equation.
    constexpr size_t BUFFER_LINES = 40;
    const size_t BUFFER_SIZE = static_cast<size_t>(BoardConfig::LCD_WIDTH) * BUFFER_LINES;

    void *buf1 = heap_caps_malloc(BUFFER_SIZE * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    void *buf2 = heap_caps_malloc(BUFFER_SIZE * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);

    if (buf1 == nullptr || buf2 == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Allocated LVGL buffers: %zu bytes each", BUFFER_SIZE * sizeof(uint16_t));

    // Hook up VSYNC so the buffer swap waits for the panel
    sGuiReady = xSemaphoreCreateBinary();
    sVsyncEnd = xSemaphoreCreateBinary();
    if (sGuiReady == nullptr || sVsyncEnd == nullptr) {
        ESP_LOGE(TAG, "Failed to create VSYNC semaphores");
        return ESP_ERR_NO_MEM;
    }

    esp_lcd_rgb_panel_event_callbacks_t vsyncCbs = {};
    vsyncCbs.on_vsync = onVsyncEvent;
    ESP_ERROR_CHECK(
        esp_lcd_rgb_panel_register_event_callbacks(hw.lcdHandle->rgbPanel, &vsyncCbs, nullptr)
    );

    // Create LVGL display
    lvglHandles.display = lv_display_create(BoardConfig::LCD_WIDTH, BoardConfig::LCD_HEIGHT);
    if (lvglHandles.display == nullptr) {
        ESP_LOGE(TAG, "Failed to create LVGL display");
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        return ESP_FAIL;
    }

    // Set draw buffers
    lv_display_set_buffers(
        lvglHandles.display,
        buf1,
        buf2,
        BUFFER_SIZE * sizeof(uint16_t),
        LV_DISPLAY_RENDER_MODE_PARTIAL
    );

    // Set color format (RGB565)
    lv_display_set_color_format(lvglHandles.display, LV_COLOR_FORMAT_RGB565);

    // Set flush callback
    lv_display_set_flush_cb(lvglHandles.display, displayFlushCb);

    ESP_LOGI(TAG, "Display created: %dx%d", BoardConfig::LCD_WIDTH, BoardConfig::LCD_HEIGHT);

    // 5. Create touch input device manually (no lvgl_port_add_touch in this version)
    lvglHandles.touchpad = lv_indev_create();
    if (lvglHandles.touchpad == nullptr) {
        ESP_LOGE(TAG, "Failed to create LVGL touch device");
        return ESP_FAIL;
    }

    lv_indev_set_type(lvglHandles.touchpad, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvglHandles.touchpad, touchpadReadCb);
    lv_indev_set_display(lvglHandles.touchpad, lvglHandles.display);

    // LVGL only counts swipe distance while the finger moves faster than
    // gesture_min_velocity per poll, and resets the count otherwise. We poll
    // every 5ms, so each step of a normal swipe is only a pixel or two and the
    // default of 3 wipes the count before a gesture is ever recognised.
    // Zero means the count is never wiped: the touch controller reports at its
    // own rate, so plenty of polls see no movement at all and any non-zero
    // threshold throws the swipe away before it is recognised.
    lv_indev_set_gesture_min_velocity(lvglHandles.touchpad, 0);

    // Short enough that a swipe is recognised while the finger is still
    // moving, long enough that a tap with a bit of drift is still a tap.
    lv_indev_set_gesture_min_distance(lvglHandles.touchpad, 16);

    // Touch is polled on its own timer, 30ms by default. That delay is felt
    // on every gesture, so read it about three times as often.
    lv_timer_set_period(lv_indev_get_read_timer(lvglHandles.touchpad), 30);

    ESP_LOGI(TAG, "Touch input device created");

    // 7. Set default theme
    lv_theme_t *theme = lv_theme_default_init(
        lvglHandles.display,
        lv_palette_main(LV_PALETTE_BLUE),  // Primary color
        lv_palette_main(LV_PALETTE_RED),   // Secondary color
        true,                              // Dark mode
        LV_FONT_DEFAULT                    // Default font
    );
    lv_display_set_theme(lvglHandles.display, theme);

    ESP_LOGI(TAG, "LVGL initialization complete!");

    return ESP_OK;
}

uint32_t getTimerPeriodMs() {
    return 5;  // Match the timer_period_ms from config
}

bool lock(uint32_t timeoutMs) {
    return lvgl_port_lock(timeoutMs);
}

void unlock() {
    lvgl_port_unlock();
}

// ============================================================================
// Private callback functions
// ============================================================================

/**
 * @brief LVGL display flush callback
 * @details Called by LVGL to transfer rendered buffer to display
 */
static void displayFlushCb(lv_display_t *disp, const lv_area_t *area, uint8_t *pxMap) {
    if (sHw.lcdHandle == nullptr || sHw.lcdHandle->rgbPanel == nullptr) {
        ESP_LOGE(TAG, "LCD handle is null in flush callback!");
        lv_display_flush_ready(disp);
        return;
    }

    // Wait for VSYNC once per refresh, not once per band. A full-screen
    // redraw arrives as a dozen bands; syncing on each one blocks this thread
    // for a dozen frame periods, and since touch is polled from this same
    // thread, swipes made during a screen change were simply never seen.
    if (lv_display_flush_is_last(disp)) {
        xSemaphoreGive(sGuiReady);
        xSemaphoreTake(sVsyncEnd, portMAX_DELAY);
    }

    esp_lcd_panel_draw_bitmap(
        sHw.lcdHandle->rgbPanel,
        area->x1,
        area->y1,
        area->x2 + 1,  // LVGL uses inclusive coordinates
        area->y2 + 1,
        pxMap
    );

    // Notify LVGL that flush is complete
    lv_display_flush_ready(disp);
}

/**
 * @brief LVGL touch input read callback
 * @details Called by LVGL to read touch input state
 */
static void touchpadReadCb(lv_indev_t *indev, lv_indev_data_t *data) {
    if (sHw.touch == nullptr) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    gt911::TouchPoint touchPoints[gt911::MAX_TOUCH_POINTS];
    uint8_t numTouches = 0;

    // The last reported contact is kept, because the controller only produces
    // a fresh sample every so often. Between those, nothing has changed: the
    // finger is still wherever it was. Reporting a release there would tear
    // every drag into a handful of separate touches, and no swipe would ever
    // travel far enough to count as one.
    static lv_point_t lastPoint = {0, 0};
    static lv_indev_state_t lastState = LV_INDEV_STATE_RELEASED;
    static uint32_t lastFreshTick = 0;

    // How long a contact may be held without a fresh sample. Long enough to
    // bridge the gaps between the controller's own samples, short enough that
    // a release which never produces a closing sample cannot wedge the input.
    constexpr uint32_t HOLD_TIMEOUT_MS = 60;

    // Touch shares its bus with the IMU, RTC and IO expander, each read from
    // a different task. Without this lock the transactions overlap and wedge
    // the bus, after which touch is dead until a reboot.
    if (!BoardDrivers::i2cLock(20)) {
        data->point = lastPoint;
        data->state = lastState;
        return;
    }

    const esp_err_t ret =
        gt911::readTouchData(sHw.touch, touchPoints, gt911::MAX_TOUCH_POINTS, &numTouches);

    BoardDrivers::i2cUnlock();


    if (ret != ESP_OK) {
        // A handful of failures in a row means the bus is stuck rather than
        // merely busy, and it will stay that way until it is clocked free.
        static int failures = 0;
        if (++failures >= 8) {
            failures = 0;
            if (BoardDrivers::i2cLock(50)) {
                BoardDrivers::i2cRecover();
                BoardDrivers::i2cUnlock();
            }
        }

        lastState = LV_INDEV_STATE_RELEASED;
        data->state = lastState;
        return;
    }

    if (numTouches > 0) {

        lastFreshTick = lv_tick_get();
        lastPoint.x = touchPoints[0].x;
        lastPoint.y = touchPoints[0].y;
        lastState = LV_INDEV_STATE_PRESSED;
    } else if ((lastState == LV_INDEV_STATE_PRESSED) &&
               (lv_tick_elaps(lastFreshTick) <= HOLD_TIMEOUT_MS)) {
        // A short gap between reported samples is the controller's own
        // cadence, not the finger leaving. Bridging it keeps a drag in one
        // piece; without this a swipe arrives as several stubby touches and
        // never travels far enough to be recognised.
        data->point = lastPoint;
        data->state = lastState;
        return;
    } else {
        lastState = LV_INDEV_STATE_RELEASED;
    }

    data->point = lastPoint;
    data->state = lastState;
}

}  // namespace lvgl_wrapper
