#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <LoRa.h>

// ---------- WiFi ----------
const char* ssid     = "JordyKam";
const char* password = "PirateKingLuffy#2451";

// ---------- LoRa pins ----------
#define LORA_SCK   5
#define LORA_MISO  21
#define LORA_MOSI  19
#define LORA_SS    27
#define LORA_RST   33
#define LORA_DIO0  15
#define LORA_FREQ  915E6

WebServer server(80);

// ----------- LAST SENT (for form persistence) -----------
String lastLat1 = "", lastLon1 = "";
String lastLat2 = "", lastLon2 = "";
String lastLat3 = "", lastLon3 = "";
String lastStatus = "No command sent yet.";

// ----------- LAST RECEIVED (latest parsed) ----------
String rxLat1 = "", rxLon1 = "";
String rxLat2 = "", rxLon2 = "";
String rxLat3 = "", rxLon3 = "";
String lastRxRaw = "";
long   lastRxRSSI = 0;

// ----------- RX LOG (WPT packets) ----------
static const int RX_LOG_MAX = 10;
String rxLog[RX_LOG_MAX];
int rxLogCount = 0;
int rxLogHead  = 0;

// Forward declarations
void handle_OnConnect();
void handle_Send();
void handle_NotFound();
String createHTML();

// ----------------- helpers -----------------

static void addToRxLog(const String& line) {
  rxLog[rxLogHead] = line;
  rxLogHead = (rxLogHead + 1) % RX_LOG_MAX;
  if (rxLogCount < RX_LOG_MAX) rxLogCount++;
}

// Remove non-printable characters and trim
static String sanitizeRx(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    // keep printable ASCII plus newline/carriage return if you want
    if (c >= 32 && c <= 126) out += c;
  }
  out.trim();
  return out;
}

// Find "WPT:" even if garbage is in front
static String extractWPT(const String& msg) {
  int idx = msg.indexOf("WPT:");
  if (idx < 0) return "";
  return msg.substring(idx);
}

// Parse BOTH formats:
// A) WPT:lat1,lon1;lat2,lon2;lat3,lon3;
// B) WPT:lat,lon,alt;
static bool parseWPTflex(const String& msg,
                         String& out1a, String& out1b,
                         String& out2a, String& out2b,
                         String& out3a, String& out3b) {
  String w = extractWPT(msg);
  if (w.length() == 0) return false;

  // remove "WPT:"
  String s = w;
  s.remove(0, 4);
  s.trim();

  // Ensure trailing ';' if it's intended to be terminated
  if (!s.endsWith(";")) s += ";";

  // Decide format:
  // If it has semicolons separating multiple segments with commas inside, we treat as format A.
  // If it looks like "lat,lon,alt;" (2 commas before first ;) treat as format B.
  int firstSemi = s.indexOf(';');
  if (firstSemi < 0) return false;

  String firstSeg = s.substring(0, firstSemi);
  int commaCount = 0;
  for (size_t i = 0; i < firstSeg.length(); i++) if (firstSeg[i] == ',') commaCount++;

  if (commaCount >= 2) {
    // -------- Format B: lat,lon,alt; --------
    int c1 = firstSeg.indexOf(',');
    int c2 = firstSeg.indexOf(',', c1 + 1);
    if (c1 < 0 || c2 < 0) return false;

    String lat = firstSeg.substring(0, c1);     lat.trim();
    String lon = firstSeg.substring(c1 + 1, c2); lon.trim();
    String alt = firstSeg.substring(c2 + 1);     alt.trim();

    if (lat == "" || lon == "" || alt == "") return false;

    // Map into your 3 “waypoint” boxes:
    // WP1 = lat,lon
    // WP2 = alt, (blank)
    // WP3 = blank, blank
    out1a = lat; out1b = lon;
    out2a = alt; out2b = "";
    out3a = "";  out3b = "";
    return true;
  }

  // -------- Format A: lat1,lon1;lat2,lon2;lat3,lon3; --------
  String a[3], b[3];

  for (int i = 0; i < 3; i++) {
    int semi = s.indexOf(';');
    if (semi < 0) return false;

    String pair = s.substring(0, semi);
    s.remove(0, semi + 1);

    int comma = pair.indexOf(',');
    if (comma < 0) return false;

    a[i] = pair.substring(0, comma);     a[i].trim();
    b[i] = pair.substring(comma + 1);    b[i].trim();

    if (a[i] == "" || b[i] == "") return false;
  }

  out1a = a[0]; out1b = b[0];
  out2a = a[1]; out2b = b[1];
  out3a = a[2]; out3b = b[2];
  return true;
}

