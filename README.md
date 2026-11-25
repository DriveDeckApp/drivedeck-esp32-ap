# DriveDeckAP

Turn a cheap ESP32 into a SIM-less Wi-Fi access point for [DriveDeck](#about-drivedeck) — no internet, no router, no data plan.

Two sketches are included:

| Sketch | Hardware | What you get |
|---|---|---|
| [`DriveDeckAP-minimal`](DriveDeckAP-minimal) | **Any** ESP32 / S2 / S3 / C3 | Access point only. No extra libraries. |
| [`DriveDeckAP`](DriveDeckAP) | LilyGO **T-Display-S3 (Touch)** | Access point + on-screen status, credentials and setup pages |

If you just want it working: use the [web flasher](#option-a--from-your-browser-easiest) and pick your
board — nothing to install. The minimal sketch is about 40 lines and has no dependencies if you would
rather build it yourself.

---

## Why this exists

Wireless CarPlay will not start unless the head unit advertises a **real Wi-Fi interface address**. Advertising a loopback address (`127.0.0.1`) does not work — the phone acknowledges the request and then silently refuses to connect.

The interesting part is what follows from that. When the receiver and the phone are the *same* device, both ends of the connection share one address, so the operating system routes the traffic internally through loopback. **The video never crosses the air.**

So this access point does not route anything, carry any bandwidth, or need an internet connection. Its entire job is:

1. Broadcast an SSID
2. Accept the association
3. Hand out a DHCP lease so the phone's Wi-Fi interface gets a routable address

That is why a $5 2.4 GHz ESP32 is enough, where you would otherwise need a 5 GHz travel router. Band and throughput are irrelevant when nothing travels over the link.

> **Note:** this only holds for the single-device case. If you are mirroring a *separate* phone to a *separate* receiver, the stream really does cross the air and you need a proper 5 GHz access point.

---

## Hardware

Anything with an ESP32 works for the minimal sketch:

- Any ESP32 / ESP32-S2 / ESP32-S3 / ESP32-C3 dev board
- Powered from a USB port (the car's own USB socket is ideal — 5 V, ~100 mA)

No SIM, no antenna mods, no soldering.

---

## Flashing

### Option A — from your browser (easiest)

No IDE, no toolchain, no command line. Plug the board in and flash it from a web page using the
Web Serial API:

**→ [Open the DriveDeck web flasher](https://drivedeck-esp32-flasher.vercel.app)**

1. Plug the ESP32 into your computer over USB
2. Pick your board from the dropdown
3. Press **Connect & Flash** and choose the serial port when prompted
4. Confirm the install — it takes under a minute

Requires a Chromium-based desktop browser (**Chrome, Edge or Opera**). Safari and Firefox do not
implement Web Serial, and no browser on iOS does. If that is you, use Option B or C below.

### Option B — Arduino IDE

1. Install the **ESP32 board support package**: Arduino IDE → Settings → *Additional Boards Manager URLs*, add
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
   then Tools → Board → Boards Manager → search `esp32` → install **esp32 by Espressif Systems**.

2. Clone or download this repo, then open the sketch folder:
   - `DriveDeckAP-minimal/DriveDeckAP-minimal.ino` — any ESP32
   - `DriveDeckAP/DriveDeckAP.ino` — T-Display-S3 only

3. **Only for the display build**, install two libraries via Library Manager:
   - `GFX Library for Arduino` (by moononournation)
   - `CST816S` (by fbiego)

4. Select your board:
   - Minimal: whatever matches your board, e.g. *ESP32S3 Dev Module* or *ESP32 Dev Module*
   - Display: **LilyGo T-Display-S3**

5. Select the port and press **Upload**.

### Option C — arduino-cli

```bash
# One-time setup
arduino-cli core install esp32:esp32

# Display build only
arduino-cli lib install "GFX Library for Arduino"
arduino-cli lib install "CST816S"

# Find your board's port
arduino-cli board list

# Minimal build — substitute your FQBN and port
arduino-cli compile --fqbn esp32:esp32:esp32s3 DriveDeckAP-minimal
arduino-cli upload  --fqbn esp32:esp32:esp32s3 -p /dev/cu.usbmodem101 DriveDeckAP-minimal

# T-Display-S3 build
arduino-cli compile --fqbn esp32:esp32:lilygo_t_display_s3 DriveDeckAP
arduino-cli upload  --fqbn esp32:esp32:lilygo_t_display_s3 -p /dev/cu.usbmodem101 DriveDeckAP
```

Starting from a board that has something else on it? Wipe it first:

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbmodem101 erase_flash
```

---

## Configuration

All settings are constants at the top of either sketch:

```cpp
static const char *AP_SSID    = "DriveDeckAP";
static const char *AP_PASS    = "drivedeck123";  // WPA2 needs >= 8 characters
static const int   AP_CHANNEL = 6;               // 2.4 GHz, channel 1-13
```

Two rules:

- The password must be **at least 8 characters**, or WPA2 silently fails and the network appears open.
- Whatever you set here must match what you enter in the DriveDeck app, including the channel.

---

## Using it

1. Power the ESP32 from any USB source.
2. On the iPhone: **Settings → Wi-Fi → DriveDeckAP**. iOS will warn about no internet connection — that is expected and harmless. Data continues over cellular.
3. In DriveDeck:
   - **Settings → Wi-Fi for CarPlay** → Network `DriveDeckAP`, Password `drivedeck123`, Channel `6`
   - **Settings → CarPlay Link** → *Shared Wi-Fi*
4. Turn **off** the iPhone's own Personal Hotspot. Hosting an access point occupies the same radio that wireless CarPlay needs, and will prevent it starting.
5. Start the receiver.

### Display build pages

Tap the screen to cycle, hold to restart the access point.

1. **Access Point** — SSID, password, channel, AP address. The header turns green and shows a client count the moment a device associates.
2. **Clients** — connected MAC addresses with signal strength, so you can confirm it is your phone and not something else.
3. **Setup** — the step list above, on the device, for when you are in the car without a laptop.

---

## Troubleshooting

**The network does not appear.** Check the serial monitor at 115200 baud — both sketches print the SSID, channel and AP address on boot. No output usually means the sketch is not running; try re-flashing after an `erase_flash`.

**iOS joins, then drops off.** Normal if iOS decides to prefer a network with internet. It keeps the association and routes data over cellular, which is all that is needed here. Disable *Auto-Join* on your other networks if it keeps switching away.

**Joined, but CarPlay still will not start.** Check, in order:
- The iPhone's Personal Hotspot is off (this is the most common cause)
- The SSID, password **and channel** in the app match the sketch exactly
- The app's link mode is *Shared Wi-Fi*, not Automatic or Loopback

**Password rejected.** `AP_PASS` must be 8 characters or more. Shorter passwords leave the AP open and iOS will not match your saved credentials.

---

## About DriveDeck

DriveDeck is a personal research project that turns an iPhone or iPad into a CarPlay head-unit receiver. This repository contains **only** the ESP32 access-point firmware, which is independent of it — it is a general-purpose "give me a Wi-Fi network with no internet" tool and is useful anywhere you need exactly that.

---

## License

MIT — see [LICENSE](LICENSE).
