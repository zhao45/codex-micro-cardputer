# Codex Micro (Cardputer) Port Specification

## Goal

Create a **M5Stack Cardputer ADV (StampS3A / ESP32-S3)** Codex
Micro-compatible controller named **Codex Micro (Cardputer)**.

The physical development board was identified from the device label as
**Cardputer ADV**. Hardware acceptance and pin assignments therefore target the
ADV model, not the original standard Cardputer.

I already downloaded the `components/` directory from:

https://github.com/shenjingnan/agentmote/tree/main/components

The objective is to reuse the compatible protocol and BLE modules obtained from
the upstream AgentMote project, while keeping the Cardputer application,
identity, documentation, and hardware integration in this repository.

> Priority: **minimum changes, maximum reuse, lowest development cost.**

---

## Host Platform

The target computer is **Windows 11**, not macOS.

For BLE Codex Micro control, preserve the existing `codex_control` and
`codex_transport_ble_espidf` implementation unless a concrete Windows 11
compatibility issue is found.

Phase 2 implements the Cardputer ADV internal microphone as a standard Windows
USB Audio Class input. Reuse the supplied `usb_device_uac` component and keep
its macOS-specific mode disabled. Windows 11 must enumerate it without a custom
driver.

Current target behavior after the composite-USB extension:

```text
USB data connected:
Cardputer --USB HID--> Windows 11 --> Codex controls
Cardputer --USB UAC--> Windows 11 --> Microphone input

USB absent or USB Codex HID not accepted:
Cardputer --BLE--> Windows 11 --> Codex controls
```

When USB microphone support is implemented, Windows 11 must show the device
under:

```text
Settings -> System -> Sound -> Input
```

Do not modify the existing Codex BLE protocol merely because the host is
Windows. The composite device reuses the same Codex report map and protocol
through a new USB transport. USB has priority only after it is available to the
Codex host; BLE remains the fallback.

---

## 1. Upstream Compatibility Modules: Reuse, Do Not Rewrite

Treat these as finished reusable libraries:

```text
components/codex_control
components/codex_transport_ble_espidf
```

Do not modify them unless a real Cardputer build/runtime incompatibility proves it is necessary.

Do not reimplement:

```text
Codex Micro protocol
BLE HID transport
HID descriptor
VID/PID identity
JSON framing
JSON-RPC
v.oai.hid
Agent status parsing
BLE bonding/reconnect
Codex Micro BLE identity
```

Do not use an F13/F14 shortcut workaround.

The Cardputer application should call the existing semantic APIs, such as:

```c
codex_control_send_action(...)
codex_control_send_agent(...)
codex_control_set_battery(...)
```

Do not manually build `v.oai.hid` JSON in Cardputer code.

---

## 2. Phase 1 Scope

Only implement:

```text
Cardputer initialization
Cardputer keyboard
Cardputer LCD
Codex BLE controller
Codex actions
BLE connection display
Agent 1-6 status display
```

Do **not** implement yet:

```text
Cardputer microphone
USB microphone / USB UAC
speaker / sound effects
Wi-Fi
web UI
RGB
OTA
configuration menu
Bluetooth Classic
generic keyboard emulation
```

If supplied components exist only for audio, StickS3 hardware, or two-button gestures, do not integrate them in Phase 1 unless truly required.

### Phase 2: Windows USB microphone

Implemented after Phase 1 hardware and BLE control acceptance:

```text
Cardputer ADV ES8311 microphone
I2S input: BCLK GPIO41, WS GPIO43, DIN GPIO46
USB Audio Class 2.0 microphone only
Mono, signed 16-bit PCM, 16 kHz device stream
Windows 11 in-box driver (no custom driver)
BLE Codex controls remain active at the same time
```

Phase 2 reuses `components/usb_device_uac`. It does not add USB speaker output,
audio playback, or macOS-specific UAC handling.

### Phase 3: Composite USB Codex HID + microphone

```text
One Windows 11 composite USB device
USB Audio Class 2.0 microphone interface
Vendor HID interface using the existing Codex report map
USB Codex controls take priority when recognized by the host
BLE Codex controls remain paired and act as fallback
Unplugging USB automatically returns control to BLE
```

The composite descriptor is Cardputer application code. Do not change
`components/codex_control` or `components/codex_transport_ble_espidf` to add
USB support.

---

## 3. Suggested Project Structure

Keep a small standalone ESP-IDF project around the required compatibility
modules:

```text
codex-micro-cardputer/
│
├── CMakeLists.txt
├── sdkconfig.defaults
│
├── components/
│   ├── codex_control/
│   ├── codex_transport_ble_espidf/
│   ├── esp_hid/
│   └── usb_device_uac/
│
└── main/
    ├── CMakeLists.txt
    ├── idf_component.yml
    ├── app_main.cpp
    ├── cardputer_board.cpp
    ├── cardputer_board.h
    ├── cardputer_keyboard.cpp
    ├── cardputer_keyboard.h
    ├── cardputer_ui.cpp
    ├── cardputer_ui.h
    ├── cardputer_usb_codex.cpp
    ├── cardputer_usb_codex.h
    ├── cardputer_usb_mic.cpp
    └── cardputer_usb_mic.h
```

