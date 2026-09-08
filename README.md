# SmartEye

A do-it-yourself take on the Athom Homey Portal, built on a Waveshare
ESP32-S3-Touch-LCD-2.8C: a round 480x480 touchscreen that controls a smart
home. The layout and interaction model follow the original closely; none of
Athom's branding, artwork or icons are used.

## What works

- All seven Portal apps: Climate, Lights, Speakers, Weather, Energy, Timer
  and Moods, reached from a home screen of seven icons
- Three ways to change a value, all driving the same state:
  - dragging along the outer edge of the screen, standing in for the
    original's rotary ring
  - tilting the whole board, which the IMU reads as a throttle: hold it over
    and the value creeps, bring it upright and it stops
  - tapping, for on/off and presets
- Wi-Fi setup on the device itself: it opens its own network and shows a QR
  code, you pick your network on your phone. Credentials go to NVS, so it
  never needs reflashing for a different network.
- Timer that keeps running in the background and sounds the onboard buzzer
  until you tap it away
- SD card, mounted and formatted to FAT32 if needed

Everything runs on mock data for now; the link to Homey and Home Assistant is
the next step.

## Hardware

| Part | Detail |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-2.8C |
| Display | 480x480 round IPS, ST7701S over RGB |
| Touch | GT911 capacitive, I2C |
| Sensors | QMI8658 IMU, PCF85063A RTC |
| Other | TCA9554 IO expander, buzzer, microSD, battery charger |

## Building

Needs PlatformIO with the ESP-IDF framework. The board shows up as a serial
port; set it in `platformio.ini`.

```bash
cd firmware
pio run              # build
pio run -t upload    # flash
```

After changing any `CMakeLists.txt`, run `pio run -t clean` first: PlatformIO
does not pick up changed `REQUIRES` otherwise, and you get the same error
again as if nothing happened.

## Notes worth knowing

Several things about this board are easy to get wrong and cost real time:

**The panel init sequence is resolution-specific.** The ST7701S encodes its
gate line count in command `0xC0` as `(NL+1)*8`. A sequence taken from the
rectangular 2.8B sends `0x4F` (640 lines); this round 2.8C needs `0x3B`
(480). The wrong value gives a scrambled, striped picture that looks like a
wiring fault but is not.

**The display pipeline is a package, not four independent settings.** Single
frame buffer, bounce buffers on, the flush synchronised to VSYNC, and
`CONFIG_LCD_RGB_ISR_IRAM_SAFE` off. Change one and the problem comes back in
another form: stripes, a picture that drifts sideways, or flicker. That last
flag must stay off, or the board panics with a cache error the moment Wi-Fi
starts touching flash.

**LVGL draw buffers belong in internal RAM.** Put them in PSRAM and every
repaint crosses that bus three times: rendering into it, copying to the frame
buffer, and the panel reading it out. The display shows stripes under load.

**The touch controller reports "no new sample" and "finger lifted" through the
same register.** Treating them alike tears every drag into a handful of short
touches, and no swipe ever travels far enough to be recognised. Swipes here
are worked out from where a touch starts and ends rather than by adding up
the steps in between, which also makes fast flicks more reliable rather than
less.

## Credit

The ST7701S init sequence and RGB timings were adapted from
[stefankn/esp32-s3-lcd-2.8c-template](https://github.com/stefankn/esp32-s3-lcd-2.8c-template),
which targets this exact board. The firmware skeleton started from
[FatihErtugral/esp32s3-waveshare-2.8-touch-lcd](https://github.com/FatihErtugral/esp32s3-waveshare-2.8-touch-lcd)
(MIT), written for the rectangular 2.8B variant.

Fonts are Montserrat (SIL OFL) and a subset of Material Icons (Apache 2.0),
converted with `lv_font_conv`.

## Licence

MIT
