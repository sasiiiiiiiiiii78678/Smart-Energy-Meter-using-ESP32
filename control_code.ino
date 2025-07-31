// ====== RECEIVER: ESP32 (Fetch from Sender + optional OLED) ======
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>

// ------- Optional OLED --------
// Uncomment the next line to enable OLED display. Otherwise, uses Serial Monitor only.
// #define USE_OLED

#ifdef USE_OLED
  #include <Adafruit_GFX.h>
  #include <Adafruit_SSD1306.h>
  #define OLED_WIDTH 128
  #define OLED_HEIGHT 64
  #define OLED_RESET  -1
  Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
#endif

// ---------- WiFi (Phone Hotspot) ----------
const char* WIFI_SSID = "Janith";
const char* WIFI_PASS = "12345678";

// ---------- Sender discovery ----------
const char* SENDER_MDNS = "esp-sender";  // resolves to esp-sender.local
String senderIP = "";                    // filled via mDNS
const char* FALLBACK_SENDER_IP = "";     // fallback if mDNS fails

// ---------- Polling ----------
const unsigned long POLL_MS = 1000;
unsigned long lastPoll = 0;

// ---------- Helpers ----------
bool resolveSenderIP() {
  IPAddress ip = MDNS.queryHost(SENDER_MDNS);
  if (ip.toString() != "0.0.0.0") {
    senderIP = ip.toString();
    Serial.print("Resolved esp-sender.local to: ");
    Serial.println(senderIP);
    return true;
  }
  if (strlen(FALLBACK_SENDER_IP) > 0) {
    senderIP = String(FALLBACK_SENDER_IP);
    Serial.print("Using FALLBACK sender IP: ");
    Serial.println(senderIP);
    return true;
  }
  return false;
}

bool httpGET(const String& url, String& payloadOut) {
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  if (!http.begin(url)) return false;
  int code = http.GET();
  if (code == 200) {
    payloadOut = http.getString();
    http.end();
    return true;
  }
  http.end();
  return false;
}

// Minimal JSON extractors
bool extractJsonNumber(const String& json, const String& key, float& outVal) {
  int k = json.indexOf("\"" + key + "\"");
  if (k < 0) return false;
  k = json.indexOf(":", k);
  if (k < 0) return false;
  int j = k + 1;
  while (j < (int)json.length() && json[j] == ' ') j++;
  int end = j;
  while (end < (int)json.length() && ((json[end] >= '0' && json[end] <= '9') || json[end] == '.' || json[end] == '-')) end++;
  if (end <= j) return false;
  outVal = json.substring(j, end).toFloat();
  return true;
}

bool extractJsonBool(const String& json, const String& key, bool& outVal) {
  int k = json.indexOf("\"" + key + "\"");
  if (k < 0) return false;
  k = json.indexOf(":", k);
  if (k < 0) return false;
  int j = k + 1;
  while (j < (int)json.length() && json[j] == ' ') j++;
  if (json.startsWith("true", j))  { outVal = true; return true; }
  if (json.startsWith("false", j)) { outVal = false; return true; }
  return false;
}

void printOrDisplay(float v, float i, float s, bool rly) {
  Serial.printf("V=%.2f V, I=%.3f A, S=%.1f VA, Relay=%s\n", v, i, s, rly ? "ON" : "OFF");

#ifdef USE_OLED
  if (display.width() > 0) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("ESP32 Receiver");
    display.printf("V: %.2f V\n", v);
    display.printf("I: %.3f A\n", i);
    display.printf("S: %.1f VA\n", s);
    display.printf("Relay: %s\n", rly ? "ON" : "OFF");
    display.display();
  }
#endif
}

void setup() {
  Serial.begin(115200);
  delay(200);

#ifdef USE_OLED
  Wire.begin(21, 22); // SDA, SCL
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 not found, continuing without OLED.");
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Receiver booting...");
    display.display();
  }
#endif

  // Wi‑Fi connect
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi "); Serial.print(WIFI_SSID); Serial.println(" ...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println();

  // Show IP on Serial
  Serial.print("Receiver connected. IP address: ");
  Serial.println(WiFi.localIP());

#ifdef USE_OLED
  if (display.width() > 0) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("WiFi OK");
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.display();
  }
#endif

  // mDNS for receiver (optional)
  if (!MDNS.begin("esp-receiver")) {
    Serial.println("mDNS start failed (receiver).");
  }

  // Resolve sender address
  if (!resolveSenderIP()) {
    Serial.println("Could not resolve sender IP. Set FALLBACK_SENDER_IP if needed.");
  }

  Serial.println("Type 'o' then Enter to turn Relay ON, 'f' to turn OFF.");
}

void loop() {
  // Keyboard control for relay from Receiver Serial
  if (Serial.available()) {
    char c = Serial.read();
    if (senderIP.length() > 0) {
      if (c == 'o' || c == 'O') {
        String url = "http://" + senderIP + "/relay?state=on";
        String dummy; httpGET(url, dummy);
        Serial.println("Relay -> ON");
      } else if (c == 'f' || c == 'F') {
        String url = "http://" + senderIP + "/relay?state=off";
        String dummy; httpGET(url, dummy);
        Serial.println("Relay -> OFF");
      }
    }
  }

  // Try resolving again if senderIP is empty
  if (senderIP.length() == 0) resolveSenderIP();

  // Poll metrics every 1s
  unsigned long now = millis();
  if (senderIP.length() > 0 && now - lastPoll >= POLL_MS) {
    lastPoll = now;
    String url = "http://" + senderIP + "/metrics";
    String payload;
    if (httpGET(url, payload)) {
      float v = 0, i = 0, s = 0; bool r = false;
      extractJsonNumber(payload, "vrms", v);
      extractJsonNumber(payload, "irms", i);
      extractJsonNumber(payload, "apparent_power_va", s);
      extractJsonBool(payload, "relay_on", r);
      printOrDisplay(v, i, s, r);
    } else {
      Serial.println("Failed to fetch /metrics");
    }
  }
}
