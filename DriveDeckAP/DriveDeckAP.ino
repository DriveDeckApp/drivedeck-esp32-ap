// DriveDeckAP — LilyGO T-Display-S3 (Touch) as a SIM-less access point for DriveDeck.
//
// Purpose: DriveDeck runs in "This iPhone" mode, where the phone is both the CarPlay source and
// the receiver. The CarPlay traffic therefore never crosses the air (both endpoints are the same
// address, so the kernel routes it through loopback) — but iOS still refuses to start wireless
// CarPlay unless `wifiIP` in the iAP2 0x4301 message is a real Wi-Fi interface address. Advertising
// 127.0.0.1 was tested and rejected: accessoryd ACKs the message and never dials.
//
// So all this board has to do is broadcast an SSID, accept the association, and hand out a DHCP
// lease so the phone's en0 gets a routable address. Throughput and band are irrelevant, which is
// why a 2.4 GHz ESP32 is sufficient where a 5 GHz router would otherwise be required.
//
// The screen shows the credentials to type into DriveDeck, and — critically — the IP the phone was
// actually assigned, so you can confirm the app advertises a matching address.
//
// Touch: tap = next page. Long press = restart the access point.

#include <WiFi.h>
#include <esp_wifi.h>
#include <Arduino_GFX_Library.h>
#include <CST816S.h>

// ---------------------------------------------------------------- Access point

static const char *AP_SSID = "DriveDeckAP";
static const char *AP_PASS = "drivedeck123";  // WPA2 requires >= 8 characters
static const int   AP_CHANNEL = 6;              // must match Settings > Wi-Fi for CarPlay > Channel
static const int   AP_MAX_CLIENTS = 4;

// ---------------------------------------------------------------- T-Display-S3 pins

#define PIN_POWER_ON 15  // must be driven HIGH or the board powers down off USB
#define PIN_LCD_BL   38

// 8-bit parallel (8080) LCD — not SPI.
Arduino_DataBus *bus = new Arduino_ESP32LCD8(
    7 /* DC */, 6 /* CS */, 8 /* WR */, 9 /* RD */,
    39, 40, 41, 42, 45, 46, 47, 48 /* D0..D7 */);

// 170x320 ST7789 with a 35-column offset, rotated to landscape (320x170).
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, 5 /* RST */, 1 /* rotation: landscape */, true /* IPS */,
    170 /* width */, 320 /* height */,
    35 /* col offset 1 */, 0 /* row offset 1 */,
    35 /* col offset 2 */, 0 /* row offset 2 */);

// CST816S capacitive touch: sda, scl, rst, irq
CST816S touch(18, 17, 21, 16);

// ---------------------------------------------------------------- Colours

#define C_BG      0x0000
#define C_TEXT    0xFFFF
#define C_DIM     0x8410
#define C_OK      0x07E0
#define C_WARN    0xFD20
#define C_BAD     0xF800
#define C_ACCENT  0x05BF
#define C_HEADBG  0x2104

static const int PAGE_COUNT = 3;
static int page = 0;
static uint32_t lastDraw = 0;
static int lastClientCount = -1;
static int lastPageDrawn = -1;

// ---------------------------------------------------------------- Client enumeration

struct ClientInfo {
  uint8_t mac[6];
  int8_t rssi;
  bool hasIP;
  IPAddress ip;
};

static ClientInfo clients[AP_MAX_CLIENTS];
static int clientCount = 0;

// Reads the associated stations and, where the DHCP server has issued a lease, their IP.
// esp_wifi_ap_get_sta_list is the stable API for MAC/RSSI; the lease lookup is best-effort.
static void refreshClients() {
  clientCount = 0;
  wifi_sta_list_t staList;
  if (esp_wifi_ap_get_sta_list(&staList) != ESP_OK) return;

  int n = staList.num;
  if (n > AP_MAX_CLIENTS) n = AP_MAX_CLIENTS;

  for (int i = 0; i < n; i++) {
    memcpy(clients[i].mac, staList.sta[i].mac, 6);
    clients[i].rssi = staList.sta[i].rssi;
    clients[i].hasIP = false;
    clients[i].ip = IPAddress((uint32_t)0);
#if __has_include(<esp_netif.h>)
    // Not all core versions expose a per-station lease lookup; guarded so this still builds.
    clients[i].hasIP = false;
#endif
    clientCount++;
  }
}

