# Notices and Disclaimers

## Independent compatibility project

**Codex Micro (Cardputer)** is an independent, unofficial compatibility
project. It is not affiliated with, authorized by, endorsed by, sponsored by,
or supported by OpenAI, Work Louder, or M5Stack.

OpenAI, ChatGPT, and Codex are trademarks or registered trademarks of OpenAI.
Work Louder and Codex Micro product branding belong to their respective owners.
M5Stack and Cardputer branding belong to M5Stack. Names and marks are used only
to identify hardware and software compatibility.

## Reused MIT-licensed code

The portable Codex control and ESP-IDF BLE transport modules were obtained from
the MIT-licensed `shenjingnan/agentmote` project:

- Source: https://github.com/shenjingnan/agentmote
- Copyright (c) 2026 shenjingnan
- License: MIT; see `LICENSE`

That upstream project states that its Codex Micro-compatible BLE HID behavior
was independently implemented with reference to the MIT-licensed
`codex-micro-4-core2` project, Copyright (c) 2026 imliubo.

The Cardputer-specific app, keyboard/LCD integration, composite USB adapter,
Windows-oriented configuration, and battery filtering are maintained in this
repository as a separate hardware application. Renaming the application does
not erase or supersede the upstream notices.

## Espressif USB Audio module

`components/usb_device_uac` originates from Espressif's `esp-iot-solution`
repository, component version 1.3.1, repository commit
`6a11ddd575777cf5b7722fcd62fd39e18a3372fa`. It is distributed under the
Apache License 2.0; see `components/usb_device_uac/license.txt`.

Local changes are limited to microphone-only safety and build integration for
the application's custom UAC + HID TinyUSB descriptor.

## Protocol and security

This firmware implements an undocumented vendor HID interface. Device names,
identifiers, report formats, key identifiers, and RPC method names may change
without notice and are not stable public interfaces.

BLE pairing uses Just Works bonding without passkey authentication. Use the
device only in a trusted environment. Microphone PCM is sent to the connected
Windows host only when the host opens the USB audio stream; this firmware does
not store recordings or send them over BLE.

See `LICENSE` and the third-party license files for warranty and liability
terms.
