/**
 * @file test.cpp
 * @brief Test functions implementation for ESP32-S3 Touch LCD 2.8B
 */

#include "test.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include "driver/ledc.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_config.hpp"
#include "gt911_simple.hpp"

#include "imu_services.hpp"
#include "lvgl.h"
#include "lvgl_wrapper.hpp"
#include "power_manager.hpp"
#include "rtc_services.hpp"
#include "sd_services.hpp"

constexpr static const char *TAG = "TESTS";

namespace tests {

void fillScreen(const BoardDrivers::HardwareHandles &hw, uint16_t color) {
    if (hw.lcdHandle == nullptr || hw.lcdHandle->rgbPanel == nullptr) {
        ESP_LOGE(TAG, "LCD Panel handle null!");
        return;
    }

    // Get frame buffer
    void *fb = nullptr;

    if (esp_err_t ret = BoardDrivers::lcd::st7701RgbGetFrameBuffer(hw.lcdHandle, 1, &fb);
        ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get frame buffer: %s", esp_err_to_name(ret));
        return;
    }

    // Fill frame buffer with color
    auto *fb16 = (uint16_t *)fb;
    const size_t TOTAL_PIXELS = BoardConfig::LCD_WIDTH * BoardConfig::LCD_HEIGHT;

    for (size_t i = 0; i < TOTAL_PIXELS; i++) {
        fb16[i] = color;
    }

    // Trigger RGB DMA update
    BoardDrivers::lcd::st7701RgbDrawBitmap(
        hw.lcdHandle,
        0,
        0,
        BoardConfig::LCD_WIDTH,
        BoardConfig::LCD_HEIGHT,
        fb
    );
}

void setBacklight(const BoardDrivers::HardwareHandles &hw, uint8_t brightness) {
    if (hw.backlightChannel == 0xFF) {
        ESP_LOGW(TAG, "Backlight channel not initialized");
        return;
    }

    uint32_t duty = (BoardConfig::LCD_BACKLIGHT_DUTY_MAX * brightness) / 100;
    ledc_set_duty(BoardConfig::LEDC_MODE, hw.backlightChannel, duty);
    ledc_update_duty(BoardConfig::LEDC_MODE, hw.backlightChannel);
}

void drawDot(
    const BoardDrivers::HardwareHandles &hw,
    int16_t x,
    int16_t y,
    int size,
    uint16_t color
) {
    // Boundary checking
    if (x < 0 || x >= BoardConfig::LCD_WIDTH || y < 0 || y >= BoardConfig::LCD_HEIGHT) {
        return;
    }

    int halfSize = size / 2;

    // Calculate bounding box
    int xStart = x - halfSize;
    int yStart = y - halfSize;
    int xEnd = x + halfSize + 1;
    int yEnd = y + halfSize + 1;

    // Clamp to screen bounds
    if (xStart < 0) {
        xStart = 0;
    }
    if (yStart < 0) {
        yStart = 0;
    }
    if (xEnd > BoardConfig::LCD_WIDTH) {
        xEnd = BoardConfig::LCD_WIDTH;
    }
    if (yEnd > BoardConfig::LCD_HEIGHT) {
        yEnd = BoardConfig::LCD_HEIGHT;
    }

    // Create local buffer for the dot (like LVGL does)
    int width = xEnd - xStart;
    int height = yEnd - yStart;
    uint16_t dotBuffer[width * height];

    // Fill dot buffer
    for (int i = 0; i < width * height; i++) {
        dotBuffer[i] = color;
    }

    // Send to LCD using esp_lcd_panel_draw_bitmap directly (like LVGL flush)
    esp_lcd_panel_draw_bitmap(hw.lcdHandle->rgbPanel, xStart, yStart, xEnd, yEnd, dotBuffer);
}

void drawLine(
    const BoardDrivers::HardwareHandles &hw,
    int16_t x0,
    int16_t y0,
    int16_t x1,
    int16_t y1,
    int size,
    uint16_t color
) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = std::max(std::abs(dx), std::abs(dy));

    if (steps == 0) {
        drawDot(hw, x0, y0, size, color);
        return;
    }

    float xInc = static_cast<float>(dx) / steps;
    float yInc = static_cast<float>(dy) / steps;

    for (int i = 0; i <= steps; i++) {
        int x = x0 + static_cast<int>(i * xInc);
        int y = y0 + static_cast<int>(i * yInc);
        drawDot(hw, x, y, size, color);
    }
}

void testCornerSquares(const BoardDrivers::HardwareHandles &hw) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST: Drawing 4 corner squares");
    ESP_LOGI(TAG, "========================================");

    // Get frame buffer
    void *fb = nullptr;
    if (esp_err_t ret = BoardDrivers::lcd::st7701RgbGetFrameBuffer(hw.lcdHandle, 1, &fb);
        ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get frame buffer!");
        return;
    }

    auto *fb16 = (uint16_t *)fb;
    const int WIDTH = BoardConfig::LCD_WIDTH;    // 480
    const int HEIGHT = BoardConfig::LCD_HEIGHT;  // 640

    // Clear frame buffer to WHITE
    ESP_LOGI(TAG, "Clearing frame buffer to WHITE");
    for (int i = 0; i < WIDTH * HEIGHT; i++) {
        fb16[i] = 0xFFFF;  // White background
    }

    // 4 CORNER SQUARES - 100x100 each, anchored to the actual panel size
    const int SQ = 100;
    const int RIGHT = WIDTH - SQ;
    const int BOTTOM = HEIGHT - SQ;

    ESP_LOGI(TAG, "Drawing RED at (0,0)");
    for (int y = 0; y < SQ; y++) {
        for (int x = 0; x < SQ; x++) {
            fb16[y * WIDTH + x] = 0xF800;
        }
    }

    ESP_LOGI(TAG, "Drawing GREEN at (%d,0)", RIGHT);
    for (int y = 0; y < SQ; y++) {
        for (int x = RIGHT; x < WIDTH; x++) {
            fb16[y * WIDTH + x] = 0x07E0;
        }
    }

    ESP_LOGI(TAG, "Drawing BLUE at (0,%d)", BOTTOM);
    for (int y = BOTTOM; y < HEIGHT; y++) {
        for (int x = 0; x < SQ; x++) {
            fb16[y * WIDTH + x] = 0x001F;
        }
    }

    ESP_LOGI(TAG, "Drawing YELLOW at (%d,%d)", RIGHT, BOTTOM);
    for (int y = BOTTOM; y < HEIGHT; y++) {
        for (int x = RIGHT; x < WIDTH; x++) {
            fb16[y * WIDTH + x] = 0xFFE0;
        }
    }

    // Flush entire frame buffer
    ESP_LOGI(TAG, "Flushing frame buffer");
    BoardDrivers::lcd::st7701RgbDrawBitmap(hw.lcdHandle, 0, 0, WIDTH, HEIGHT, fb);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "4 Corner Squares Displayed:");
    ESP_LOGI(TAG, "  RED    at (0,0) - top-left");
    ESP_LOGI(TAG, "  GREEN  at (%d,0) - top-right", RIGHT);
    ESP_LOGI(TAG, "  BLUE   at (0,%d) - bottom-left", BOTTOM);
    ESP_LOGI(TAG, "  YELLOW at (%d,%d) - bottom-right", RIGHT, BOTTOM);
    ESP_LOGI(TAG, "========================================");
}

