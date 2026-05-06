/*
 * 公車到站即時顯示器 (最終完美版)
 * LilyGo T-Display S3
 * 特色：Captive Portal 網頁設定、本地倒數引擎、進階動畫、斷電記憶
 */

#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <WiFiManager.h> 
#include <Preferences.h> 

// ════════════════════════════════════════
//  ★ 設定區 
// ════════════════════════════════════════
#define UPDATE_MS  30000UL // API 同步頻率 (30秒)

// ── GAS 中繼站 URL ──────────────────────────
const char* GAS_URL = "https://script.google.com/macros/s/AKfycbxeBZlcuMJlG1H6UwS5fHSr8MHGgSMQb8BxyYxffz6XgfuI6yDynsLVEMMAJ9s3xZRbeQ/exec";

Preferences prefs;
bool shouldSaveConfig = false; 

// ── T-Display S3 螢幕驅動設定 ────────────
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789  _panel;
  lgfx::Bus_Parallel8 _bus; 
  lgfx::Light_PWM     _light;
public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.freq_write = 20000000;    
      cfg.pin_wr = 8;
      cfg.pin_rd = 9;
      cfg.pin_rs = 7;
      cfg.pin_d0 = 39;
      cfg.pin_d1 = 40;
      cfg.pin_d2 = 41;
      cfg.pin_d3 = 42;
      cfg.pin_d4 = 45;
      cfg.pin_d5 = 46;
      cfg.pin_d6 = 47;
      cfg.pin_d7 = 48;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs           = 6;
      cfg.pin_rst          = 5;
      cfg.pin_busy         = -1;
      cfg.memory_width     = 240; 
      cfg.memory_height    = 320;
      cfg.panel_width      = 170; 
      cfg.panel_height     = 320;
      cfg.offset_x         = 35;  
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = true;
      cfg.rgb_order        = false;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = true;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl      = 38;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};

LGFX tft;
LGFX_Sprite spr(&tft); 

// ── 路線與方向設定 ───────────────────────
const char* STOP_NAME = "南港軟體園區";

struct RouteConfig {
  const char* name;
  const char* nameEn;
  const char* dir[1];
  const char* dirEn[1];
  uint32_t bgColor;
};

const RouteConfig ROUTES[] = {
  { "棕19", "BR19", { "→捷運昆陽"   }, { "→MRT Kunyang"   }, 0x0001 }, 
  { "藍51", "BL51", { "→南港展覽館" }, { "→Nangang Exh Ctr" }, 0x0002 }, 
  { "629",  "629",  { "→松山車站"   }, { "→Songshan Station"}, 0x0000 }  
};
const int ROUTE_COUNT = 3;

struct BusRow {
  int estimateSec;
  int stopStatus;
};
BusRow busData[3][2]; // 修改為 3，對應 3 條路線
String lastUpdateStr = "--:--:--"; // 新增：存儲最後成功的更新時間

// ── 從 GAS 中繼站抓取棕19 + 藍51 即時資料 ─
// GAS 回傳格式: {"棕19":{"sec":120,"status":0},"藍51":{"sec":45,"status":0}}
// 629 為新北市公車，不在台北市 API，改用本地倒數模式
void fetchAllRoutes() {
  HTTPClient http;
  Serial.print("正在連線 GAS... ");
  http.begin(GAS_URL);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  int code = http.GET();
  Serial.printf("HTTP 狀態碼: %d\n", code);

  if (code == 200) {
    String payload = http.getString();
    Serial.print("收到 JSON: ");
    Serial.println(payload);
    
    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (!err) {
      const char* apiRoutes[] = {"棕19", "藍51", "629"};
      for (int ri = 0; ri < 3; ri++) {
        if (doc.containsKey(apiRoutes[ri])) {
          JsonObject r = doc[apiRoutes[ri]];
          busData[ri][0].estimateSec = r["sec"] | -1;
          busData[ri][0].stopStatus  = r["status"] | 0;
          Serial.printf("已更新 %s: %d 秒\n", apiRoutes[ri], busData[ri][0].estimateSec);
        }
      }
      if (doc.containsKey("updated")) {
        lastUpdateStr = doc["updated"].as<String>();
      }
    } else {
      Serial.print("解析 JSON 失敗: ");
      Serial.println(err.c_str());
    }
  } else {
    Serial.println("抓取失敗，請確認網路或 GAS URL 是否正確");
  }
  http.end();
}

// ── 到站文字與顏色 ──────────────
String formatETA(int sec, int status, bool isEn) {
  if (status != 0) {
    switch (status) {
      case 1: return isEn ? "Not Start" : "未發車";
      case 2: return isEn ? "No Stop"   : "不停靠";
      case 3: return isEn ? "Last Bus"  : "末班過";
      case 4: return isEn ? "No Serv"   : "未營運";
    }
  }
  if (sec < 0)  return "---";
  if (sec < 30) return isEn ? "Arriving" : "進站中";
  if (sec < 90) return isEn ? "Soon" : "即將進站";
  return String(sec / 60) + (isEn ? " mins" : " 分");
}

