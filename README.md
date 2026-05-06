# ESP32 Bus Tracker (Taipei & New Taipei)

一個基於 LilyGo T-Display S3 的公車到站即時顯示器，整合了台北市與新北市的公車開放資料。

## 特色
- **雙北整合**：同時支援台北市 (blobbus) 與新北市 (ntpcbus) 的開放資料。
- **中繼站架構**：使用 Google Apps Script (GAS) 作為 Proxy，減輕 ESP32 負擔。
- **平滑倒數**：內建本地倒數引擎，即使在 API 更新間隔也會每秒跳動。
- **自動化設定**：內建 WiFiManager，第一次連線可透過手機網頁設定 WiFi。
- **雙語介面**：自動在中文與英文間切換顯示。

## 硬體需求
- **LilyGo T-Display S3** (ESP32-S3)
- USB-C 供電線

## 軟體與環境
- **Arduino IDE** (需安裝 ESP32 核心與 LovyanGFX, ArduinoJson, WiFiManager 函式庫)
- **Google Apps Script** (部署 `BusProxy.gs` 作為中繼伺服器)

## 安裝步驟
1. 部署 `BusProxy.gs` 到 Google Apps Script 並發佈為 Web App。
2. 在 `sketch_apr29a.ino` 中填入您的 `GAS_URL`。
3. 使用 Arduino IDE 將程式碼燒錄至 T-Display S3。
4. 裝置開機後，用手機連線至 `BusTracker_Config` WiFi 進行網路設定。

## 授權
MIT License
