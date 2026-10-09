# StoxBox

StoxBox is an ESPHome-based stock market dashboard for the **Guition ESP32-S3-4848S040** with a 480×480 touchscreen display.

It displays stock prices, daily changes, market status and an intraday chart, with optional portfolio values and currency conversion. Configuration can be changed directly from the touchscreen or through Home Assistant.

> **Note:** This project is a personal dashboard and is not financial advice. Market data is provided for informational purposes only.

## Features

- 480×480 touchscreen UI
- Up to **6 ticker symbols**
- Configurable share quantities
- Current price and daily change
- Intraday price chart using 5-minute market data
- Pre-market, regular-market and after-hours status
- Previous close reference line
- Optional portfolio value display
- USD plus configurable currency conversion
- Configurable display backlight
- Relay control
- Home Assistant API integration with encryption
- OTA updates
- Wi-Fi fallback setup AP / captive portal
- Device information page
- **Restart button on the Setup screen**

## Screenshots

### Start screen

![StoxBox Quote screen](page_splash.png)

### Quote screen

![StoxBox Quote screen](page_quote.png)

The main Quote screen displays the selected ticker, current price, daily change, market status and intraday chart.
Swipe left for the next or swipe right for the previous ticker.

### Quote screen with portfolio value

![StoxBox Quote screen with portfolio value](page_quote_val.png)

When share quantities are configured, the portfolio value can also be displayed together with the current market data.
Double tap the screen to turn on/off showing the value.

### Sum of values scrren

![StoxBox Sum screen](page_sum.png)

### Info screen

![StoxBox Info screen](page_info.png)

The Info screen provides device and system information, including firmware version, network status, memory and SD card information.

### Setup screen

![StoxBox Setup screen](page_setup.png)

The Setup screen allows the main device settings to be changed directly from the touchscreen, including tickers, share quantities, currency, relay control, backlight and display orientation. A Restart button is also available.

## Hardware

![Hardware](guition-esp32-s3-4848s040.png)
![Hardware](guition-esp32-s3-4848s040-connector.png)

The configuration targets:

- **Guition ESP32-S3-4848S040**
- ESP32-S3
- 16 MB flash
- 8 MB-class octal PSRAM configuration
- 480×480 ST7701S display
- GT911 touchscreen

The display and touch GPIO configuration is already included in the YAML.

## Software

- ESPHome `2025.2.0` or newer
- ESP-IDF framework
- LVGL
- Home Assistant (optional)

## Project files

At minimum, keep these files together:

```text
.
├── stoxbox.yaml
├── stoxbox_sd.h
├── secrets.yaml
├── logo.png
└── back.png
```

`logo.png` and `back.png` are referenced by the ESPHome configuration and are required when compiling the firmware.
You can find a couple of other background images in this repo.

## Configuration

Create a `secrets.yaml` file next to `stoxbox.yaml`:

```yaml
wifi_ssid: "YOUR_WIFI_SSID"
wifi_password: "YOUR_WIFI_PASSWORD"
stoxbox__encryption_key: "YOUR_ESPHOME_API_ENCRYPTION_KEY"
```

Do **not** commit your real `secrets.yaml` to GitHub.

A suitable `.gitignore` entry is:

```gitignore
secrets.yaml
```

### Main settings

The default configuration includes:

```yaml
substitutions:
  device_name: stoxbox
  poll_seconds: "60"
  app_version: "0.6"
  ha_topic: "stoxbox/quote"
```

The default ticker and portfolio configuration is stored in persistent ESPHome globals:

```text
Ticker symbols: TSLA SPCX
Shares:         100 0
Currency:       USD
```

Up to six ticker symbols can be entered. Symbols can be separated by spaces, commas, semicolons or line breaks.

Examples:

```text
AAPL MSFT NVDA
```

or:

```text
AAPL, MSFT, NVDA
```

Share counts can be entered in the corresponding order:

```text
10 25 5
```

If only one share count is supplied, that value is applied to every configured ticker.

## Setup screen

The Setup screen provides:

- Ticker list
- Share list
- Currency
- Relay control for the 2x4 pin connector (IO40, IO2, IO1)
- Display backlight control
- Display orientation setting
- **Restart** button

The Restart button performs a hardware/software reboot of the ESP32-S3 using the ESP-IDF restart function.

Swipe up and down gestures used for navigation between the Quote, Info and Setup pages.

## Market data

Stock data is retrieved directly from Yahoo Finance's chart endpoint using HTTPS.

The configuration requests:

```text
range=1d
interval=5m
includePrePost=true
```

The dashboard uses this data to display the current price, change, daily high/low and intraday chart.
Swipe right and left to select the next or previous asset.

Because the project depends on an external market-data service, availability and data quality are outside the control of this project.

## Home Assistant

The device exposes configurable values through the ESPHome native API, including:

- Currency
- Ticker list
- Share list
- Relays
- Display/backlight state
- Display orientation
- Device information and status entities

The API is protected with an encryption key from `secrets.yaml`.

## Flashing / installation

1. Install ESPHome.
2. Clone this repository.
3. Put `stoxbox.yaml`, `logo.png` and `back.png` in the same directory.
4. Create `secrets.yaml`.
5. Adjust the substitutions and initial configuration if required.
6. Compile and upload the YAML with ESPHome.

For example:

```bash
esphome run stoxbox.yaml
```

After the first successful installation, OTA updates can be used for subsequent firmware updates.

## Wi-Fi fallback

If the configured Wi-Fi network is unavailable, the device exposes a fallback access point:

```text
SSID: StoxBox-Setup
```

The fallback AP password is configured in the YAML and should be changed before public distribution if required.

## Time zone and market hours

The current configuration uses:

```yaml
timezone: America/New_York
```

The software distinguishes between:

- Pre-market: 04:00–09:30
- Regular market: 09:30–16:00
- After-hours: until 20:00

The configuration also contains a list of US market holidays and early-close dates. Update these dates as necessary when maintaining the project.

## MIT License

Copyright (c) 2026 pel.hu

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## Disclaimer

StoxBox is a technical display project. It does not provide investment, trading or financial advice. Stock prices can be delayed, unavailable or incorrect, and nothing displayed by the device should be treated as a recommendation to buy or sell securities.