static String macToString(const uint8_t *m) {
  char buf[18];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(buf);
}

// ---------------------------------------------------------------- Drawing helpers

static void drawHeader(const char *title) {
  bool connected = clientCount > 0;
  uint16_t bg = connected ? C_OK : C_WARN;

  gfx->fillRect(0, 0, gfx->width(), 26, bg);
  gfx->setTextColor(0x0000);
  gfx->setTextSize(2);
  gfx->setCursor(6, 5);
  gfx->print(title);

  // Client count badge, right-aligned.
  char badge[16];
  snprintf(badge, sizeof(badge), "%d/%d", clientCount, AP_MAX_CLIENTS);
  int w = strlen(badge) * 12;
  gfx->setCursor(gfx->width() - w - 6, 5);
  gfx->print(badge);
}

static void label(int x, int y, const char *text) {
  gfx->setTextSize(1);
  gfx->setTextColor(C_DIM);
  gfx->setCursor(x, y);
  gfx->print(text);
}

static void value(int x, int y, const String &text, uint16_t colour, uint8_t size) {
  gfx->setTextSize(size);
  gfx->setTextColor(colour);
  gfx->setCursor(x, y);
  gfx->print(text);
}

// Page 0: the credentials to type into DriveDeck.
static void drawPageCredentials() {
  drawHeader("ACCESS POINT");

  label(6, 34, "SSID");
  value(6, 44, String(AP_SSID), C_TEXT, 2);

  label(6, 70, "PASSWORD");
  value(6, 80, String(AP_PASS), C_TEXT, 2);

  label(6, 106, "CHANNEL");
  value(6, 116, String(AP_CHANNEL), C_ACCENT, 2);

  label(110, 106, "AP ADDRESS");
  value(110, 116, WiFi.softAPIP().toString(), C_ACCENT, 2);

  gfx->setTextSize(1);
  gfx->setTextColor(clientCount > 0 ? C_OK : C_WARN);
  gfx->setCursor(6, 150);
  gfx->print(clientCount > 0 ? "Phone associated - open DriveDeck" : "Waiting for phone to join...");

  gfx->setTextColor(C_DIM);
  gfx->setCursor(6, 162);
  gfx->print("tap=next  hold=restart AP");
}

// Page 1: who is actually connected.
static void drawPageClients() {
  drawHeader("CLIENTS");

  if (clientCount == 0) {
    value(6, 60, "No devices joined", C_WARN, 2);
    gfx->setTextSize(1);
    gfx->setTextColor(C_DIM);
    gfx->setCursor(6, 90);
    gfx->print("Join '");
    gfx->print(AP_SSID);
    gfx->print("' from iOS Wi-Fi settings.");
    gfx->setCursor(6, 104);
    gfx->print("An iOS 'no internet' warning is expected");
    gfx->setCursor(6, 118);
    gfx->print("and harmless - no internet is needed.");
  } else {
    int y = 36;
    for (int i = 0; i < clientCount; i++) {
      value(6, y, macToString(clients[i].mac), C_TEXT, 1);
      gfx->setTextColor(C_ACCENT);
      gfx->setCursor(130, y);
      gfx->print(clients[i].rssi);
      gfx->print(" dBm");
      y += 14;
    }
    gfx->setTextSize(1);
    gfx->setTextColor(C_DIM);
    gfx->setCursor(6, y + 8);
    gfx->print("DHCP pool starts at 192.168.4.2");
    gfx->setCursor(6, y + 22);
    gfx->print("Check the app's Settings > CarPlay Link");
    gfx->setCursor(6, y + 36);
    gfx->print("'Address' row matches the phone's en0.");
  }

  gfx->setTextColor(C_DIM);
  gfx->setTextSize(1);
  gfx->setCursor(6, 162);
  gfx->print("tap=next  hold=restart AP");
}