[[noreturn]] void demoTouchDrawing(const BoardDrivers::HardwareHandles &hw) {
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Touch Drawing Demo");
    ESP_LOGI(TAG, "Touch the screen to draw with white pen");
    ESP_LOGI(TAG, "========================================");

    // Drawing parameters
    int16_t lastX = -1;
    int16_t lastY = -1;
    const int DOT_SIZE = 6;              // Smaller dot for smoother drawing
    const uint16_t DRAW_COLOR = 0xFFFF;  // White

    while (true) {
        gt911::TouchPoint touchPoints[gt911::MAX_TOUCH_POINTS];
        uint8_t numTouches = 0;

        // Read touch data
        if (gt911::readTouchData(hw.touch, touchPoints, gt911::MAX_TOUCH_POINTS, &numTouches) ==
            ESP_OK) {
            if (numTouches > 0) {
                // Use first touch point
                auto touchX = static_cast<int16_t>(touchPoints[0].x);
                auto touchY = static_cast<int16_t>(touchPoints[0].y);

                // If we have a previous position, draw line to connect
                if (lastX >= 0 && lastY >= 0) {
                    // Always draw line between points for smooth continuous drawing
                    drawLine(hw, lastX, lastY, touchX, touchY, DOT_SIZE, DRAW_COLOR);
                } else {
                    // First touch - just draw a dot
                    drawDot(hw, touchX, touchY, DOT_SIZE, DRAW_COLOR);
                }

                lastX = touchX;
                lastY = touchY;

                // Shorter delay when touch is active for faster response
                vTaskDelay(pdMS_TO_TICKS(2));  // 500Hz when touching
            } else {
                // No touch - reset last position
                lastX = -1;
                lastY = -1;

                // Longer delay when no touch to save CPU
                vTaskDelay(pdMS_TO_TICKS(10));  // 100Hz when idle
            }
        } else {
            // Read error - small delay
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
}

// ============================================================================
// IMU DEMO
// ============================================================================

[[noreturn]] void demoIMU(const BoardDrivers::HardwareHandles &hw) {
    ESP_LOGI(TAG, "Starting IMU Demo...");

    auto *imu = (qmi8658_dev_t *)hw.imu;
    if (imu == nullptr) {
        ESP_LOGE(TAG, "IMU not initialized!");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    ESP_LOGI(TAG, "IMU Demo started - Press Ctrl+C to stop");
    ESP_LOGI(TAG, "Testing all 4 IMU services:");
    ESP_LOGI(TAG, "  1. Screen Orientation");
    ESP_LOGI(TAG, "  2. Motion Detection (shake/free-fall)");
    ESP_LOGI(TAG, "  3. Gaming Control (tilt)");
    ESP_LOGI(TAG, "  4. Pedometer (steps)");

    // Start motion detection with callback
    ImuServices::setMotionCallback([](const ImuServices::MotionEvent &event) {
        const char *type = (event.type == ImuServices::MotionType::SHAKE) ? "SHAKE" : "FREE_FALL";
        ESP_LOGW(TAG, ">>> Motion detected: %s (intensity: %.2f)", type, event.intensity);
    });
    ImuServices::startMotionDetection(imu, 50);

    // Start pedometer
    ImuServices::startPedometer(imu, 50);

    uint32_t loopCount = 0;

    while (true) {
        // 1. Screen Orientation (every 500ms)
        if (loopCount % 5 == 0) {
            ImuServices::ScreenOrientation orientation = ImuServices::getScreenOrientation(imu);
            const char *orientName = "Unknown";
            switch (orientation) {
                case ImuServices::ScreenOrientation::PORTRAIT:
                    orientName = "Portrait";
                    break;
                case ImuServices::ScreenOrientation::LANDSCAPE_RIGHT:
                    orientName = "Landscape Right";
                    break;
                case ImuServices::ScreenOrientation::PORTRAIT_INVERTED:
                    orientName = "Portrait Inverted";
                    break;
                case ImuServices::ScreenOrientation::LANDSCAPE_LEFT:
                    orientName = "Landscape Left";
                    break;
            }
            ESP_LOGI(TAG, "[Orientation] %s", orientName);
        }

        // 2. Gaming Control - Tilt (every 100ms)
        ImuServices::TiltAngles tilt = ImuServices::getTiltAngles(imu);
        float tiltX = ImuServices::getTiltX(imu, 45.0);
        float tiltY = ImuServices::getTiltY(imu, 45.0);

        ESP_LOGI(
            TAG,
            "[Tilt] Pitch: %6.2f°, Roll: %6.2f°  |  Normalized X: %5.2f, Y: %5.2f",
            tilt.pitch,
            tilt.roll,
            tiltX,
            tiltY
        );

        // 3. Pedometer (every 1000ms)
        if (loopCount % 10 == 0) {
            uint32_t steps = ImuServices::getStepCount();
            float distance = ImuServices::getDistance(0.75);
            ESP_LOGI(TAG, "[Pedometer] Steps: %lu, Distance: %.2f m", steps, distance);
        }

        // Raw sensor data (every 100ms)
        float accelX;
        float accelY;
        float accelZ;
        float gyroX;
        float gyroY;
        float gyroZ;
        if (qmi8658_read_accel(imu, &accelX, &accelY, &accelZ) == ESP_OK &&
            qmi8658_read_gyro(imu, &gyroX, &gyroY, &gyroZ) == ESP_OK) {
            ESP_LOGI(
                TAG,
                "[Raw] Accel: X=%6.2f Y=%6.2f Z=%6.2f m/s²  |  Gyro: X=%5.2f Y=%5.2f Z=%5.2f rad/s",
                accelX,
                accelY,
                accelZ,
                gyroX,
                gyroY,
                gyroZ
            );
        }

        loopCount++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ============================================================================
// IMU DEMO WITH GUI
// ============================================================================

[[noreturn]] void demoIMU_GUI(const BoardDrivers::HardwareHandles &hw) {
    ESP_LOGI(TAG, "Starting IMU Demo with GUI...");

    auto *imu = (qmi8658_dev_t *)hw.imu;
    if (imu == nullptr) {
        ESP_LOGE(TAG, "IMU not initialized!");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Create main container
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

    // Title
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "IMU 3D Visualization");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

    // === LEFT PANEL: 3D Cube with Canvas ===
    constexpr int CANVAS_WIDTH = 200;
    constexpr int CANVAS_HEIGHT = 200;

    // Create canvas for 3D cube
    lv_obj_t *canvas = lv_canvas_create(screen);
    lv_obj_align(canvas, LV_ALIGN_TOP_LEFT, 20, 60);

    // Allocate canvas buffer in PSRAM (RGB565 = 2 bytes per pixel)
    // Total: 200*200*2 = 80KB (much smaller than 240*300*2 = 144KB)
    auto *canvasBuffer = (lv_color_t *)
        heap_caps_malloc(CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);

    if (canvasBuffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate canvas buffer!");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    lv_canvas_set_buffer(canvas, canvasBuffer, CANVAS_WIDTH, CANVAS_HEIGHT, LV_COLOR_FORMAT_RGB565);

    // Initial clear
    lv_canvas_fill_bg(canvas, lv_color_hex(0x101010), LV_OPA_COVER);

    // === RIGHT PANEL: Data ===
    // Orientation Label (compact)
    lv_obj_t *orientLabel = lv_label_create(screen);
    lv_label_set_text(orientLabel, "Orient: --");
    lv_obj_set_style_text_color(orientLabel, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(orientLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(orientLabel, LV_ALIGN_TOP_LEFT, 260, 40);

    // Angle Labels (compact)
    lv_obj_t *angleLabel = lv_label_create(screen);
    lv_label_set_text(angleLabel, "Pitch: 0.0°\nRoll: 0.0°");
    lv_obj_set_style_text_color(angleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(angleLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(angleLabel, LV_ALIGN_TOP_LEFT, 260, 70);

    // Pedometer Label (compact)
    lv_obj_t *pedometerLabel = lv_label_create(screen);
    lv_label_set_text(pedometerLabel, "Steps: 0\nDist: 0.0m");
    lv_obj_set_style_text_color(pedometerLabel, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(pedometerLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(pedometerLabel, LV_ALIGN_TOP_LEFT, 260, 120);

    // Motion Event Label (compact)
    lv_obj_t *motionLabel = lv_label_create(screen);
    lv_label_set_text(motionLabel, "Motion: None");
    lv_obj_set_style_text_color(motionLabel, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_text_font(motionLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(motionLabel, LV_ALIGN_TOP_LEFT, 260, 170);

    // Accel/Gyro Raw Data Label (compact, bottom)
    lv_obj_t *rawDataLabel = lv_label_create(screen);
    lv_label_set_text(rawDataLabel, "Accel Z,Y,X: --\nGyro Z,Y,X: --");
    lv_obj_set_style_text_color(rawDataLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(rawDataLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(rawDataLabel, LV_ALIGN_BOTTOM_LEFT, 10, -10);

    ESP_LOGI(TAG, "IMU 3D GUI Demo started");

    // Cube vertices (8 corners: -1 to +1 range)
    float cubeVerts[8][3] = {
        {-1, -1, -1},
        {1, -1, -1},
        {1, 1, -1},
        {-1, 1, -1},  // Back face
        {-1, -1, 1},
        {1, -1, 1},
        {1, 1, 1},
        {-1, 1, 1}  // Front face
    };

    char buf[128];
    uint32_t frameCounter = 0;  // For throttling label updates

    // Exponential smoothing for accelerometer (reduce jitter)
    static float smoothAccelX = 0.0F;
    static float smoothAccelY = 0.0F;
    static float smoothAccelZ = 0.0F;
    constexpr float SMOOTH_FACTOR = 0.15F;  // Lower = smoother, Higher = more responsive (0.0-1.0)

    while (true) {
        // Read raw accelerometer data (physical device orientation)
        float accelX;
        float accelY;
        float accelZ;
        qmi8658_read_accel(imu, &accelX, &accelY, &accelZ);

        // Apply exponential smoothing to reduce jitter
        smoothAccelX = smoothAccelX * (1.0F - SMOOTH_FACTOR) + accelX * SMOOTH_FACTOR;
        smoothAccelY = smoothAccelY * (1.0F - SMOOTH_FACTOR) + accelY * SMOOTH_FACTOR;
        smoothAccelZ = smoothAccelZ * (1.0F - SMOOTH_FACTOR) + accelZ * SMOOTH_FACTOR;

        // Calculate rotation angles from accelerometer
        // Using proper accelerometer-to-angle formulas
        float roll = -atan2f(
            -smoothAccelY,
            sqrtf(smoothAccelX * smoothAccelX + smoothAccelZ * smoothAccelZ)
        );
        float pitch = atan2f(smoothAccelX, smoothAccelZ);
        float yaw = 0.0F;  // Yaw disabled (gyroscope drift issues)

        // === Calculate 3D Cube Projection ===
        const float COS_PITCH = cosf(pitch);
        const float SIN_PITCH = sinf(pitch);
        const float COS_ROLL = cosf(roll);
        const float SIN_ROLL = sinf(roll);
        const float COS_YAW = cosf(yaw);
        const float SIN_YAW = sinf(yaw);

        // Project 3D points to 2D
        int projected[8][2];
        constexpr float SCALE = 50.0F;
        constexpr int CENTER_X = CANVAS_WIDTH / 2;   // 100
        constexpr int CENTER_Y = CANVAS_HEIGHT / 2;  // 100
        constexpr float PERSPECTIVE = 200.0F;

        for (int i = 0; i < 8; i++) {
            const float VX = cubeVerts[i][0];
            const float VY = cubeVerts[i][1];
            const float VZ = cubeVerts[i][2];

            // Apply rotations: Yaw (Z-axis) → Roll (Y-axis) → Pitch (X-axis)
            // Order: Z → Y → X for proper 3D rotation

            // 1. Rotate around Z axis (yaw - rotation when device is flat on table)
            const float X1 = VX * COS_YAW - VY * SIN_YAW;
            const float Y1 = VX * SIN_YAW + VY * COS_YAW;

            // 2. Rotate around Y axis (roll - side tilt)
            const float X2 = X1 * COS_ROLL - VZ * SIN_ROLL;
            const float Z2 = X1 * SIN_ROLL + VZ * COS_ROLL;

            // 3. Rotate around X axis (pitch - forward/back tilt)
            const float Y3 = Y1 * COS_PITCH - Z2 * SIN_PITCH;
            const float Z3 = Y1 * SIN_PITCH + Z2 * COS_PITCH;

            // 3D to 2D projection with perspective
            const float FACTOR = PERSPECTIVE / (PERSPECTIVE + Z3 * 50);
            projected[i][0] = CENTER_X + static_cast<int>(X2 * SCALE * FACTOR);
            projected[i][1] = CENTER_Y - static_cast<int>(Y3 * SCALE * FACTOR);
        }

        // Bresenham line drawing algorithm
        auto drawLine = [&](int x0, int y0, int x1, int y1, lv_color_t color) {
            int dx = abs(x1 - x0);
            int dy = abs(y1 - y0);
            int sx = (x0 < x1) ? 1 : -1;
            int sy = (y0 < y1) ? 1 : -1;
            int err = dx - dy;

            while (true) {
                // Draw pixel (with bounds check)
                if (x0 >= 0 && x0 < CANVAS_WIDTH && y0 >= 0 && y0 < CANVAS_HEIGHT) {
                    lv_canvas_set_px(canvas, x0, y0, color, LV_OPA_COVER);
                }

                if (x0 == x1 && y0 == y1) {
                    break;
                }

                int e2 = 2 * err;
                if (e2 > -dy) {
                    err -= dy;
                    x0 += sx;
                }
                if (e2 < dx) {
                    err += dx;
                    y0 += sy;
                }
            }
        };

        // === LVGL LOCK - Update canvas and widgets ===
        if (lvgl_wrapper::lock(10)) {
            // Clear canvas buffer directly (much faster than lv_canvas_fill_bg)
            memset(canvasBuffer, 0x10, CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(lv_color_t));

            // Draw cube edges
            // Back face (4 edges) - Red
            lv_color_t redColor = lv_color_hex(0xFF0000);
            for (int i = 0; i < 4; i++) {
                int next = (i + 1) % 4;
                drawLine(
                    projected[i][0],
                    projected[i][1],
                    projected[next][0],
                    projected[next][1],
                    redColor
                );
            }

            // Front face (4 edges) - Green
            lv_color_t greenColor = lv_color_hex(0x00FF00);
            for (int i = 4; i < 8; i++) {
                int next = 4 + ((i - 4 + 1) % 4);
                drawLine(
                    projected[i][0],
                    projected[i][1],
                    projected[next][0],
                    projected[next][1],
                    greenColor
                );
            }

            // Connecting edges (4 edges) - Blue
            lv_color_t blueColor = lv_color_hex(0x0000FF);
            for (int i = 0; i < 4; i++) {
                drawLine(
                    projected[i][0],
                    projected[i][1],
                    projected[i + 4][0],
                    projected[i + 4][1],
                    blueColor
                );
            }

            // Manually invalidate canvas to prevent flickering
            lv_obj_invalidate(canvas);

            // Update labels only every 10 frames (reduce LVGL load)
            if (frameCounter % 10 == 0) {
                // Angles (convert radians to degrees for display)
                const float PITCH_DEG = pitch * 180.0F / M_PI;
                const float ROLL_DEG = roll * 180.0F / M_PI;
                snprintf(buf, sizeof(buf), "Pitch: %.1f°\nRoll: %.1f°", PITCH_DEG, ROLL_DEG);
                lv_label_set_text(angleLabel, buf);

                // Orientation
                ImuServices::ScreenOrientation orientation = ImuServices::getScreenOrientation(imu);
                const char *orientName = "?";
                switch (orientation) {
                    case ImuServices::ScreenOrientation::PORTRAIT:
                        orientName = "Port";
                        break;
                    case ImuServices::ScreenOrientation::LANDSCAPE_RIGHT:
                        orientName = "Land-R";
                        break;
                    case ImuServices::ScreenOrientation::PORTRAIT_INVERTED:
                        orientName = "Port-Inv";
                        break;
                    case ImuServices::ScreenOrientation::LANDSCAPE_LEFT:
                        orientName = "Land-L";
                        break;
                }
                snprintf(buf, sizeof(buf), "Orient: %s", orientName);
                lv_label_set_text(orientLabel, buf);

                // Pedometer (disabled - not used in this demo)
                lv_label_set_text(pedometerLabel, "Steps: --\nDist: --");

                // Raw sensor data
                float accelX;
                float accelY;
                float accelZ;
                float gyroX;
                float gyroY;
                float gyroZ;
                if (qmi8658_read_accel(imu, &accelX, &accelY, &accelZ) == ESP_OK &&
                    qmi8658_read_gyro(imu, &gyroX, &gyroY, &gyroZ) == ESP_OK) {
                    snprintf(
                        buf,
                        sizeof(buf),
                        "Accel Z,Y,X: %.1f,%.1f,%.1f\nGyro Z,Y,X: %.1f,%.1f,%.1f",
                        accelZ,
                        accelY,
                        accelX,
                        gyroZ,
                        gyroY,
                        gyroX
                    );
                    lv_label_set_text(rawDataLabel, buf);
                }
            }

            // Motion detection (disabled - not used in this demo)
            lv_label_set_text(motionLabel, "Motion: --");

            lvgl_wrapper::unlock();
        }

        frameCounter++;
        vTaskDelay(pdMS_TO_TICKS(50));  // 20 FPS for smooth 3D animation
    }
}

// =============================================================================
// RTC CLOCK DEMO
// =============================================================================

[[noreturn]] void demoRTCClock(const BoardDrivers::HardwareHandles &hw) {
    ESP_LOGI(TAG, "Starting RTC Clock Demo...");

    auto *rtc = (pcf85063a_dev_t *)hw.rtc;
    if (rtc == nullptr) {
        ESP_LOGE(TAG, "RTC not initialized!");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Initialize RTC service
    rtc_services::initRTC(rtc);

    // Set initial time (2025-11-03 14:30:00)
    ESP_LOGI(TAG, "Setting RTC time to 2025-11-03 14:30:00");
    rtc_services::setTime(2025, 11, 3, 14, 30, 0);

    // Sync system time from RTC
    rtc_services::syncSystemTimeFromRTC();

    // Create main screen
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

    // Title
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Real-Time Clock");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Time display (large)
    lv_obj_t *timeLabel = lv_label_create(screen);
    lv_label_set_text(timeLabel, "00:00:00");
    lv_obj_set_style_text_color(timeLabel, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_48, 0);
    lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, -60);

    // Date display
    lv_obj_t *dateLabel = lv_label_create(screen);
    lv_label_set_text(dateLabel, "Monday, Jan 1");
    lv_obj_set_style_text_color(dateLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_20, 0);
    lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, 0);

    // Full datetime display (small)
    lv_obj_t *fullDateLabel = lv_label_create(screen);
    lv_label_set_text(fullDateLabel, "2025-01-01 00:00:00");
    lv_obj_set_style_text_color(fullDateLabel, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(fullDateLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(fullDateLabel, LV_ALIGN_CENTER, 0, 60);

    // System uptime
    lv_obj_t *uptimeLabel = lv_label_create(screen);
    lv_label_set_text(uptimeLabel, "Uptime: 0s");
    lv_obj_set_style_text_color(uptimeLabel, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(uptimeLabel, &lv_font_montserrat_14, 0);
    lv_obj_align(uptimeLabel, LV_ALIGN_BOTTOM_MID, 0, -10);

    ESP_LOGI(TAG, "RTC Clock UI created");

    char buf[128];
    uint32_t startTime = xTaskGetTickCount() * portTICK_PERIOD_MS / 1000;

    while (true) {
        // Update every second
        if (lvgl_wrapper::lock(10)) {
            // Get current time from RTC
            uint16_t year = 0;
            uint8_t month;
            uint8_t day;
            uint8_t hour;
            uint8_t min;
            uint8_t sec;
            esp_err_t ret = rtc_services::getTime(&year, &month, &day, &hour, &min, &sec);

            if (ret == ESP_OK) {
                // Update time display (HH:MM:SS)
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d", hour, min, sec);
                lv_label_set_text(timeLabel, buf);

                // Update date display (Day, Mon DD)
                rtc_services::formatDate(buf, sizeof(buf));
                lv_label_set_text(dateLabel, buf);

                // Update full datetime display
                rtc_services::formatTime(buf, sizeof(buf), true);
                lv_label_set_text(fullDateLabel, buf);

                // Update uptime
                uint32_t uptime = (xTaskGetTickCount() * portTICK_PERIOD_MS / 1000) - startTime;
                snprintf(buf, sizeof(buf), "Uptime: %lus", uptime);
                lv_label_set_text(uptimeLabel, buf);
            } else {
                ESP_LOGE(TAG, "Failed to read RTC time");
            }

            lvgl_wrapper::unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));  // Update every second
    }
}

// ============================================================================
// SD CARD DEMO WITH LVGL
// ============================================================================

// Global UI elements for SD card demo
static lv_obj_t *gSdStatusLabel = nullptr;
static lv_obj_t *gSdInfoLabel = nullptr;
static lv_obj_t *gFileListTextarea = nullptr;
static lv_obj_t *gFormatBtn = nullptr;
static lv_obj_t *gCreateFileBtn = nullptr;
static lv_obj_t *gDeleteFileBtn = nullptr;
static lv_obj_t *gRefreshBtn = nullptr;
static lv_obj_t *gMountBtn = nullptr;
static lv_obj_t *gFormatProgressBar = nullptr;
static lv_obj_t *gFormatProgressLabel = nullptr;
static lv_obj_t *gFormatOverlay = nullptr;
static bool gSdMounted = false;

// Update SD card info display
static void updateSDInfo() {
    // Sync mount status from sd_services
    gSdMounted = sd_services::isMounted();

    if (!gSdMounted) {
        lv_label_set_text(gSdInfoLabel, "SD Card: Not mounted");
        lv_obj_add_state(gFormatBtn, LV_STATE_DISABLED);
        lv_obj_add_state(gCreateFileBtn, LV_STATE_DISABLED);
        lv_obj_add_state(gDeleteFileBtn, LV_STATE_DISABLED);
        lv_obj_add_state(gRefreshBtn, LV_STATE_DISABLED);
        return;
    }

    sd_services::SDCardInfo info = {};
    esp_err_t ret = sd_services::getCardInfo(&info);

    if (ret == ESP_OK) {
        char totalStr[32];
        char freeStr[32];
        char usedStr[32];
        sd_services::formatBytes(info.totalBytes, totalStr, sizeof(totalStr));
        sd_services::formatBytes(info.freeBytes, freeStr, sizeof(freeStr));
        sd_services::formatBytes(info.usedBytes, usedStr, sizeof(usedStr));

        uint64_t freePercent = (info.totalBytes > 0) ? (info.freeBytes * 100 / info.totalBytes) : 0;

        char buf[256];
        snprintf(
            buf,
            sizeof(buf),
            "Type: %s\n"
            "Size: %s\n"
            "Free: %s (%llu%%)\n"
            "Used: %s\n"
            "Speed: %lu MHz",
            info.type,
            totalStr,
            freeStr,
            freePercent,
            usedStr,
            info.speedMHz
        );
        lv_label_set_text(gSdInfoLabel, buf);

        // Enable buttons when mounted
        lv_obj_remove_state(gFormatBtn, LV_STATE_DISABLED);
        lv_obj_remove_state(gCreateFileBtn, LV_STATE_DISABLED);
        lv_obj_remove_state(gDeleteFileBtn, LV_STATE_DISABLED);
        lv_obj_remove_state(gRefreshBtn, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(gSdInfoLabel, "Error reading card info");
    }
}

// Update file list display
static void updateFileList() {
    // Sync mount status from sd_services
    gSdMounted = sd_services::isMounted();

    if (!gSdMounted) {
        lv_textarea_set_text(gFileListTextarea, "SD Card not mounted");
        return;
    }

    std::vector<sd_services::FileEntry> files;
    esp_err_t ret = sd_services::listFiles("", files);  // Empty string for root directory

    if (ret != ESP_OK) {
        lv_textarea_set_text(gFileListTextarea, "Error listing files");
        return;
    }

    if (files.empty()) {
        lv_textarea_set_text(gFileListTextarea, "Empty directory");
        return;
    }

    // Build file list string
    char buf[2048] = {0};
    size_t offset = 0;

    for (const auto &file : files) {
        if (offset >= sizeof(buf) - 100) {
            break;  // Prevent buffer overflow
        }

        if (file.isDirectory) {
            offset += snprintf(
                buf + offset,
                sizeof(buf) - offset,
                "[DIR]  %s\n",
                file.name.c_str()
            );
        } else {
            // Format file size
            if (file.size < 1024) {
                offset += snprintf(
                    buf + offset,
                    sizeof(buf) - offset,
                    "%s (%llu B)\n",
                    file.name.c_str(),
                    file.size
                );
            } else if (file.size < 1024 * 1024) {
                offset += snprintf(
                    buf + offset,
                    sizeof(buf) - offset,
                    "%s (%.1f KB)\n",
                    file.name.c_str(),
                    file.size / 1024.0
                );
            } else {
                offset += snprintf(
                    buf + offset,
                    sizeof(buf) - offset,
                    "%s (%.1f MB)\n",
                    file.name.c_str(),
                    file.size / (1024.0 * 1024.0)
                );
            }
        }
    }

    lv_textarea_set_text(gFileListTextarea, buf);
}

// Show format progress overlay
static void showFormatProgress() {
    if (gFormatOverlay != nullptr) {
        return;  // Already showing
    }

    // Create semi-transparent overlay
    gFormatOverlay = lv_obj_create(lv_screen_active());
    lv_obj_set_size(gFormatOverlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(gFormatOverlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(gFormatOverlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(gFormatOverlay, 0, 0);
    lv_obj_clear_flag(gFormatOverlay, LV_OBJ_FLAG_SCROLLABLE);

    // Create progress container
    lv_obj_t *container = lv_obj_create(gFormatOverlay);
    lv_obj_set_size(container, 300, 150);
    lv_obj_center(container);
    lv_obj_set_style_bg_color(container, lv_color_hex(0x202020), 0);

    // Title
    lv_obj_t *title = lv_label_create(container);
    lv_label_set_text(title, "Formatting SD Card");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Progress bar
    gFormatProgressBar = lv_bar_create(container);
    lv_obj_set_size(gFormatProgressBar, 250, 20);
    lv_obj_align(gFormatProgressBar, LV_ALIGN_CENTER, 0, 0);
    lv_bar_set_value(gFormatProgressBar, 0, LV_ANIM_OFF);

    // Progress label
    gFormatProgressLabel = lv_label_create(container);
    lv_label_set_text(gFormatProgressLabel, "0%");
    lv_obj_set_style_text_color(gFormatProgressLabel, lv_color_hex(0xFFFF00), 0);
    lv_obj_align(gFormatProgressLabel, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// Hide format progress overlay
static void hideFormatProgress() {
    if (gFormatOverlay != nullptr) {
        lv_obj_delete(gFormatOverlay);
        gFormatOverlay = nullptr;
        gFormatProgressBar = nullptr;
        gFormatProgressLabel = nullptr;
    }
}

// Format task (runs in separate thread to avoid blocking LVGL)
static void formatTask(void *pvParameters) {
    ESP_LOGI(TAG, "Format task started");

    // Format with progress callback
    esp_err_t ret = sd_services::formatSD([](uint8_t percent) {
        if (lvgl_wrapper::lock(10)) {
            if (gFormatProgressBar != nullptr) {
                lv_bar_set_value(gFormatProgressBar, percent, LV_ANIM_ON);

                char buf[16];
                snprintf(buf, sizeof(buf), "%d%%", percent);
                lv_label_set_text(gFormatProgressLabel, buf);
            }
            lvgl_wrapper::unlock();
        }
    });

    // Small delay to show 100% before hiding
    vTaskDelay(pdMS_TO_TICKS(500));

    // Hide progress overlay and show result
    if (lvgl_wrapper::lock(100)) {
        hideFormatProgress();

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Format complete!");
            lv_label_set_text(gSdStatusLabel, "Format complete!");
            updateSDInfo();
            updateFileList();
        } else {
            ESP_LOGE(TAG, "Format failed: %s", esp_err_to_name(ret));
            lv_label_set_text(gSdStatusLabel, "Format failed!");
        }

        lvgl_wrapper::unlock();
    }

    // Delete task
    vTaskDelete(nullptr);
}

// Format Yes button callback (LVGL v9 style)
static void formatYesCallback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        // Get the msgbox to close it later
        auto *mbox = (lv_obj_t *)lv_event_get_user_data(event);

        ESP_LOGI(TAG, "Starting format...");

        // Close the msgbox first
        lv_obj_delete(mbox);

        // Force LVGL to process the deletion before showing progress
        lv_refr_now(nullptr);

        // Show progress overlay
        showFormatProgress();

        // Start format task (non-blocking)
        xTaskCreate(formatTask, "format_task", 8192, nullptr, 5, nullptr);
    }
}

// Format No button callback (LVGL v9 style)
static void formatNoCallback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        auto *mbox = (lv_obj_t *)lv_event_get_user_data(event);
        lv_obj_delete(mbox);
        lv_label_set_text(gSdStatusLabel, "Format cancelled");
    }
}

// Button event handlers
static void formatBtnCallback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        // Show confirmation dialog (LVGL v9 style)
        lv_obj_t *mbox = lv_msgbox_create(lv_screen_active());
        lv_msgbox_add_title(mbox, "Format SD Card");
        lv_msgbox_add_text(mbox, "This will erase ALL data!\nAre you sure?");

        // Add buttons
        lv_obj_t *btnYes = lv_msgbox_add_footer_button(mbox, "Yes");
        lv_obj_t *btnNo = lv_msgbox_add_footer_button(mbox, "No");

        // Style the Yes button as red
        lv_obj_set_style_bg_color(btnYes, lv_color_hex(0xFF0000), 0);

        // Add callbacks with mbox as user data so we can close it
        lv_obj_add_event_cb(btnYes, formatYesCallback, LV_EVENT_CLICKED, mbox);
        lv_obj_add_event_cb(btnNo, formatNoCallback, LV_EVENT_CLICKED, mbox);

        lv_obj_center(mbox);
    }
}

static void createFileBtnCallback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        const char *testFile = "test.txt";  // 8.3 FAT format: max 8 chars + 3 ext
        const char *testData = "Hello from ESP32-S3!\nThis is a test file.\n";

        esp_err_t ret = sd_services::writeFile(testFile, testData, strlen(testData));

        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Test file created successfully");
            lv_label_set_text(gSdStatusLabel, "Test file created!");
            updateFileList();
            updateSDInfo();
        } else {
            ESP_LOGE(TAG, "Failed to create test file");
            lv_label_set_text(gSdStatusLabel, "Failed to create file");
        }
    }
}

static void deleteFileBtnCallback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        const char *testFile = "test.txt";  // 8.3 FAT format

        if (sd_services::fileExists(testFile)) {
            esp_err_t ret = sd_services::deleteFile(testFile);

            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Test file deleted successfully");
                lv_label_set_text(gSdStatusLabel, "Test file deleted!");
                updateFileList();
                updateSDInfo();
            } else {
                ESP_LOGE(TAG, "Failed to delete test file");
                lv_label_set_text(gSdStatusLabel, "Failed to delete file");
            }
        } else {
            lv_label_set_text(gSdStatusLabel, "test.txt not found");
        }
    }
}

static void refreshBtnCallback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        lv_label_set_text(gSdStatusLabel, "Checking SD card...");

        // Try to mount if not already mounted
        if (!sd_services::isMounted()) {
            esp_err_t ret = sd_services::initSD();
            if (ret == ESP_OK) {
                lv_label_set_text(gSdStatusLabel, "SD Card Mounted!");
                updateSDInfo();
                updateFileList();
            } else {
                lv_label_set_text(gSdStatusLabel, "SD Card not found");
                lv_label_set_text(gSdInfoLabel, "Insert SD card and click Refresh");
            }
        } else {
            // Already mounted, just refresh info
            lv_label_set_text(gSdStatusLabel, "Refreshing...");
            updateSDInfo();
            updateFileList();
            lv_label_set_text(gSdStatusLabel, "Ready");
        }
    }
}

static void mountBtnCallback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        if (gSdMounted) {
            // Unmount
            esp_err_t ret = sd_services::deinitSD();
            if (ret == ESP_OK) {
                gSdMounted = false;
                lv_label_set_text(gSdStatusLabel, "SD Card unmounted");
                lv_label_set_text((lv_obj_t *)lv_event_get_user_data(event), "Mount");
                updateSDInfo();
                updateFileList();
            } else {
                lv_label_set_text(gSdStatusLabel, "Unmount failed!");
            }
        } else {
            // Mount
            lv_label_set_text(gSdStatusLabel, "Mounting SD card...");
            esp_err_t ret = sd_services::initSD();

            if (ret == ESP_OK) {
                gSdMounted = true;
                lv_label_set_text(gSdStatusLabel, "SD Card mounted!");
                lv_label_set_text((lv_obj_t *)lv_event_get_user_data(event), "Unmount");
                updateSDInfo();
                updateFileList();
            } else {
                lv_label_set_text(gSdStatusLabel, "Mount failed! Insert card?");
            }
        }
    }
}

[[noreturn]] void demoSDCard(const BoardDrivers::HardwareHandles &hw) {
    ESP_LOGI(TAG, "=== SD Card Demo Started ===");

    if (!lvgl_wrapper::lock(100)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Create main container
    lv_obj_t *cont = lv_obj_create(lv_screen_active());
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(cont, 10, 0);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // Title
    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "SD CARD DEMO");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF00), 0);

    // Status label
    gSdStatusLabel = lv_label_create(cont);
    lv_label_set_text(gSdStatusLabel, "Ready");
    lv_obj_set_style_text_color(gSdStatusLabel, lv_color_hex(0xFFFF00), 0);

    // Mount/Unmount button
    gMountBtn = lv_button_create(cont);
    lv_obj_set_size(gMountBtn, 120, 40);
    lv_obj_t *mountLabel = lv_label_create(gMountBtn);
    lv_label_set_text(mountLabel, "Mount");
    lv_obj_center(mountLabel);
    lv_obj_add_event_cb(gMountBtn, mountBtnCallback, LV_EVENT_CLICKED, mountLabel);

    // Card info label
    gSdInfoLabel = lv_label_create(cont);
    lv_label_set_text(gSdInfoLabel, "SD Card: Not mounted");
    lv_obj_set_style_text_color(gSdInfoLabel, lv_color_hex(0xFFFFFF), 0);

    // File list area
    lv_obj_t *fileLabel = lv_label_create(cont);
    lv_label_set_text(fileLabel, "Files:");
    lv_obj_set_style_text_color(fileLabel, lv_color_hex(0x00FFFF), 0);

    gFileListTextarea = lv_textarea_create(cont);
    lv_obj_set_size(gFileListTextarea, LV_PCT(95), 150);
    lv_textarea_set_text(gFileListTextarea, "SD Card not mounted");

    // Button row container
    lv_obj_t *btnRow = lv_obj_create(cont);
    lv_obj_set_size(btnRow, LV_PCT(95), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btnRow, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(
        btnRow,
        LV_FLEX_ALIGN_SPACE_EVENLY,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );
    lv_obj_set_style_pad_all(btnRow, 5, 0);

    // Create File button
    gCreateFileBtn = lv_button_create(btnRow);
    lv_obj_set_size(gCreateFileBtn, 100, 40);
    lv_obj_t *createLabel = lv_label_create(gCreateFileBtn);
    lv_label_set_text(createLabel, "Create");
    lv_obj_center(createLabel);
    lv_obj_add_event_cb(gCreateFileBtn, createFileBtnCallback, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_state(gCreateFileBtn, LV_STATE_DISABLED);

    // Delete File button
    gDeleteFileBtn = lv_button_create(btnRow);
    lv_obj_set_size(gDeleteFileBtn, 100, 40);
    lv_obj_t *deleteLabel = lv_label_create(gDeleteFileBtn);
    lv_label_set_text(deleteLabel, "Delete");
    lv_obj_center(deleteLabel);
    lv_obj_add_event_cb(gDeleteFileBtn, deleteFileBtnCallback, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_state(gDeleteFileBtn, LV_STATE_DISABLED);

    // Refresh button (always enabled for manual mount)
    gRefreshBtn = lv_button_create(btnRow);
    lv_obj_set_size(gRefreshBtn, 100, 40);
    lv_obj_t *refreshLabel = lv_label_create(gRefreshBtn);
    lv_label_set_text(refreshLabel, "Refresh");
    lv_obj_center(refreshLabel);
    lv_obj_add_event_cb(gRefreshBtn, refreshBtnCallback, LV_EVENT_CLICKED, nullptr);
    // NOTE: Refresh button is always enabled to allow manual SD card mounting

    // Format button (separate row, warning color)
    gFormatBtn = lv_button_create(cont);
    lv_obj_set_size(gFormatBtn, 120, 40);
    lv_obj_set_style_bg_color(gFormatBtn, lv_color_hex(0xFF0000), 0);
    lv_obj_t *formatLabel = lv_label_create(gFormatBtn);
    lv_label_set_text(formatLabel, "FORMAT");
    lv_obj_center(formatLabel);
    lv_obj_add_event_cb(gFormatBtn, formatBtnCallback, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_state(gFormatBtn, LV_STATE_DISABLED);

    lvgl_wrapper::unlock();

    ESP_LOGI(TAG, "SD Card Demo UI created successfully!");

    // Start auto-detect with callback for UI updates
    sd_services::startAutoDetect([](bool mounted) {
        ESP_LOGI(TAG, "SD Auto-Detect callback: %s", mounted ? "MOUNTED" : "UNMOUNTED");

        // Update UI (thread-safe)
        if (lvgl_wrapper::lock(100)) {
            if (mounted) {
                lv_label_set_text(gSdStatusLabel, "SD Card Auto-Mounted!");
                updateSDInfo();
                updateFileList();
            } else {
                lv_label_set_text(gSdStatusLabel, "SD Card Removed");
                lv_label_set_text(gSdInfoLabel, "SD Card: Not mounted");
                lv_textarea_set_text(gFileListTextarea, "SD Card not mounted");

                // Disable buttons
                lv_obj_add_state(gFormatBtn, LV_STATE_DISABLED);
                lv_obj_add_state(gCreateFileBtn, LV_STATE_DISABLED);
                lv_obj_add_state(gDeleteFileBtn, LV_STATE_DISABLED);
                lv_obj_add_state(gRefreshBtn, LV_STATE_DISABLED);
            }
            lvgl_wrapper::unlock();
        }
    });

    ESP_LOGI(TAG, "SD Auto-Detect started!");

    // Main loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// ========================================
// Power Management Test
// ========================================

// Global power manager instance
static PowerManager gPowerMgr;

// LVGL UI elements for power test
static lv_obj_t *gPowerStatusLabel = nullptr;
static lv_obj_t *gPowerCountdownLabel = nullptr;
static lv_obj_t *gPowerSdStatusLabel = nullptr;  // Renamed to avoid conflict
static lv_obj_t *gWakeupLabel = nullptr;

// SD Card callbacks
static void powerPreSleepCallback() {
    ESP_LOGI(TAG, "[POWER] Pre-sleep callback: Unmounting SD card...");
    if (sd_services::isMounted()) {
        sd_services::deinitSD();
        ESP_LOGI(TAG, "[POWER] SD card unmounted");

        // Update UI
        if (lvgl_wrapper::lock(100)) {
            if (gPowerSdStatusLabel != nullptr) {
                lv_label_set_text(gPowerSdStatusLabel, "SD: Unmounted (sleep)");
            }
            lvgl_wrapper::unlock();
        }
    }
}

static void powerPostWakeupCallback() {
    ESP_LOGI(TAG, "[POWER] Post-wakeup callback: Remounting SD card...");
    esp_err_t ret = sd_services::initSD();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "[POWER] SD card remounted");

        // Update UI
        if (lvgl_wrapper::lock(100)) {
            if (gPowerSdStatusLabel != nullptr) {
                lv_label_set_text(gPowerSdStatusLabel, "SD: Mounted (wakeup)");
            }
            lvgl_wrapper::unlock();
        }
    } else {
        ESP_LOGW(TAG, "[POWER] SD card remount failed: %s", esp_err_to_name(ret));

        if (lvgl_wrapper::lock(100)) {
            if (gPowerSdStatusLabel != nullptr) {
                lv_label_set_text(gPowerSdStatusLabel, "SD: Remount failed");
            }
            lvgl_wrapper::unlock();
        }
    }
}

// Power mode button callbacks
static void activeBtnCallback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "[POWER] Switching to ACTIVE mode");
        gPowerMgr.setMode(PowerManager::PowerMode::ACTIVE);
        if (gPowerStatusLabel != nullptr) {
            lv_label_set_text(gPowerStatusLabel, "Mode: ACTIVE");
        }
        gPowerMgr.resetAutoSleepTimer();
    }
}

static void lowPowerBtnCallback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "[POWER] Switching to LOW_POWER mode");
        gPowerMgr.setMode(PowerManager::PowerMode::LOW_POWER);
        if (gPowerStatusLabel != nullptr) {
            lv_label_set_text(gPowerStatusLabel, "Mode: LOW_POWER (20%)");
        }
        gPowerMgr.resetAutoSleepTimer();
    }
}

static void sleepBtnCallback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "[POWER] Entering SLEEP mode manually");
        if (gPowerStatusLabel != nullptr) {
            lv_label_set_text(gPowerStatusLabel, "Mode: Entering SLEEP...");
        }
        lv_refr_now(nullptr);
        vTaskDelay(pdMS_TO_TICKS(500));
        gPowerMgr.setMode(PowerManager::PowerMode::SLEEP);
        // Returns here after wakeup
        ESP_LOGI(TAG, "[POWER] Woke up from manual sleep");
        if (gPowerStatusLabel != nullptr) {
            lv_label_set_text(gPowerStatusLabel, "Mode: ACTIVE (woke up)");
        }
        gPowerMgr.resetAutoSleepTimer();
    }
}

