# ESP32-S3 Touch LCD 2.8B Starter Kit

A professional, modular starter project for the **Waveshare ESP32-S3-Touch-LCD-2.8B** development board with comprehensive peripheral support and clean code architecture.

## Hardware Features

- **MCU**: ESP32-S3 (WiFi, BLE, 8MB PSRAM)
- **Display**: 2.8" LCD (480x640 portrait, ST7701 driver, RGB565)
- **Touch**: GT911 capacitive touch controller (5-point multi-touch)
- **Sensors**: QMI8658 6-axis IMU, PCF85063 RTC
- **Expansion**: TCA9554 I/O expander

## Project Structure

```
version1/
├── components/
│   ├── board_drivers/          # Hardware abstraction layer
│   │   ├── include/
│   │   │   ├── board_config.hpp        # Pin definitions & board constants
│   │   │   ├── board_constants.hpp     # Hardware timing constants
│   │   │   ├── board_drivers.hpp       # Main driver interface
│   │   │   ├── gt911_simple.hpp        # Touch controller driver
│   │   │   └── lcd_st7701_rgb.hpp      # LCD driver (ST7701 + RGB)
│   │   └── src/
│   │       ├── board_drivers.cpp    # Hardware initialization
│   │       ├── gt911_simple.cpp        # Touch implementation
│   │       └── lcd_st7701_rgb.cpp      # LCD implementation
│   ├── power_manager/          # Power & battery management
│   │   ├── include/
│   │   │   ├── power_manager.hpp       # Power management API
│   │   │   └── power_constants.hpp     # Power timing constants
│   │   └── power_manager.cpp
│   ├── sd_services/            # SD card file operations
│   │   ├── include/
│   │   │   ├── sd_services.hpp         # SD card API
│   │   │   └── sd_constants.hpp        # SD card constants
│   │   └── src/sd_services.cpp
│   ├── rtc_services/           # Real-time clock services
│   │   ├── include/
│   │   │   ├── rtc_services.hpp        # RTC API
│   │   │   └── rtc_constants.hpp       # Time format constants
│   │   └── src/rtc_services.cpp
│   ├── imu_services/           # Motion sensor services
│   │   ├── include/
│   │   │   ├── imu_services.hpp        # IMU API
│   │   │   └── imu_constants.hpp       # Sensor threshold constants
│   │   └── src/
│   │       ├── pedometer.cpp           # Step counter
│   │       ├── motion_detection.cpp    # Shake/freefall detection
│   │       ├── screen_orientation.cpp  # Auto-rotation
│   │       └── gaming_control.cpp      # Tilt-based controls
│   └── lvgl_wrapper/           # LVGL GUI integration
│       ├── include/lvgl_wrapper.hpp
│       └── lvgl_wrapper.cpp
├── main/
│   ├── main.cpp                # Application entry & demo selection
│   ├── test.cpp                # Test demos for all peripherals
│   ├── test.hpp                # Test function declarations
│   ├── test_constants.hpp      # Test configuration constants
│   ├── ui.cpp                  # LVGL UI examples
│   └── ui.hpp                  # UI function declarations
└── STARTER_KIT_GUIDE.md        # This file
```

## Quick Start

### 1. Build & Flash

```bash
idf.py build
idf.py flash monitor
```

### 2. Select Demo Mode

Edit `main/main.cpp` and set `ACTIVE_MODE`:

```cpp
enum class DemoMode {
    CORNER_SQUARES,      // LCD coordinate test
    TOUCH_DRAWING,       // Touch input test
    IMU_VISUALIZATION,   // 3D cube with motion sensors
    RTC_CLOCK,           // Real-time clock display
    SD_CARD_DEMO,        // File system operations
    POWER_MGMT_TEST,     // Power management test
    DEFAULT_UI,          // LVGL UI counter demo
};

constexpr DemoMode ACTIVE_MODE = DemoMode::POWER_MGMT_TEST;  // Change this
```

## Available Demo Applications

### LCD & Touch Demos

#### 1. Corner Squares Test (`CORNER_SQUARES`)
Displays 4 colored squares at screen corners to verify coordinate mapping.

**Features:**
- Validates LCD RGB timing
- Tests frame buffer access
- Verifies coordinate system