This structure is only a suggestion. Use fewer files if that makes the implementation simpler.

Do not introduce unnecessary abstraction or refactoring.

---

## 4. Cardputer Hardware

Target:

```text
M5Stack Cardputer ADV
StampS3A / ESP32-S3
TCA8418 keyboard controller
```

Before coding, verify the official Cardputer hardware/API information for:

```text
LCD
keyboard
power initialization
battery information
```

Prefer official M5Stack / ESP-IDF-compatible components or well-tested drivers.

Do not guess GPIO assignments.

For Cardputer ADV Phase 1, use the board's native hardware connections:

```text
LCD: ST7789, 240x135 landscape
Keyboard: TCA8418 over I2C (SDA GPIO8, SCL GPIO9, INT GPIO11)
```

Do not use the original Cardputer's direct GPIO keyboard matrix scanner on
Cardputer ADV.

Keep the project on **ESP-IDF**; do not convert it into an Arduino sketch.

---

## 5. Keyboard Mapping

Use:

```text
Enter -> Send
M     -> Mic
Y     -> Approve
N     -> Decline
F     -> Fast
Tab   -> Fork

1 -> Agent 1
2 -> Agent 2
3 -> Agent 3
4 -> Agent 4
5 -> Agent 5
6 -> Agent 6

Space -> Local LCD page toggle only
```

If the Agent API is zero-based:

```text
1 -> agent index 0
2 -> agent index 1
...
6 -> agent index 5
```

Avoid off-by-one errors.

---

## 6. Mic Must Be True Push-to-Talk

`M` must support key-down and key-up:

```text
M DOWN
  -> CODEX_ACTION_MIC / CODEX_ACTION_PRESS

M HELD
  -> do not repeatedly send PRESS

M UP
  -> CODEX_ACTION_MIC / CODEX_ACTION_RELEASE
```

Implement real state tracking.

Other keys must also avoid repeated triggering caused by continuous keyboard scanning. Use appropriate edge detection/debounce.

---

## 7. LCD Pages

The LCD must have at least two pages:

```text
PAGE 1: STATUS
PAGE 2: KEY MAP
```

Use **Space** to toggle:

```text
STATUS --Space--> KEY MAP
KEY MAP --Space--> STATUS
```

Important:

> `Space` is a Cardputer-local UI control. Do not transmit Space to Codex.

### STATUS page

Example:

```text
Codex Micro                  Cardputer

BLE: Connected

Agent 1: ...
Agent 2: ...
Agent 3: ...
Agent 4: ...
Agent 5: ...
Agent 6: ...

Battery: xx%

[SPACE] Key Map
```

If disconnected:

```text
BLE: Disconnected
```

### KEY MAP page

Display:

```text
CODEX KEY MAP

ENTER  Send
M      Mic / Hold
Y      Approve
N      Decline
F      Fast
TAB    Fork

1-6    Agent 1-6

SPACE  Back
```

This page exists so the user can immediately see what each key does.

---

## 8. Agent Status

Reuse the existing `codex_control` host event handling for:

```text
v.oai.thstatus
```

Desired flow:

```text
Codex host
   ->
codex_control
   ->
existing event callback
   ->
Cardputer application state
   ->
LCD
```

Do not put LCD drawing logic into `codex_control.c`.

Keep protocol code and Cardputer UI separated.

---

## 9. BLE Connection State

Show:

```text
BLE: Connected
```

or:

```text
BLE: Disconnected
```

Use the existing compatibility callbacks and public state interfaces where
possible.

Do not duplicate BLE state tracking if the current components already expose it.

---

## 10. UI Refresh

Do not clear/redraw the full LCD every loop.

Refresh when something actually changes:

```text
page changed
BLE state changed
Agent state changed
battery changed
```

A simple dirty-flag/state-change approach is sufficient.

---

## 11. Reuse vs New Code

### REUSE unchanged if possible

```text
components/codex_control
components/codex_transport_ble_espidf

Codex protocol
BLE HID transport
HID descriptor
VID/PID identity
JSON framing
RPC parsing
Agent status parsing
BLE bonding/reconnect
```

### NEW Cardputer-specific code

```text
Cardputer initialization
Cardputer keyboard integration
key -> semantic action mapping
Cardputer LCD
STATUS page
KEY MAP page
Space page switching
app_main
build configuration
```

### IGNORE in Phase 1

```text
USB audio
USB UAC
microphone
speaker
StickS3-specific UI/input
two-button gestures
audio codec
```

---

## 12. Required Development Sequence

### Step 1 — Inspect supplied components

Identify:

```text
public APIs
required dependencies
BLE initialization entry point
connection-state API/callback
Agent status event callback
```

### Step 2 — Report reuse plan before major coding

Report:

```text
A. Which supplied files can remain completely unchanged
B. Which supplied files truly need modification, if any
C. Which new Cardputer files need to be created
D. Which Cardputer driver/library will be used
E. Which upstream modules are unnecessary for Phase 1
```

Expected result: `codex_control` and `codex_transport_ble_espidf` remain unchanged.

### Step 3 — Bring up Cardputer hardware

First verify:

```text
Cardputer boots
LCD works
keyboard works
Space switches between STATUS and KEY MAP
```

### Step 4 — Connect the existing compatibility stack

Initialize the existing:

```text
codex_control
codex_transport_ble_espidf
```

through their public APIs.

### Step 5 — Map controls

Implement:

```text
Enter -> Send
M hold -> Mic press/release
Y -> Approve
N -> Decline
F -> Fast
Tab -> Fork
1-6 -> Agent 1-6
Space -> local LCD page toggle only
```

### Step 6 — Display host state

Show:

```text
BLE connected/disconnected
Agent 1-6 host status
```

### Step 7 — Real build

Run a real ESP-IDF build and fix actual:

```text
compiler errors
linker errors
dependency errors
ESP-IDF API mismatches
Cardputer driver problems
```

Do not stop at pseudocode.

---

## 13. Acceptance Criteria

The Phase 1 implementation is complete only when:

```text
1. Firmware builds successfully
2. Firmware flashes to M5Stack Cardputer
3. Cardputer boots
4. LCD works
5. Keyboard works

6. Windows 11 Bluetooth sees "Codex Micro"
7. Windows 11 pairs successfully without a custom driver
8. ChatGPT / Codex Desktop for Windows connects to it

9. Enter -> Send
10. M down -> Mic PRESS
11. M held -> no repeated PRESS spam
12. M up -> Mic RELEASE
13. Y -> Approve
14. N -> Decline
15. F -> Fast
16. Tab -> Fork
17. 1-6 -> Agent 1-6

18. STATUS shows BLE Connected/Disconnected
19. STATUS can show Agent 1-6 status
20. Space switches to KEY MAP
21. KEY MAP shows the control mapping
22. Space switches back to STATUS
23. Space is never transmitted to Codex
```

---

## 14. Final Deliverables

After implementation, provide:

```text
1. New files created
2. Supplied files modified
3. Why each supplied file had to be modified
4. Which upstream modules remained unchanged
5. Exact ESP-IDF version
6. Exact build command
7. Exact flash command
8. Exact serial monitor command
9. First-boot test procedure
10. Expected serial log
11. Windows 11 BLE pairing procedure
12. BLE bond clearing / re-pair procedure
13. Per-key functional test procedure
14. Known remaining limitations
```

Do not provide only mock code or pseudocode.

The final result must be actually buildable and flashable to a real
**M5Stack Cardputer ADV** and accepted against **Windows 11**.

---

## Final Priority

Whenever choosing between rewriting a compatibility feature and reusing an
existing upstream module, **prefer reuse unless there is a concrete technical
reason not to**.

The supplied `components/` directory exists specifically to avoid rebuilding the Codex Micro compatibility layer.

**Do not refactor the upstream compatibility modules unnecessarily.**

---

## 15. Phase 2 USB Microphone Acceptance

```text
1. Windows composite USB device VID/PID is 303A:8360
2. MEDIA interface is "Cardputer ADV Microphone"
3. AudioEndpoint is "Microphone (Cardputer ADV Microphone)"
4. Device uses the Windows 11 in-box USB audio driver
5. Capturing from the endpoint returns non-silent PCM samples
6. HID interface is "Codex Micro" and uses the existing Codex report map
7. No USB speaker, playback, or macOS-only descriptor is enabled
8. USB-only supports both Codex controls and the Cardputer microphone
9. With BLE and USB present, USB controls have priority without duplicate actions
10. If USB is removed or not accepted by Codex, BLE controls remain available
```

Expected feature matrix:

| BLE | USB data | Result |
|---|---|---|
| Yes | Yes | USB Codex controls + Cardputer USB microphone; BLE fallback |
| Yes | No | BLE Codex controls; Windows/Codex selects the host microphone |
| No | Yes | USB Codex controls + Cardputer USB microphone |
| No | No | Local LCD/key-map and battery display only |

## 16. Battery Display Acceptance

```text
1. Startup does not display a fabricated 100% value
2. GPIO10 ADC uses the Cardputer ADV 2:1 battery divider
3. Every measurement uses multiple ADC samples with outlier rejection
4. Time filtering and percentage hysteresis suppress charger ripple
5. The displayed value is refreshed every 5 seconds, not every scan loop
6. USB connection does not blindly force the battery display to 100%
7. Charging is not claimed because Cardputer ADV does not expose a reliable
   charge-status signal to this firmware
```

The native USB data pins are shared between the ESP32-S3 ROM USB Serial/JTAG
bootloader and the application USB-OTG peripheral. When the application starts,
the COM port is therefore expected to disappear and the UAC microphone appears.
To flash again, enter download mode with the Cardputer ADV `G0` procedure; the
ROM COM port will return for `idf.py flash`.
