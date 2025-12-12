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
int rxLogCount = 0;   // number of valid entries
int rxLogHead  = 0;   // next write index (ring buffer)

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

static bool parseWPT(const String& msg,
                     String& outLat1, String& outLon1,
                     String& outLat2, String& outLon2,
                     String& outLat3, String& outLon3) {
  // Expected: "WPT:lat1,lon1;lat2,lon2;lat3,lon3;"
  if (!msg.startsWith("WPT:")) return false;

  String s = msg;
  s.remove(0, 4); // remove "WPT:"

  // Ensure trailing ';'
  if (!s.endsWith(";")) s += ";";

  String lat[3], lon[3];

  for (int i = 0; i < 3; i++) {
    int semi = s.indexOf(';');
    if (semi < 0) return false;

    String pair = s.substring(0, semi);
    s.remove(0, semi + 1);

    int comma = pair.indexOf(',');
    if (comma < 0) return false;

    lat[i] = pair.substring(0, comma);
    lon[i] = pair.substring(comma + 1);

    lat[i].trim();
    lon[i].trim();

    if (lat[i].length() == 0 || lon[i].length() == 0) return false;
  }

  outLat1 = lat[0]; outLon1 = lon[0];
  outLat2 = lat[1]; outLon2 = lon[1];
  outLat3 = lat[2]; outLon3 = lon[2];
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

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttemptTime < 20000) {
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
  server.on("/", handle_OnConnect);   // dashboard page
  server.on("/send", handle_Send);    // form submission
  server.onNotFound(handle_NotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  // ---------- LoRa receive (non-blocking) ----------
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    Serial.println("========== RX ==========");
    Serial.print("RX packet size: ");
    Serial.println(packetSize);

    String rx = "";
    while (LoRa.available()) {
      char c = (char)LoRa.read();
      rx += c;
    }

    lastRxRSSI = LoRa.packetRssi();
    lastRxRaw = rx;

    Serial.print("RX raw: ");
    Serial.println(lastRxRaw);
    Serial.print("RX RSSI: ");
    Serial.println(lastRxRSSI);
    Serial.println("========================");

    // If it is WPT, parse/store and add to log
    String tLat1, tLon1, tLat2, tLon2, tLat3, tLon3;
    if (parseWPT(lastRxRaw, tLat1, tLon1, tLat2, tLon2, tLat3, tLon3)) {
      rxLat1 = tLat1; rxLon1 = tLon1;
      rxLat2 = tLat2; rxLon2 = tLon2;
      rxLat3 = tLat3; rxLon3 = tLon3;

      addToRxLog(lastRxRaw + "  (RSSI " + String(lastRxRSSI) + ")");
      lastStatus = "Received WPT packet (RSSI " + String(lastRxRSSI) + ")";
    } else {
      Serial.println("RX packet was NOT valid WPT");
    }
  }
}

// --------- HANDLERS ----------

void handle_OnConnect() {
  server.send(200, "text/html", createHTML());
}