// Auto-sleep timeout button callbacks
static void timeout10Callback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "[POWER] Auto-sleep timeout set to 10 seconds");
        gPowerMgr.setAutoSleepTimeout(10);
    }
}

static void timeout20Callback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "[POWER] Auto-sleep timeout set to 20 seconds");
        gPowerMgr.setAutoSleepTimeout(20);
    }
}

static void timeout30Callback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "[POWER] Auto-sleep timeout set to 30 seconds");
        gPowerMgr.setAutoSleepTimeout(30);
    }
}

static void timeoutOffCallback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "[POWER] Auto-sleep disabled");
        gPowerMgr.setAutoSleepTimeout(0);
    }
}

// Touch event to reset auto-sleep timer
static void screenTouchCallback(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_PRESSED) {
        gPowerMgr.resetAutoSleepTimer();
        ESP_LOGD(TAG, "[POWER] Auto-sleep timer reset by touch");
    }
}

// Countdown update task
static void countdownUpdateTask(void *pvParameters) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // Update every second

        if (lvgl_wrapper::lock(100)) {
            if (gPowerCountdownLabel != nullptr) {
                uint32_t remaining = gPowerMgr.getTimeUntilAutoSleep();

                if (gPowerMgr.isAutoSleepEnabled()) {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "Auto-sleep in: %lu s", remaining);
                    lv_label_set_text(gPowerCountdownLabel, buf);
                } else {
                    lv_label_set_text(gPowerCountdownLabel, "Auto-sleep: OFF");
                }
            }
            lvgl_wrapper::unlock();
        }
    }
}