#### 2. Touch Drawing (`TOUCH_DRAWING`)
Interactive drawing application using touch input.

**Features:**
- Real-time touch tracking
- Multi-touch support (uses first point)
- Smooth drawing

### Sensor Demos

#### 3. IMU Visualization (`IMU_VISUALIZATION`)
3D rotating cube controlled by tilting the board.

**Features:**
- Real-time accelerometer/gyroscope data
- 3D projection and rendering
- Motion-responsive cube rotation
- Sensor value display

**Controls:** Tilt the board to rotate the cube in 3D space.

### Peripheral Demos

#### 4. RTC Clock (`RTC_CLOCK`)
Digital clock with date/time display.

**Features:**
- Real-time clock readout from PCF85063
- Date formatting (day, month, year)
- Uptime counter
- LVGL-based UI

#### 5. SD Card Demo (`SD_CARD_DEMO`)
Complete file system demonstration.

**Features:**
- SD card mount/unmount
- File read/write operations
- Directory listing
- Card info (type, size, speed)
- Format functionality
- LVGL file browser

**Note:** Requires SD card inserted.

### Power Management Demo

#### 6. Power Management Test (`POWER_MGMT_TEST`)
Comprehensive power mode control with SD card integration.

**Features:**
- Power mode switching (ACTIVE, LOW_POWER, SLEEP)
- Auto-sleep timer with countdown
- Touch wake-up from sleep
- SD card power management (unmount before sleep)
- Wake-up reason display
- Configurable auto-sleep timeout (10s/20s/30s/OFF)

**Controls:**
- Touch buttons to change power modes
- Auto-sleep timer resets on touch
- Wake from sleep by touching screen

#### 7. LVGL UI Demo (`DEFAULT_UI`)
Simple LVGL counter demonstration.

**Features:**
- Button-based counter
- Increment/decrement controls
- Status bar with time
- Clean UI layout

## Hardware API Overview

### Initialization

```cpp
#include "board_drivers.hpp"

BoardDrivers::HardwareHandles hw = {};
ESP_ERROR_CHECK(BoardDrivers::initAll(hw));
```

### LCD Drawing

#### Fill Screen
```cpp
void fillScreen(const BoardDrivers::HardwareHandles &hw, uint16_t color) {
    void *fb = nullptr;
    BoardDrivers::lcd::st7701RgbGetFrameBuffer(hw.lcdHandle, 1, &fb);

    auto *fb16 = (uint16_t *)fb;
    const size_t totalPixels = BoardConfig::LCD_WIDTH * BoardConfig::LCD_HEIGHT;

    for (size_t i = 0; i < totalPixels; i++) {
        fb16[i] = color;
    }

    BoardDrivers::lcd::st7701RgbDrawBitmap(
        hw.lcdHandle, 0, 0,
        BoardConfig::LCD_WIDTH, BoardConfig::LCD_HEIGHT, fb
    );
}
```

#### Draw Bitmap Region
```cpp
// Direct draw to specific region (like LVGL flush)
esp_lcd_panel_draw_bitmap(
    hw.lcdHandle->rgbPanel,
    x_start, y_start, x_end, y_end,  // Coordinates (x_end, y_end exclusive)
    colorBuffer                       // RGB565 pixel data
);
```

### Touch Input

```cpp
#include "gt911_simple.hpp"

gt911::TouchPoint touchPoints[gt911::MAX_TOUCH_POINTS];
uint8_t numTouches = 0;

if (gt911::readTouchData(hw.touch, touchPoints, gt911::MAX_TOUCH_POINTS, &numTouches) == ESP_OK) {
    if (numTouches > 0) {
        int16_t x = touchPoints[0].x;
        int16_t y = touchPoints[0].y;
        // Handle touch at (x, y)
    }
}
```

### Backlight Control

```cpp
void setBacklight(const BoardDrivers::HardwareHandles &hw, uint8_t brightness) {
    // brightness: 0-100%
    uint32_t duty = (BoardConfig::LCD_BACKLIGHT_DUTY_MAX * brightness) / 100;
    ledc_set_duty(BoardConfig::LEDC_MODE, hw.backlightChannel, duty);
    ledc_update_duty(BoardConfig::LEDC_MODE, hw.backlightChannel);
}
```

