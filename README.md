# Codex Micro (Cardputer)

English | [繁體中文](README.zh-TW.md)

Turn an **M5Stack Cardputer ADV** into a Codex controller and USB microphone
for Windows 11. Codex controls work over USB or BLE, while the built-in
microphone is available through USB without a custom driver.

> Cardputer **ADV (StampS3A / ESP32-S3)** only. The original Cardputer is not
> supported.

## Features

- USB Codex controls and Cardputer ADV internal microphone.
- BLE Codex controls with automatic fallback when USB control is unavailable.
- LCD connection status, Agent 1-6 status, key map, and smoothed battery level.
- Mono, 16 kHz, signed 16-bit PCM microphone.
- No speaker, recording storage, Wi-Fi, or audio playback.

## Download and flash on Windows 11

### 1. Install ESP-IDF 5.5.2

Follow Espressif's official
[Windows installation guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/get-started/index.html)
and install **ESP-IDF v5.5.2**. Open `ESP-IDF 5.5 PowerShell` from the Windows
Start menu after installation.

### 2. Download the project

Run in ESP-IDF PowerShell:

```powershell
git clone https://github.com/zhao45/codex-micro-cardputer.git
cd codex-micro-cardputer
```

Alternatively, select **Code -> Download ZIP** on GitHub, extract it, and open
the extracted folder:

```powershell
cd "$HOME\Downloads\codex-micro-cardputer-main"
```

Change the path if the ZIP was extracted elsewhere.

### 3. Build

```powershell
idf.py build
```

The first build needs internet access to download ESP-IDF dependencies. The
application image is generated at:

```text
build/codex_micro_cardputer.bin
```

### 4. Enter download mode

1. Connect the Cardputer ADV with a USB data cable.
2. Hold `G0`, then press Reset; alternatively reconnect USB while holding `G0`.
3. Release `G0` after Windows detects a new COM port.
4. Find the port:

```powershell
Get-CimInstance Win32_SerialPort |
  Select-Object DeviceID,Name,PNPDeviceID
```

Look for a device similar to `USB Serial Device (COM9)`. The COM number varies
between computers.

### 5. Flash

Replace `COM9` with the detected port:

```powershell
idf.py -p COM9 flash
```

`Hash of data verified` followed by `Hard resetting via RTS pin` means the
flash succeeded. Release `G0` and press Reset. If it remains in download mode,
disconnect USB and reconnect it **without holding G0**.

The serial COM port may disappear after a normal boot because USB changes into
the Codex HID + microphone composite device. This is expected.

## Connection behavior

| BLE | USB | Available functions |
|---|---|---|
| Connected | Connected | USB Codex controls + Cardputer microphone; BLE fallback |
| Connected | Disconnected | BLE controls; microphone selected by Windows/Codex |
| Disconnected | Connected | USB Codex controls + Cardputer microphone |
| Disconnected | Disconnected | Local LCD pages and battery display |

For BLE, pair `Codex Micro` under **Settings -> Bluetooth & devices**. To use
the built-in microphone, select `Cardputer ADV Microphone` under
**Settings -> System -> Sound -> Input**.

USB takes priority when both transports are present. If Codex does not accept
the USB HID interface, BLE remains available. A key event is never sent through
both transports at the same time.

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
| Space | Toggle the local LCD page; not sent to the computer |

## Troubleshooting

### No COM port

- Use a USB data cable, not a charge-only cable.
- Retry while holding `G0` and pressing Reset or reconnecting USB.
- Check **Ports (COM & LPT)** in Windows Device Manager.

### `Access denied` or busy COM port

Close other serial terminals, ESP-IDF monitors, or applications using the COM
port, then retry.

### Flash succeeds but the LCD remains off

The device is usually still in G0 download mode. Release `G0`, press Reset, or
reconnect USB without holding G0.

### Windows cannot find the microphone

Reconnect USB and check **Settings -> System -> Sound -> Input**. You can also
run:

```powershell
Get-PnpDevice -PresentOnly -Class AudioEndpoint |
  Where-Object FriendlyName -match 'Cardputer'
```

Windows and Codex still select the active audio input. Firmware cannot force a
desktop application to switch microphones.

## License and attribution

This is an unofficial compatibility project and is not affiliated with OpenAI,
Work Louder, or M5Stack. Selected low-level modules come from the MIT-licensed
[shenjingnan/agentmote](https://github.com/shenjingnan/agentmote) project. The
USB Audio component comes from Espressif's `esp-iot-solution` under Apache-2.0.
See [LICENSE](LICENSE), [NOTICE.md](NOTICE.md), and the component license files.