void handle_Send() {
  // Expect:
  // /send?lat1=...&lon1=...&lat2=...&lon2=...&lat3=...&lon3=...

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

  // Store for the page
  lastLat1 = lat1; lastLon1 = lon1;
  lastLat2 = lat2; lastLon2 = lon2;
  lastLat3 = lat3; lastLon3 = lon3;

  // Build LoRa packet with trailing semicolon:
  // "WPT:lat1,lon1;lat2,lon2;lat3,lon3;"
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
  String str = "<!DOCTYPE html><html>";
  str += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">";
  str += "<title>Cubesat Command Dashboard</title>";
  str += "<style>";
  str += "body{font-family:Arial,sans-serif;color:#f0f0f0;text-align:center;background:#0b1020;}";
  str += ".card{background:#151a2c;padding:20px;margin:18px auto;max-width:560px;border-radius:12px;box-shadow:0 0 10px rgba(0,0,0,0.4);}";
  str += "h1{color:#7fd1ff;}";
  str += "h2{color:#9be7ff;margin-top:0;}";
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
  str += ".muted{opacity:0.7;}";
  str += "</style>";
  str += "</head>";

  str += "<body>";
  str += "<h1>Cubesat Command Dashboard</h1>";

  // ---- PANEL 1: SEND ----
  str += "<div class=\"card\">";
  str += "<h2>Send Waypoints (WPT)</h2>";
  str += "<form action=\"/send\" method=\"GET\">";
  str += "<div class=\"grid\">";

  str += "<div><label>Waypoint 1 Latitude:<br><input type=\"text\" name=\"lat1\" placeholder=\"e.g. 37.427500\" value=\"" + lastLat1 + "\"></label></div>";
  str += "<div><label>Waypoint 1 Longitude:<br><input type=\"text\" name=\"lon1\" placeholder=\"e.g. -122.169700\" value=\"" + lastLon1 + "\"></label></div>";

  str += "<div><label>Waypoint 2 Latitude:<br><input type=\"text\" name=\"lat2\" placeholder=\"e.g. 37.428000\" value=\"" + lastLat2 + "\"></label></div>";
  str += "<div><label>Waypoint 2 Longitude:<br><input type=\"text\" name=\"lon2\" placeholder=\"e.g. -122.170200\" value=\"" + lastLon2 + "\"></label></div>";

  str += "<div><label>Waypoint 3 Latitude:<br><input type=\"text\" name=\"lat3\" placeholder=\"e.g. 37.428500\" value=\"" + lastLat3 + "\"></label></div>";
  str += "<div><label>Waypoint 3 Longitude:<br><input type=\"text\" name=\"lon3\" placeholder=\"e.g. -122.170800\" value=\"" + lastLon3 + "\"></label></div>";

  str += "</div>";
  str += "<button type=\"submit\">Send to Cubesat</button>";
  str += "</form>";
  str += "<div class=\"status\">Last status: " + lastStatus + "</div>";
  str += "</div>";

  // ---- PANEL 2: RECEIVE ----
  str += "<div class=\"card\">";
  str += "<h2>Received WPT Packets</h2>";

  // Latest parsed display
  str += "<div class=\"grid\">";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">Latest RX Waypoint 1 (lat, lon)</div>" +
         (rxLat1.length() ? (rxLat1 + ", " + rxLon1) : String("<span class='muted'>—</span>")) + "</div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">Latest RX Waypoint 2 (lat, lon)</div>" +
         (rxLat2.length() ? (rxLat2 + ", " + rxLon2) : String("<span class='muted'>—</span>")) + "</div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">Latest RX Waypoint 3 (lat, lon)</div>" +
         (rxLat3.length() ? (rxLat3 + ", " + rxLon3) : String("<span class='muted'>—</span>")) + "</div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">Latest RX RSSI</div>" + String(lastRxRSSI) + "</div>";
  str += "</div>";

  // Log of all WPT packets received
  str += "<div class=\"smallLabel\" style=\"margin-top:12px;\">WPT Receive Log (most recent first)</div>";
  str += "<div class=\"log\">";

  if (rxLogCount == 0) {
    str += "<div class=\"logLine muted\">No WPT packets received yet.</div>";
  } else {
    for (int i = 0; i < rxLogCount; i++) {
      int idx = (rxLogHead - 1 - i);
      while (idx < 0) idx += RX_LOG_MAX;
      str += "<div class=\"logLine\">";
      str += rxLog[idx];
      str += "</div>";
    }
  }

  str += "</div>"; // log

  // Raw last RX (debug)
  str += "<div class=\"status\" style=\"color:#ffd48a;\">Last RX raw: ";
  str += (lastRxRaw.length() ? lastRxRaw : String("None"));
  str += "</div>";

  str += "</div>"; // receive card

  str += "</body></html>";
  return str;
}
