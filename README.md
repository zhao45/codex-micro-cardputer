# Codex Micro (Cardputer)

English | [繁體中文](README.zh-TW.md)

Unofficial Codex Micro-compatible controller firmware for the
**M5Stack Cardputer ADV (StampS3A / ESP32-S3)**, built with ESP-IDF and
targeting Windows 11.

The Cardputer keyboard can control Codex through USB or Bluetooth Low Energy
(BLE). Its internal microphone is exposed to Windows as a standard USB Audio
Class input. No custom Windows driver is required.

> This is an independent compatibility project. It is not affiliated with or
> endorsed by OpenAI, Work Louder, or M5Stack. See [NOTICE.md](NOTICE.md).

## Features

- USB composite device: Codex-compatible vendor HID plus UAC 2.0 microphone.
- BLE `Codex Micro` control remains available as an automatic fallback.
- Cardputer ADV internal ES8311 microphone: mono, 16 kHz, signed 16-bit PCM.
- TCA8418 keyboard support with true push-to-talk press/release handling.
- ST7789 status and key-map pages on the built-in 240x135 LCD.
- Agent 1-6 selection and host status display.
- Smoothed battery display using ADC multisampling, outlier rejection, an IIR
  filter, and percentage hysteresis.
- Windows 11 in-box USB audio and HID drivers; no speaker/audio-output device.

## Connection behavior

| BLE | USB data | Available functions |
|---|---|---|
| Connected | Connected | USB Codex controls + Cardputer microphone; BLE fallback |
| Connected | Disconnected | BLE Codex controls; microphone selected by Windows/Codex |
| Disconnected | Connected | USB Codex controls + Cardputer microphone |
| Disconnected | Disconnected | Local LCD, key-map page, and battery display only |

When USB and BLE are both present, USB takes priority after the Codex host
actually recognizes the USB HID interface. If USB HID is not accepted, the
working BLE path remains active. A key event is never sent through both
transports at the same time.

## Key mapping

| Cardputer key | Codex action |
|---|---|
| Enter | Send |
| M (hold/release) | Microphone press/release |
| Y | Approve |
| N | Decline |
| F | Fast |
| Tab | Fork |
| 1-6 | Select Agent 1-6 |
| Space | Toggle local LCD page; never sent to the host |

## LCD

The status page shows the active control transport as `CTRL USB`, `CTRL BLE`,
or `CTRL NONE`, the filtered battery percentage, and Agent 1-6 state. Press
Space to switch to the key-map page.

The main header is `CODEX MICRO`; `CARDPUTER` appears as the hardware label.

## Hardware target

| Function | Hardware / pin |
|---|---|
| MCU | StampS3A / ESP32-S3 |
| LCD | ST7789, 240x135 |
| Keyboard | TCA8418; SDA GPIO8, SCL GPIO9, INT GPIO11 |
| Microphone codec | ES8311 |
| I2S microphone | BCLK GPIO41, WS GPIO43, DIN GPIO46 |
| Battery ADC | GPIO10 / ADC1 channel 9, 2:1 divider |

The firmware targets Cardputer **ADV**, not the original Cardputer keyboard
matrix.

## Repository layout