uint32_t etaColor(int sec, int status) {
  if (status != 0 || sec < 0) return spr.color565(40, 40, 40);
  if (sec < 30)  return spr.color565(255, 0, 0);   // 正紅
  if (sec < 90)  return spr.color565(255, 128, 0); // 橘
  if (sec < 300) return spr.color565(255, 210, 0); // 黃
  return TFT_WHITE;
}

// ── 渲染畫面 ──────────
// 語言與動畫全域變數
bool isEnglish = false;
float langOffset = 0; // 0:中文, 40:英文 (捲動偏移)
uint32_t lastLangSwitch = 0;

void drawRow(int x, int y, int ri, int di, bool en) {
  BusRow& b    = busData[ri][di];
  String  eta  = formatETA(b.estimateSec, b.stopStatus, en);
  uint32_t col = etaColor(b.estimateSec, b.stopStatus);
  int animFrame = (millis() / 400) % 4;
  int W = spr.width();

  String routeName = en ? ROUTES[ri].nameEn : ROUTES[ri].name;
  int nameW = spr.textWidth(routeName);
  
  uint32_t bg = ROUTES[ri].bgColor;
  if (bg == 0x0001) bg = spr.color565(200, 128, 0);   // 棕色
  if (bg == 0x0002) bg = spr.color565(0, 85, 166);   // 藍色

  if (bg != 0) {
    // 路線專屬色塊背景
    spr.fillRoundRect(x, y - 2, 50, 20, 4, bg);
    spr.setTextColor(TFT_WHITE);
    spr.drawString(routeName, x + (50 - nameW) / 2, y);
  } else {
    // 沒有背景色 (一般公車)
    spr.setTextColor(spr.color565(255, 128, 0)); // 亮橘色
    spr.drawString(routeName, x + (50 - nameW) / 2, y); 
  }

  // 方向：淺灰色，從固定位置開始
  spr.setTextColor(0xBDF7);
  spr.drawString(en ? ROUTES[ri].dirEn[di] : ROUTES[ri].dir[di], x + 55, y);

  if (b.estimateSec >= 0 && b.estimateSec < 30 && b.stopStatus == 0) {
    // ★ 三級警示：紅底白字 (進站中)
    if (animFrame == 0) eta = en ? "Arriving  " : "進站中   ";
    if (animFrame == 1) eta = en ? "Arriving >" : "進站中 > ";
    if (animFrame == 2) eta = en ? "Arriving>>" : "進站中 >>";
    if (animFrame == 3) eta = en ? "Arriving!!" : "進站中>>>";
    int tw = spr.textWidth(eta);
    spr.fillRoundRect(W - tw - 10, y - 2, tw + 8, 22, 4, spr.color565(255, 0, 0)); // 紅底
    spr.setTextColor(TFT_WHITE);
    spr.drawString(eta, W - tw - 6, y);
  } else if (b.estimateSec >= 30 && b.estimateSec < 90 && b.stopStatus == 0) {
    // ★ 三級警示：深底亮橘字 (即將進站)
    int tw = spr.textWidth(eta);
    spr.fillRoundRect(W - tw - 10, y - 2, tw + 8, 22, 4, spr.color565(64, 32, 0));
    spr.setTextColor(spr.color565(255, 128, 0));
    spr.drawString(eta, W - tw - 6, y);
  } else {
    spr.setTextColor(col);
    int tw = spr.textWidth(eta);
    spr.drawString(eta, W - tw - 4, y);
  }
}