// ----------------- setup/loop -----------------

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Connecting to:");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 20000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Failed to connect. WiFi.status() = ");
    Serial.println(WiFi.status());
    while (true) { delay(1000); }
  }

  Serial.println("WiFi connected..!");
  Serial.print("Got IP: ");
  Serial.println(WiFi.localIP());

  // ---------- LoRa init ----------
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init failed!");
    while (true) { delay(100); }
  }

  // Force explicit radio params (MUST match the other node)
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();

  Serial.println("LoRa ready (SF7, BW125k, CR4/5, SW0x12, CRC ON).");

  // ---------- Web server routes ----------
  server.on("/", handle_OnConnect);
  server.on("/send", handle_Send);
  server.onNotFound(handle_NotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    Serial.println("========== RX ==========");
    Serial.print("RX packet size: ");
    Serial.println(packetSize);

    String rx = "";
    while (LoRa.available()) {
      rx += (char)LoRa.read();
    }

    lastRxRSSI = LoRa.packetRssi();

    // Keep raw (sanitized) for display/debug
    String clean = sanitizeRx(rx);
    lastRxRaw = clean;

    Serial.print("RX raw (clean): ");
    Serial.println(lastRxRaw);
    Serial.print("RX RSSI: ");
    Serial.println(lastRxRSSI);
    Serial.println("========================");

    String t1a, t1b, t2a, t2b, t3a, t3b;
    if (parseWPTflex(lastRxRaw, t1a, t1b, t2a, t2b, t3a, t3b)) {
      rxLat1 = t1a; rxLon1 = t1b;
      rxLat2 = t2a; rxLon2 = t2b;
      rxLat3 = t3a; rxLon3 = t3b;

      addToRxLog("WPT: " + lastRxRaw + "  (RSSI " + String(lastRxRSSI) + ")");
      lastStatus = "Received WPT (RSSI " + String(lastRxRSSI) + ")";
    } else {
      Serial.println("RX packet was NOT valid WPT (after cleaning/extract)");
    }
  }
}

// --------- HANDLERS ----------

void handle_OnConnect() {
  server.send(200, "text/html", createHTML());
}