```text
codex-micro-cardputer/
|-- CMakeLists.txt
|-- sdkconfig.defaults
|-- dependencies.lock
|-- main/
|   |-- app_main.cpp
|   |-- cardputer_board.*
|   |-- cardputer_keyboard.*
|   |-- cardputer_ui.*
|   |-- cardputer_usb_codex.*
|   |-- cardputer_usb_mic.*
|   `-- usb/
|       |-- cardputer_usb_descriptors.c
|       `-- tusb_config.h
|-- components/
|   |-- codex_control/
|   |-- codex_transport_ble_espidf/
|   |-- esp_hid/
|   `-- usb_device_uac/
|-- docs/
|   `-- CARDPUTER_PORT_SPEC.md
|-- LICENSE
`-- NOTICE.md
```

The app presents a small semantic control interface to the keyboard/UI code.
The existing protocol module stays separate from its BLE and USB adapters, so
Cardputer-specific code does not construct protocol JSON or duplicate the HID
report map.

## Requirements

- Windows 11
- M5Stack Cardputer ADV
- ESP-IDF v5.5.2
- A USB data cable

Install ESP-IDF by following Espressif's official Windows setup instructions,
then open PowerShell.

## Build on Windows 11

```powershell
cd "path\to\codex-micro-cardputer"
. "C:\Users\YOUR_NAME\esp\v5.5.2\esp-idf\export.ps1"
idf.py build
```

The resulting application image is:

```text
build/codex_micro_cardputer.bin
```

## Flash

Once the application starts, the native USB port becomes the composite HID/UAC
device and the serial COM port normally disappears. To flash again, put the
Cardputer ADV into `G0` ROM download mode and find the newly appearing COM port:

```powershell
Get-CimInstance Win32_SerialPort |
  Select-Object DeviceID,Name,PNPDeviceID
```

Replace `COM9` with the actual port:

```powershell
idf.py -p COM9 flash
```

## Monitor

```powershell
idf.py -p COM9 monitor
```

Exit with `Ctrl+]`. The monitor may disconnect when TinyUSB takes control of
the native USB peripheral; that is expected. Runtime acceptance is primarily
performed with the LCD and Windows device state.

Expected ready log:

```text
CODEX_MICRO_CARDPUTER_READY USB_COMPOSITE_READY BLE_FALLBACK_READY
```

## Windows 11 verification

List the composite USB device and its interfaces:

```powershell
Get-PnpDevice -PresentOnly |
  Where-Object InstanceId -match 'VID_303A&PID_8360' |
  Format-Table Status,Class,FriendlyName,InstanceId -AutoSize

Get-PnpDevice -PresentOnly -Class AudioEndpoint |
  Where-Object FriendlyName -match 'Cardputer' |
  Format-Table Status,FriendlyName,InstanceId -AutoSize
```

Then verify:

1. Open **Settings -> System -> Sound -> Input**.
2. Select `Cardputer ADV Microphone`.
3. With BLE disconnected, confirm USB-only Codex key control and microphone.
4. Connect BLE too; the LCD should show `CTRL USB` and each key should trigger
   only once.
5. Unplug USB; the LCD should change to `CTRL BLE` and controls should continue.
6. Observe the battery for at least one minute. USB power must not force a fake
   100%, and the display should not rapidly jump between 90-100%.

Windows/Codex still decides which audio input is selected. Firmware cannot
force the desktop application to switch microphones.

## Known limitations

- The Codex vendor HID protocol is undocumented and may change in future host
  releases.
- BLE uses Just Works bonding without a passkey; use it in a trusted space.
- Cardputer ADV does not expose a reliable charging-status signal to this
  firmware, so USB presence is not presented as proof that the battery is
  charging.
- USB speaker, Wi-Fi, microphone recording/storage, and audio playback are not
  implemented.

## Upstream code and attribution

This Cardputer application was built by reusing selected modules from the
MIT-licensed [shenjingnan/agentmote](https://github.com/shenjingnan/agentmote)
project. Those modules retain their original history, interfaces, copyright,
and license. The USB UAC module comes from Espressif's `esp-iot-solution` and
retains its Apache-2.0 license.

Rebranding this Cardputer application does not remove or replace upstream
credit. See [NOTICE.md](NOTICE.md) and the license files inside vendored
modules for details.

## License

Project code and the reused MIT-licensed compatibility modules are distributed
under the [MIT License](LICENSE). `components/usb_device_uac` is distributed
under its included Apache License 2.0. Trademarks belong to their respective
owners and are used only to describe compatibility.

## Publish to GitHub

Create an empty GitHub repository named `codex-micro-cardputer`, then run:

```powershell
git add .
git commit -m "Initial Cardputer ADV release"
git remote add origin https://github.com/YOUR_NAME/codex-micro-cardputer.git
git push -u origin main
```

Generated build files, managed dependencies, local ESP-IDF configuration, and
local Codex workflow files are excluded by `.gitignore`.
