// DriveDeckAP (minimal) — turns any ESP32 into a SIM-less Wi-Fi access point for DriveDeck.
//
// No display, no touch, no extra libraries. Works on any ESP32 / ESP32-S2 / ESP32-S3 / ESP32-C3
// board with the stock Arduino ESP32 core. If you have a LilyGO T-Display-S3 and want the on-screen
// status pages, flash ../DriveDeckAP instead.
//
// Why this exists: wireless CarPlay refuses to start unless the head unit advertises a real Wi-Fi
// interface address. When the receiver and the phone are the same device, the CarPlay traffic never
// actually crosses the air — so this access point does not need to route anything, carry any
// bandwidth, or have an internet connection. It only has to broadcast an SSID, accept the
// association, and hand out a DHCP lease. That is why a cheap 2.4 GHz ESP32 is sufficient where a
// 5 GHz travel router would otherwise be required.
//
// See the repository README for the full explanation and setup steps.

#include <WiFi.h>
#include <esp_bt.h>

// ---------------------------------------------------------------- Configuration
//
// Change these if you like. The SSID and password must match what you enter in
// DriveDeck > Settings > Wi-Fi for CarPlay, and AP_CHANNEL must match the Channel setting there.

static const char *AP_SSID    = "DriveDeckAP";
static const char *AP_PASS    = "drivedeck123";  // WPA2 requires at least 8 characters
static const int   AP_CHANNEL = 6;               // 2.4 GHz channel 1-13
static const int   AP_MAX_CLIENTS = 4;

// Most dev boards have an LED here; harmless if yours does not.
#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

static int lastClientCount = -1;

void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, AP_CHANNEL, 0 /* not hidden */, AP_MAX_CLIENTS);
  // The access point must stay responsive while the phone is associated.
  WiFi.setSleep(false);

  Serial.println();
  Serial.println("=== DriveDeckAP ===");
  Serial.print("SSID     : "); Serial.println(AP_SSID);
  Serial.print("Password : "); Serial.println(AP_PASS);
  Serial.print("Channel  : "); Serial.println(AP_CHANNEL);
  Serial.print("AP IP    : "); Serial.println(WiFi.softAPIP());
  Serial.println("Clients get addresses from 192.168.4.2 upward.");
  Serial.println("An iOS 'no internet' warning is expected and harmless.");
  Serial.println("===================");
}

void setup() {
  // Bluetooth is never used here and the controller comes up by default; releasing it also
  // returns its memory. The only power saving available on a board with no panel.
  btStop();
  esp_bt_controller_disable();
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

  Serial.begin(115200);
  delay(200);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  startAP();
}

void loop() {
  int clients = WiFi.softAPgetStationNum();

  if (clients != lastClientCount) {
    lastClientCount = clients;
    Serial.print("[AP] connected clients: ");
    Serial.println(clients);
    // Solid LED once something has joined, off while waiting.
    digitalWrite(LED_BUILTIN, clients > 0 ? HIGH : LOW);
  }

  delay(1000);
}
