# Codex Micro (Cardputer)

[English](README.md) | 繁體中文

這是以 **M5Stack Cardputer ADV（StampS3A／ESP32-S3）**為目標的非官方
Codex Micro 相容控制器韌體，使用 ESP-IDF 開發並以 Windows 11 為驗收平台。

Cardputer 鍵盤可以透過 USB 或低功耗藍牙（BLE）控制 Codex；內建麥克風
會成為標準 Windows USB Audio Class 輸入裝置，不需要安裝自訂驅動程式。

> 本專案是獨立的相容性實作，與 OpenAI、Work Louder、M5Stack 無隸屬或
> 授權關係。第三方來源與授權請參閱 [NOTICE.md](NOTICE.md)。

## 功能

- USB 複合裝置：Codex 相容 Vendor HID＋UAC 2.0 麥克風。
- USB Codex 控制成功時優先使用 USB，BLE 保留為自動備援。
- Cardputer ADV ES8311 內建麥克風：單聲道、16 kHz、16-bit PCM。
- TCA8418 鍵盤與正確的 Push-to-Talk 按下／放開事件。
- ST7789 240x135 LCD 狀態頁與按鍵說明頁。
- Agent 1～6 選擇與主機狀態顯示。
- 電池 ADC 多次取樣、離群值排除、時間平滑與百分比遲滯。
- 不提供 USB speaker、錄音儲存、Wi-Fi 或音訊播放。

## 連線行為

| BLE | USB 資料連線 | 可用功能 |
|---|---|---|
| 有 | 有 | USB Codex 按鍵＋Cardputer 麥克風；BLE 備援 |
| 有 | 無 | BLE Codex 按鍵；麥克風由 Windows／Codex 選擇 |
| 無 | 有 | USB Codex 按鍵＋Cardputer 麥克風 |
| 無 | 無 | LCD 本機頁面、按鍵說明與電池顯示 |

USB 與 BLE 同時存在時，USB 必須先被 Codex 主機實際辨識才會接手。若
USB HID 不被接受，原本已可用的 BLE 控制會維持啟用；同一個按鍵事件不會
同時由兩種傳輸送出。

## 按鍵

| Cardputer 按鍵 | Codex 動作 |
|---|---|
| Enter | Send |
| M（按住／放開） | Microphone press／release |
| Y | Approve |
| N | Decline |
| F | Fast |
| Tab | Fork |
| 1～6 | 選擇 Agent 1～6 |
| Space | 切換 LCD 本機頁面，不會送給電腦 |

LCD 主標題為 `CODEX MICRO`，右上角顯示硬體名稱 `CARDPUTER`。狀態列會
顯示 `CTRL USB`、`CTRL BLE` 或 `CTRL NONE`。

## 硬體目標

| 功能 | 硬體／接腳 |
|---|---|
| MCU | StampS3A／ESP32-S3 |
| LCD | ST7789，240x135 |
| 鍵盤 | TCA8418；SDA GPIO8、SCL GPIO9、INT GPIO11 |
| 麥克風 codec | ES8311 |
| I2S 麥克風 | BCLK GPIO41、WS GPIO43、DIN GPIO46 |
| 電池 ADC | GPIO10／ADC1 channel 9，2:1 分壓 |

本韌體只支援 Cardputer **ADV**，不是原版 Cardputer 的直接 GPIO 鍵盤矩陣。

## 專案結構

```text
codex-micro-cardputer/
|-- CMakeLists.txt
|-- sdkconfig.defaults
|-- dependencies.lock
|-- main/                       # Cardputer app 與 USB adapter
|-- components/
|   |-- codex_control/
|   |-- codex_transport_ble_espidf/
|   |-- esp_hid/
|   `-- usb_device_uac/
|-- docs/CARDPUTER_PORT_SPEC.md
|-- LICENSE
`-- NOTICE.md
```

Cardputer app 只呼叫語意控制介面，不會自行建立 protocol JSON。既有 protocol
module、BLE adapter 與新增的 USB adapter 維持分離。

## Windows 11 Build

需求：ESP-IDF v5.5.2、USB 資料線與 M5Stack Cardputer ADV。

```powershell
cd "path\to\codex-micro-cardputer"
. "C:\Users\YOUR_NAME\esp\v5.5.2\esp-idf\export.ps1"
idf.py build
```

輸出檔案：

```text
build/codex_micro_cardputer.bin
```

## Flash

應用程式啟動後，原生 USB 會切換成 HID/UAC 複合裝置，serial COM 通常會
消失。再次燒錄前，請讓 Cardputer ADV 進入 `G0` ROM download mode，再找
出新出現的 COM：

```powershell
Get-CimInstance Win32_SerialPort |
  Select-Object DeviceID,Name,PNPDeviceID

idf.py -p COM9 flash
```

請將 `COM9` 改成當次實際連接埠。

## Monitor

```powershell
idf.py -p COM9 monitor
```

使用 `Ctrl+]` 離開。TinyUSB 接管原生 USB 後 monitor 可能中斷，這是預期
行為。預期 ready log：

```text
CODEX_MICRO_CARDPUTER_READY USB_COMPOSITE_READY BLE_FALLBACK_READY
```

## Windows 驗收

```powershell
Get-PnpDevice -PresentOnly |
  Where-Object InstanceId -match 'VID_303A&PID_8360' |
  Format-Table Status,Class,FriendlyName,InstanceId -AutoSize

Get-PnpDevice -PresentOnly -Class AudioEndpoint |
  Where-Object FriendlyName -match 'Cardputer' |
  Format-Table Status,FriendlyName,InstanceId -AutoSize
```

1. 在「設定 → 系統 → 音效 → 輸入」選擇 `Cardputer ADV Microphone`。
2. 關閉 BLE，確認只用 USB 仍能使用按鍵與麥克風。
3. 同時連 BLE 與 USB，LCD 應顯示 `CTRL USB`，每個動作只觸發一次。
4. 拔除 USB，LCD 應切換為 `CTRL BLE`，控制仍可繼續。
5. 觀察電池至少一分鐘，不應在 90～100% 間快速跳動，也不會因 USB 插入
   而直接假設是 100%。

Windows／Codex 仍然負責選擇目前使用的麥克風；韌體無法強迫桌面程式切換
音訊輸入。

## 已知限制

- Codex Vendor HID protocol 未公開，未來主機版本可能改變。
- BLE 使用無密碼的 Just Works bonding，請在可信任環境使用。
- Cardputer ADV 沒有向目前韌體提供可靠充電狀態，因此不會把「有 USB」
  直接顯示成「正在充電」。

## 來源、授權與商標

Cardputer app 重用了 MIT 授權的
[shenjingnan/agentmote](https://github.com/shenjingnan/agentmote) 部分 module；
原作者、介面與授權均保留。`components/usb_device_uac` 來自 Espressif
`esp-iot-solution`，使用其內附的 Apache License 2.0。

重新命名 Cardputer app 不代表移除或取代上游作者權利。完整說明請參閱
[NOTICE.md](NOTICE.md) 與 [LICENSE](LICENSE)。

## 上傳 GitHub

先在 GitHub 建立空白 repository，名稱建議為 `codex-micro-cardputer`，再執行：

```powershell
git add .
git commit -m "Initial Cardputer ADV release"
git remote add origin https://github.com/YOUR_NAME/codex-micro-cardputer.git
git push -u origin main
```

`.gitignore` 已排除 build、managed dependencies、sdkconfig、`.agents/` 與
`.codex`；這些內容留在本機，不會上傳。
