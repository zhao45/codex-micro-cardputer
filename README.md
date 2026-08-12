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

## Screen preview

| Status screen | Key map |
| :---: | :---: |
| ![Cardputer status screen](docs/images/home-screen.jpg) | ![Cardputer key map](docs/images/key-map-screen.jpg) |

## Usage examples

| 1. Windows 11 connection | 2. Agent RGB status |
| :---: | :---: |
| ![Cardputer connected to Codex Micro on Windows 11](docs/images/windows-codex-micro-connection.png) | ![Agent RGB status indicators](docs/images/agent-status-lighting.png) |
| 3. Microphone and key map | 4. Directional control feedback |
| ![Hold M for voice input and view the LCD key map](docs/images/microphone-and-key-map.png) | ![Cardputer directional controls in Codex](docs/images/directional-control-feedback.png) |

1. After Cardputer connects through USB or BLE, Codex recognizes it as a Codex
   Micro controller on Windows 11.
2. Agent color blocks on the LCD reflect status colors received from the Codex
   host, such as idle, thinking, waiting, finished, or error.
3. Hold `M` to control Codex voice input. Press `Space` to view the full key map
   on the Cardputer LCD.
4. Codex displays analog-direction feedback when a mapped direction key is
   pressed. This firmware maps Left/Right to decrease/increase reasoning effort.

## Requirements

- M5Stack Cardputer ADV (StampS3A / ESP32-S3).
- A data-capable USB-C cable.
- A Windows 11 PC with the Codex desktop app.
- ESP-IDF v5.5.2, Git, and internet access for building and flashing.
- Bluetooth Low Energy support on Windows for wireless controls.

## Download and flash on Windows 11

### 1. Install ESP-IDF 5.5.2

Follow Espressif's official
[Windows installation guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/get-started/index.html)
and install **ESP-IDF v5.5.2**. Open `ESP-IDF 5.5 PowerShell` from the Windows
Start menu after installation.

If that shortcut is unavailable, open a regular PowerShell window and load the
ESP-IDF environment first:

```powershell
& "$HOME\esp\v5.5.2\esp-idf\export.ps1"
```

Adjust the path if ESP-IDF is installed elsewhere. The environment is ready
when it prints `Done! You can now compile ESP-IDF projects.`

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

## Complete key mapping

| Cardputer key | Action | What it does | Custom setup |
|---|---|---|---|
| `Enter` | Send | Sends the text or instruction currently in the Codex composer | No |
| `M` (hold) | Microphone press | Starts voice input and keeps the microphone control active while held | No |
| `M` (release) | Microphone release | Ends voice input and releases the microphone control | No |
| `Y` | Approve | Approves the current command, edit, or permission request | No |
| `N` | Decline | Declines the current permission or confirmation request | No |
| `F` | Fast | Triggers the Codex Micro Fast action | No |
| `Tab` | Fork | Forks a new task from the current work | No |
| `1` | Agent 1 | Selects Agent/task slot 1 directly | No |
| `2` | Agent 2 | Selects Agent/task slot 2 directly | No |
| `3` | Agent 3 | Selects Agent/task slot 3 directly | No |
| `4` | Agent 4 | Selects Agent/task slot 4 directly | No |
| `5` | Agent 5 | Selects Agent/task slot 5 directly | No |
| `6` | Agent 6 | Selects Agent/task slot 6 directly | No |
| `Up` | Previous Agent | Selects the previous Agent; wraps from Agent 1 to Agent 6 | No |
| `Down` | Next Agent | Selects the next Agent; wraps from Agent 6 to Agent 1 | No |
| `C` | New Thread / New Chat | Creates a new Codex task directly | Yes |
| `P` | Plan Mode | Toggles Plan Mode so Codex plans before making changes | Yes |
| `R` | Review Changes | Opens the current code changes/diff review | Yes |
| `Left` | Reasoning - | Decreases the current model reasoning effort by one level | Yes |
| `Right` | Reasoning + | Increases the current model reasoning effort by one level | Yes |
| `Space` | LCD page | Toggles between the status and key-map LCD pages; not sent to the computer | No |

Keys not listed above (for example `A`, `B`, `D`, `E`, and `G`) currently have
no Codex action. They do not type regular keyboard text or affect the computer.

### One-time setup for the new keys

The Codex desktop app lets you customize Codex Micro command keys and analog
directions. Open **Settings -> Codex Micro** and configure:

| Codex Micro control | Action | Cardputer key |
|---|---|---|
| Analog Up | Toggle Plan Mode | P |
| Analog Down | New Thread / New Chat | C |
| Analog Left | Decrease Reasoning Effort | Left |
| Analog Right | Increase Reasoning Effort | Right |
| Second microphone key (ACT11) | Review Changes | R |

The five keys marked **Yes** under Custom setup require the mappings below.
Enable **Use separate microphone keys** before configuring ACT11. Keep the
first microphone key assigned to Microphone for Cardputer `M`, and assign the
second one to Review Changes. Up/Down and number keys select Agents directly
in firmware and need no Codex setting.

Analog Up defaults to Plan Mode, but verify all five mappings in case the host
has older custom settings. See the official
[Codex Micro documentation](https://learn.chatgpt.com/docs/features/codex-micro).

### Functional checks

After setup, verify:

1. With USB connected and Bluetooth disabled, Enter, Y, N, C, P, and R still
   control Codex.
2. Up/Down moves the LCD selection sequentially through Agents 1-6 without
   random jumps.
3. Left/Right decreases/increases the Codex reasoning effort.
4. `Cardputer ADV Microphone` appears under **Settings -> System -> Sound ->
   Input**.
5. With USB disconnected, Codex controls still work through a paired BLE
   connection.

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