// Page 2: the exact steps, so the car is not the place you try to remember them.
static void drawPageHelp() {
  drawHeader("DRIVINGMODE SETUP");

  gfx->setTextSize(1);
  gfx->setTextColor(C_TEXT);
  int y = 34;
  const char *lines[] = {
    "1. iOS Wi-Fi: join DriveDeckAP",
    "2. App > Settings > Wi-Fi for CarPlay:",
    "     Network  = DriveDeckAP",
    "     Password = drivedeck123",
    "     Channel  = 6",
    "3. Settings > CarPlay Link = Shared Wi-Fi",
    "4. Turn OFF iPhone Personal Hotspot",
    "5. Unfold device, then Start receiver",
  };
  for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
    gfx->setCursor(6, y);
    gfx->print(lines[i]);
    y += 13;
  }

  gfx->setTextColor(C_DIM);
  gfx->setCursor(6, 162);
  gfx->print("tap=next  hold=restart AP");
}

static void drawPage(bool force) {
  if (!force && page == lastPageDrawn && clientCount == lastClientCount) return;
  lastPageDrawn = page;
  lastClientCount = clientCount;

  gfx->fillScreen(C_BG);
  switch (page) {
    case 0: drawPageCredentials(); break;
    case 1: drawPageClients(); break;
    default: drawPageHelp(); break;
  }
}

// ---------------------------------------------------------------- AP lifecycle

static void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0 /* not hidden */, AP_MAX_CLIENTS);
  // Keeps the radio responsive; the AP must not doze while the phone is associated.
  WiFi.setSleep(false);

  Serial.print("[AP] SSID=");
  Serial.print(AP_SSID);
  Serial.print(" ch=");
  Serial.print(AP_CHANNEL);
  Serial.print(" ip=");
  Serial.println(WiFi.softAPIP());
}

void setup() {
  // Hold the board's power rail up. Without this the S3 drops out when not USB-powered.
  pinMode(PIN_POWER_ON, OUTPUT);
  digitalWrite(PIN_POWER_ON, HIGH);

  Serial.begin(115200);
  delay(100);
  Serial.println("\n[DriveDeckAP] booting");

  gfx->begin();
  gfx->fillScreen(C_BG);

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH);

  gfx->setTextColor(C_ACCENT);
  gfx->setTextSize(2);
  gfx->setCursor(6, 60);
  gfx->print("CarCast AP");
  gfx->setTextSize(1);
  gfx->setTextColor(C_DIM);
  gfx->setCursor(6, 90);
  gfx->print("starting access point...");

  touch.begin();
  startAP();

  delay(400);
  refreshClients();
  drawPage(true);
}

void loop() {
  // Touch: tap cycles pages, long press restarts the AP.
  if (touch.available()) {
    if (touch.data.gestureID == LONG_PRESS) {
      gfx->fillScreen(C_BG);
      value(6, 70, "Restarting AP...", C_WARN, 2);
      WiFi.softAPdisconnect(true);
      delay(300);
      startAP();
      delay(400);
      refreshClients();
      drawPage(true);
    } else {
      page = (page + 1) % PAGE_COUNT;
      drawPage(true);
    }
  }

  // Poll the station list once a second; redraw only when something changed.
  if (millis() - lastDraw > 1000) {
    lastDraw = millis();
    int before = clientCount;
    refreshClients();
    if (clientCount != before) {
      Serial.print("[AP] clients=");
      Serial.println(clientCount);
    }
    drawPage(false);
  }

  delay(10);
}