void handle_Send() {
  String lat1 = server.hasArg("lat1") ? server.arg("lat1") : "";
  String lon1 = server.hasArg("lon1") ? server.arg("lon1") : "";
  String lat2 = server.hasArg("lat2") ? server.arg("lat2") : "";
  String lon2 = server.hasArg("lon2") ? server.arg("lon2") : "";
  String lat3 = server.hasArg("lat3") ? server.arg("lat3") : "";
  String lon3 = server.hasArg("lon3") ? server.arg("lon3") : "";

  lat1.trim(); lon1.trim();
  lat2.trim(); lon2.trim();
  lat3.trim(); lon3.trim();

  if (lat1 == "" || lon1 == "" || lat2 == "" || lon2 == "" || lat3 == "" || lon3 == "") {
    lastStatus = "Error: please fill ALL lat/lon fields for 3 waypoints.";
    server.send(200, "text/html", createHTML());
    return;
  }

  lastLat1 = lat1; lastLon1 = lon1;
  lastLat2 = lat2; lastLon2 = lon2;
  lastLat3 = lat3; lastLon3 = lon3;

  String packet = "WPT:" + lat1 + "," + lon1 + ";" +
                          lat2 + "," + lon2 + ";" +
                          lat3 + "," + lon3 + ";";

  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();

  Serial.println("========== TX ==========");
  Serial.print("TX packet: ");
  Serial.println(packet);
  Serial.println("========================");

  lastStatus = "Sent over LoRa: " + packet;
  server.send(200, "text/html", createHTML());
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

// --------- HTML PAGE ----------

String createHTML() {
  String dash = "-";

  String str = "<!DOCTYPE html><html>";
  str += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">";
  str += "<title>Cubesat Command Dashboard</title>";
  str += "<style>";
  str += "body{font-family:Arial,sans-serif;color:#f0f0f0;text-align:center;background:#0b1020;}";
  str += ".card{background:#151a2c;padding:20px;margin:18px auto;max-width:560px;border-radius:12px;box-shadow:0 0 10px rgba(0,0,0,0.4);}";
  str += "h1{color:#7fd1ff;}h2{color:#9be7ff;margin-top:0;}";
  str += "label{display:block;text-align:left;margin-top:10px;font-size:14px;}";
  str += "input{width:100%;padding:8px;margin-top:4px;border-radius:6px;border:1px solid #333;background:#101425;color:#f0f0f0;}";
  str += "button{margin-top:16px;padding:10px 18px;border-radius:8px;border:none;background:#2f9bff;color:white;font-weight:bold;cursor:pointer;}";
  str += "button:hover{background:#1c7fd4;}";
  str += ".status{margin-top:12px;font-size:13px;color:#b0ffc4;word-wrap:break-word;}";
  str += ".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;}";
  str += ".smallLabel{font-size:12px;opacity:0.9;text-align:left;}";
  str += ".valBox{padding:8px;border:1px solid #2a2f45;border-radius:8px;background:#0f1324;text-align:left;}";
  str += ".log{margin-top:12px;padding:10px;border:1px solid #2a2f45;border-radius:10px;background:#0f1324;text-align:left;max-height:180px;overflow:auto;}";
  str += ".logLine{font-family:ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, \"Liberation Mono\", \"Courier New\", monospace;font-size:12px;opacity:0.95;margin:6px 0;}";
  str += "</style></head><body>";

  str += "<h1>Cubesat Command Dashboard</h1>";

  // SEND PANEL
  str += "<div class=\"card\"><h2>Send Waypoints (WPT)</h2>";
  str += "<form action=\"/send\" method=\"GET\"><div class=\"grid\">";
  str += "<div><label>Waypoint 1 Latitude:<br><input type=\"text\" name=\"lat1\" value=\"" + lastLat1 + "\"></label></div>";
  str += "<div><label>Waypoint 1 Longitude:<br><input type=\"text\" name=\"lon1\" value=\"" + lastLon1 + "\"></label></div>";
  str += "<div><label>Waypoint 2 Latitude:<br><input type=\"text\" name=\"lat2\" value=\"" + lastLat2 + "\"></label></div>";
  str += "<div><label>Waypoint 2 Longitude:<br><input type=\"text\" name=\"lon2\" value=\"" + lastLon2 + "\"></label></div>";
  str += "<div><label>Waypoint 3 Latitude:<br><input type=\"text\" name=\"lat3\" value=\"" + lastLat3 + "\"></label></div>";
  str += "<div><label>Waypoint 3 Longitude:<br><input type=\"text\" name=\"lon3\" value=\"" + lastLon3 + "\"></label></div>";
  str += "</div><button type=\"submit\">Send to Cubesat</button></form>";
  str += "<div class=\"status\">Last status: " + lastStatus + "</div></div>";

  // RECEIVE PANEL
  str += "<div class=\"card\"><h2>Received WPT Packets</h2>";
  str += "<div class=\"grid\">";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">RX Field 1</div>" + (rxLat1.length()? (rxLat1 + ", " + rxLon1) : dash) + "</div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">RX Field 2</div>" + (rxLat2.length()? (rxLat2 + (rxLon2.length()? (", " + rxLon2):"")) : dash) + "</div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">RX Field 3</div>" + (rxLat3.length()? (rxLat3 + ", " + rxLon3) : dash) + "</div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">RX RSSI</div>" + String(lastRxRSSI) + "</div>";
  str += "</div>";

  str += "<div class=\"smallLabel\" style=\"margin-top:12px;\">WPT Receive Log (most recent first)</div>";
  str += "<div class=\"log\">";
  if (rxLogCount == 0) {
    str += "<div class=\"logLine\">No WPT packets received yet.</div>";
  } else {
    for (int i = 0; i < rxLogCount; i++) {
      int idx = (rxLogHead - 1 - i);
      while (idx < 0) idx += RX_LOG_MAX;
      str += "<div class=\"logLine\">" + rxLog[idx] + "</div>";
    }
  }
  str += "</div>";

  str += "<div class=\"status\" style=\"color:#ffd48a;\">Last RX raw: " + (lastRxRaw.length()? lastRxRaw : String("None")) + "</div>";
  str += "</div>";

  str += "</body></html>";
  return str;
}