[[noreturn]] void testPowerManagement(const BoardDrivers::HardwareHandles &hw) {
    ESP_LOGI(TAG, "=== Power Management Test ===");

    // Initialize power manager
    PowerManager::Config config = {
        .lcdPanel = hw.lcdHandle->rgbPanel,
        .touchHandle = nullptr,  // GT911 custom touch controller - power management not supported
        .backlightChannel = hw.backlightChannel,
        .backlightDutyMax = BoardConfig::LCD_BACKLIGHT_DUTY_MAX,
        .touchIntPin = BoardConfig::TOUCH_INT_PIN,
        .deepSleepWakeupSec = 0,    // No timer wakeup for deep sleep
        .autoSleepTimeoutSec = 20,  // 20-second default
        .enableBatteryMonitoring = false,
        .batteryAdcPin = GPIO_NUM_NC,
    };

    ESP_ERROR_CHECK(gPowerMgr.init(config));

    // Register SD card callbacks
    gPowerMgr.registerPreSleepCallback(powerPreSleepCallback);
    gPowerMgr.registerPostWakeupCallback(powerPostWakeupCallback);

    // Try to mount SD card initially
    ESP_LOGI(TAG, "Attempting to mount SD card...");
    esp_err_t ret = sd_services::initSD();
    bool sdMounted = (ret == ESP_OK);

    // Create LVGL UI
    if (!lvgl_wrapper::lock(100)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    // Main container
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

    // Add touch event handler to reset auto-sleep timer
    lv_obj_add_event_cb(screen, screenTouchCallback, LV_EVENT_PRESSED, nullptr);

    lv_obj_t *cont = lv_obj_create(screen);
    lv_obj_set_size(cont, LV_PCT(95), LV_PCT(95));
    lv_obj_center(cont);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(cont, 10, 0);
    lv_obj_set_style_pad_row(cont, 8, 0);

    // Title
    lv_obj_t *title = lv_label_create(cont);
    lv_label_set_text(title, "Power Management Test");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x00FF00), 0);

    // Wake-up reason
    gWakeupLabel = lv_label_create(cont);
    char wakeupBuf[64];
    snprintf(wakeupBuf, sizeof(wakeupBuf), "Wake-up: %s", gPowerMgr.getWakeupReason());
    lv_label_set_text(gWakeupLabel, wakeupBuf);
    lv_obj_set_style_text_color(gWakeupLabel, lv_color_hex(0xFFFF00), 0);

    // Power status
    gPowerStatusLabel = lv_label_create(cont);
    lv_label_set_text(gPowerStatusLabel, "Mode: ACTIVE");
    lv_obj_set_style_text_color(gPowerStatusLabel, lv_color_hex(0x00FFFF), 0);

    // Countdown label
    gPowerCountdownLabel = lv_label_create(cont);
    lv_label_set_text(gPowerCountdownLabel, "Auto-sleep in: 20 s");
    lv_obj_set_style_text_color(gPowerCountdownLabel, lv_color_hex(0xFFFFFF), 0);

    // SD status
    gPowerSdStatusLabel = lv_label_create(cont);
    lv_label_set_text(gPowerSdStatusLabel, sdMounted ? "SD: Mounted" : "SD: Not mounted");
    lv_obj_set_style_text_color(
        gPowerSdStatusLabel,
        sdMounted ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000),
        0
    );

    // Separator
    lv_obj_t *sep1 = lv_label_create(cont);
    lv_label_set_text(sep1, "--- Power Modes ---");
    lv_obj_set_style_text_color(sep1, lv_color_hex(0x808080), 0);

    // Power mode buttons container
    lv_obj_t *modeRow = lv_obj_create(cont);
    lv_obj_set_size(modeRow, LV_PCT(95), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(modeRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        modeRow,
        LV_FLEX_ALIGN_SPACE_EVENLY,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    // ACTIVE button
    lv_obj_t *activeBtn = lv_button_create(modeRow);
    lv_obj_set_size(activeBtn, 100, 50);
    lv_obj_t *activeLabel = lv_label_create(activeBtn);
    lv_label_set_text(activeLabel, "ACTIVE");
    lv_obj_center(activeLabel);
    lv_obj_add_event_cb(activeBtn, activeBtnCallback, LV_EVENT_CLICKED, nullptr);

    // LOW_POWER button
    lv_obj_t *lowPowerBtn = lv_button_create(modeRow);
    lv_obj_set_size(lowPowerBtn, 100, 50);
    lv_obj_t *lowPowerLabel = lv_label_create(lowPowerBtn);
    lv_label_set_text(lowPowerLabel, "LOW\nPOWER");
    lv_obj_center(lowPowerLabel);
    lv_obj_add_event_cb(lowPowerBtn, lowPowerBtnCallback, LV_EVENT_CLICKED, nullptr);

    // SLEEP button
    lv_obj_t *sleepBtn = lv_button_create(modeRow);
    lv_obj_set_size(sleepBtn, 100, 50);
    lv_obj_t *sleepLabel = lv_label_create(sleepBtn);
    lv_label_set_text(sleepLabel, "SLEEP");
    lv_obj_center(sleepLabel);
    lv_obj_add_event_cb(sleepBtn, sleepBtnCallback, LV_EVENT_CLICKED, nullptr);

    // Separator
    lv_obj_t *sep2 = lv_label_create(cont);
    lv_label_set_text(sep2, "--- Auto-Sleep Timeout ---");
    lv_obj_set_style_text_color(sep2, lv_color_hex(0x808080), 0);

    // Timeout buttons container
    lv_obj_t *timeoutRow = lv_obj_create(cont);
    lv_obj_set_size(timeoutRow, LV_PCT(95), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(timeoutRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        timeoutRow,
        LV_FLEX_ALIGN_SPACE_EVENLY,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    // 10s button
    lv_obj_t *timeout10Btn = lv_button_create(timeoutRow);
    lv_obj_set_size(timeout10Btn, 80, 40);
    lv_obj_t *timeout10Label = lv_label_create(timeout10Btn);
    lv_label_set_text(timeout10Label, "10s");
    lv_obj_center(timeout10Label);
    lv_obj_add_event_cb(timeout10Btn, timeout10Callback, LV_EVENT_CLICKED, nullptr);

    // 20s button
    lv_obj_t *timeout20Btn = lv_button_create(timeoutRow);
    lv_obj_set_size(timeout20Btn, 80, 40);
    lv_obj_t *timeout20Label = lv_label_create(timeout20Btn);
    lv_label_set_text(timeout20Label, "20s");
    lv_obj_center(timeout20Label);
    lv_obj_add_event_cb(timeout20Btn, timeout20Callback, LV_EVENT_CLICKED, nullptr);

    // 30s button
    lv_obj_t *timeout30Btn = lv_button_create(timeoutRow);
    lv_obj_set_size(timeout30Btn, 80, 40);
    lv_obj_t *timeout30Label = lv_label_create(timeout30Btn);
    lv_label_set_text(timeout30Label, "30s");
    lv_obj_center(timeout30Label);
    lv_obj_add_event_cb(timeout30Btn, timeout30Callback, LV_EVENT_CLICKED, nullptr);

    // OFF button
    lv_obj_t *timeoutOffBtn = lv_button_create(timeoutRow);
    lv_obj_set_size(timeoutOffBtn, 80, 40);
    lv_obj_t *timeoutOffLabel = lv_label_create(timeoutOffBtn);
    lv_label_set_text(timeoutOffLabel, "OFF");
    lv_obj_center(timeoutOffLabel);
    lv_obj_add_event_cb(timeoutOffBtn, timeoutOffCallback, LV_EVENT_CLICKED, nullptr);

    // Info label
    lv_obj_t *infoLabel = lv_label_create(cont);
    lv_label_set_text(infoLabel, "Touch screen to reset timer");
    lv_obj_set_style_text_color(infoLabel, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(infoLabel, &lv_font_montserrat_14, 0);

    lvgl_wrapper::unlock();

    // Start countdown update task
    xTaskCreate(countdownUpdateTask, "power_countdown", 4096, nullptr, 5, nullptr);

    ESP_LOGI(TAG, "Power Management Test UI ready!");
    ESP_LOGI(TAG, "Auto-sleep timeout: 20 seconds");
    ESP_LOGI(TAG, "Touch screen to reset timer");

    // Main loop
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

}  // namespace tests