void drawScreen() {
  spr.fillSprite(TFT_BLACK); 
  int W = spr.width();
  int H = spr.height();

  // ★ 標題捲動 (加入裁剪區域防止蓋到下方資訊)
  spr.setFont(&lgfx::fonts::efontTW_16);
  spr.setClipRect(0, 0, W, 25); 
  int titleY = 4 - (int)langOffset;
  spr.setTextColor(TFT_WHITE);
  spr.drawString("捷運南港軟體園區站", 4, titleY);
  spr.drawString("Nangang Software Park Station", 4, titleY + 40);
  spr.clearClipRect();

  // 右上角時間 (不捲動)
  struct tm ti;
  if (getLocalTime(&ti)) {
    char buf[10]; strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
    spr.setTextColor(0x52AA); // 調暗：退居背景角色
    spr.drawString(buf, W - spr.textWidth(buf) - 4, 4);
  }

  spr.drawFastHLine(0, 25, W, 0x2945);
  
  int startY = 34;
  int ROW_H = 38;
  
  for (int ri = 0; ri < ROUTE_COUNT; ri++) {
    int curY = startY + ri * ROW_H;
    if (ri > 0) spr.drawFastHLine(8, curY - 4, W - 16, 0x2124); // 左右各留8px呼吸感
    
    // 使用裁剪區塊實現單行內捲動
    spr.setClipRect(0, curY - 2, W, ROW_H); 
    drawRow(4, curY - (int)langOffset, ri, 0, false); // 中文版
    drawRow(4, curY + 40 - (int)langOffset, ri, 0, true); // 英文版
    spr.clearClipRect();
  }

  // 底部更新時間 (顯示最後一次從 GAS 抓到的時間)
  spr.drawFastHLine(0, H - 18, W, 0x2945);
  char buf[32];
  snprintf(buf, sizeof(buf), isEnglish ? "Updated %s" : "更新 %s", lastUpdateStr.c_str());
  spr.setFont(&lgfx::fonts::efontTW_12);
  spr.setTextColor(0x39C7);
  spr.drawString(buf, 4, H - 15);
  
  spr.pushSprite(0, 0); 
}

void showMsg(String msg, uint32_t color = TFT_WHITE) {
  tft.fillScreen(TFT_BLACK);
  tft.setFont(&lgfx::fonts::efontTW_16);
  tft.setTextColor(color);
  tft.setCursor(10, 60);
  tft.print(msg);
}

// ════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  pinMode(15, OUTPUT);
  digitalWrite(15, HIGH);
  
  pinMode(14, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1); 
  tft.setBrightness(200);
  spr.createSprite(tft.width(), tft.height());
  // 回歸最簡約的設定，不進行任何 Byte Swap 或色調反轉

  prefs.begin("busConfig", false);

  WiFiManager wm;
  
  // ★ 強制重設邏輯：如果你開機時按住按鈕，就清除 WiFi
  if (digitalRead(14) == LOW) {
    showMsg("偵測到按鈕按下\n正在清除 WiFi 設定...", TFT_RED);
    wm.resetSettings(); 
    delay(2000);
  }

  // 不需要額外輸入欄位，GAS URL 已內建在程式碼中

  wm.setAPCallback([](WiFiManager *myWiFiManager) {
    showMsg("請用手機連線WiFi:\n\n名稱: BusTracker_Config\n\n連線後將自動跳出設定網頁", TFT_YELLOW);
  });

  showMsg("正在連接 WiFi...");
  
  // 如果連不上 WiFi 會開啟設定網頁，超時 180 秒自動重開
  wm.setConfigPortalTimeout(180); 
  bool res = wm.autoConnect("BusTracker_Config");

  if (!res) {
    showMsg("連線超時，請按 Reset 重試", TFT_RED);
    delay(5000);
    ESP.restart();
  }

  showMsg("同步網路時間...");
  configTime(8 * 3600, 0, "pool.ntp.org", "time.cloudflare.com");
  delay(1500); 

  // 初始化（全部等待 API）
  for (int r = 0; r < ROUTE_COUNT; r++) {
    busData[r][0].estimateSec = -1;
    busData[r][0].stopStatus  = 0;
  }

  showMsg("載入公車資料...");
}

// ── 全新三引擎驅動迴圈 ──────────────────
uint32_t lastApiMs = 0;
uint32_t lastAnimMs = 0;
uint32_t lastTickMs = 0;

void loop() {
  uint32_t now = millis();
  
  // 語言自動切換計時器 (每 6 秒更換一次)
  if (now - lastLangSwitch >= 6000) {
    isEnglish = !isEnglish;
    lastLangSwitch = now;
  }

  // 平滑捲動動畫引擎 (目標 0 或 40)
  float target = isEnglish ? 40.0f : 0.0f;
  if (abs(langOffset - target) > 0.1) {
    langOffset += (target - langOffset) * 0.15f; // 彈性平滑移動
  } else {
    langOffset = target;
  }

  // 引擎 1：從 GAS 同步棕19 + 藍51 (每 30 秒)
  if (now - lastApiMs >= UPDATE_MS || lastApiMs == 0) {
    fetchAllRoutes();
    lastApiMs = now;
  }
  
  // 引擎 2：本地倒數計時器 (每 1 秒)
  if (now - lastTickMs >= 1000) {
    for (int r = 0; r < ROUTE_COUNT; r++) {
      for (int d = 0; d < 1; d++) {
        if (busData[r][d].estimateSec > 0) busData[r][d].estimateSec--;
      }
    }
    lastTickMs = now;
  }

  // 引擎 3：畫面刷新 (提升至 30fps 以確保動畫流暢)
  static uint32_t lastRender = 0;
  if (now - lastRender >= 33) {
    drawScreen();
    lastRender = now;
  }
  
  delay(5); 
}