## RGB565 Color Format

Common colors in RGB565 format:

```cpp
#define COLOR_BLACK   0x0000  // 0b0000000000000000
#define COLOR_WHITE   0xFFFF  // 0b1111111111111111
#define COLOR_RED     0xF800  // 0b1111100000000000
#define COLOR_GREEN   0x07E0  // 0b0000011111100000
#define COLOR_BLUE    0x001F  // 0b0000000000011111
#define COLOR_YELLOW  0xFFE0  // RED + GREEN
#define COLOR_CYAN    0x07FF  // GREEN + BLUE
#define COLOR_MAGENTA 0xF81F  // RED + BLUE
```

**RGB565 Bit Layout**: `RRRRRGGGGGGBBBBB`
- Red: 5 bits
- Green: 6 bits
- Blue: 5 bits

## LCD Coordinate System

**Portrait Mode** (480 x 640):
```
(0,0) ─────────────► (479, 0)
  │
  │     LCD Display
  │    (480 x 640)
  ▼
(0,639) ───────────► (479, 639)
```

**Frame Buffer Access**:
```cpp
fb16[y * LCD_WIDTH + x] = color;  // Set pixel at (x, y)
```

## Critical Configuration

### RGB Panel Timing (Do Not Change!)
These values are matched to the ST7701 panel's specifications:

```cpp
// In lcd_st7701_rgb.cpp
.timings = {
    .pclk_hz = 30 * 1000 * 1000,  // 30MHz pixel clock
    .h_res = 480,                  // Horizontal resolution
    .v_res = 640,                  // Vertical resolution
    .hsync_pulse_width = 8,
    .hsync_back_porch = 10,
    .hsync_front_porch = 50,
    .vsync_pulse_width = 2,
    .vsync_back_porch = 18,
    .vsync_front_porch = 8,
},
.bounce_buffer_size_px = 10 * 480,  // CRITICAL for performance!
```

**Why bounce buffer matters**: PSRAM access is slow. The bounce buffer (in SRAM) allows smooth DMA transfers to the RGB panel, preventing tearing and coordinate mapping issues.

## Troubleshooting

### Issue: Coordinates appear in wrong locations
**Solution**: Verify bounce buffer is enabled (`bounce_buffer_size_px = 10 * 480` in `lcd_st7701_rgb.cpp`)

### Issue: Screen shows garbage or wrong colors
**Solution**:
1. Check RGB channel ordering (RGB vs BGR)
2. Verify RGB565 color format
3. Ensure frame buffer is cleared before drawing

### Issue: Touch not responding
**Solution**:
1. Check GT911 I2C address (default: 0x5D)
2. Verify touch reset sequence in `board_drivers.cpp`
3. Check INT pin configuration (GPIO 16)

### Issue: Backlight not working
**Solution**: Verify LEDC PWM initialization and GPIO 6 configuration

## Pin Configuration

See [board_config.hpp](components/board_drivers/include/board_config.hpp) for complete pin mapping.

**Key Pins**:
- **I2C**: SCL=GPIO7, SDA=GPIO15
- **LCD RGB**: 16-bit parallel data bus (GPIO 3-21, 38-48)
- **LCD SPI Init**: MOSI=GPIO1, SCLK=GPIO2, CS=via IO expander
- **Touch**: INT=GPIO16, RST=via IO expander
- **Backlight**: GPIO6 (LEDC PWM)

## Adding Your Own Application

1. Create a new function in `main.cpp`:
```cpp
static void myCustomApp(const BoardDrivers::HardwareHandles &hw) {
    // Your application code here
}
```

2. Call it from `app_main()`:
```cpp
extern "C" void app_main(void) {
    // ... initialization code ...

    myCustomApp(hw);
}
```

## Resources

- **Waveshare Wiki**: [ESP32-S3-Touch-LCD-2.8B](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-2.8B)
- **ESP-IDF Documentation**: [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/)
- **ST7701 Datasheet**: Search for "ST7701S datasheet"
- **GT911 Datasheet**: Search for "GT911 capacitive touch controller"

## License

This starter kit is provided as-is for educational and development purposes.

---

**Note**: This project uses the new ESP-IDF v5.5+ I2C master API. For older ESP-IDF versions, you may need to modify the I2C initialization code.
