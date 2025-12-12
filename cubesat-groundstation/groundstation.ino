#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <LoRa.h>

// ---------- WiFi ----------
const char* ssid     = "JordyKam";
const char* password = "PirateKingLuffy#2451";

// ---------- LoRa pins (ORIGINAL) ----------
#define LORA_SCK   5
#define LORA_MISO  21
#define LORA_MOSI  19
#define LORA_SS    27
#define LORA_RST   33
#define LORA_DIO0  15
#define LORA_FREQ  915E6

WebServer server(80);

// ----------- LAST RECEIVED (parsed) ----------
String lastRxRaw = "";
long   lastRxRSSI = 0;
unsigned long lastRxMs = 0;

// Parsed telemetry fields
String f_ctr = "-";
String f_pitch = "-";
String f_roll = "-";
String f_ax = "-";
String f_ay = "-";
String f_az = "-";
String f_gx = "-";
String f_gy = "-";
String f_gz = "-";
String f_tC = "-";
String f_tF = "-";
String f_mag = "-";
String f_dir = "-";
String f_steps = "-";

// RX log
static const int RX_LOG_MAX = 12;
String rxLog[RX_LOG_MAX];
int rxLogCount = 0;
int rxLogHead  = 0;

void handle_OnConnect();
void handle_NotFound();
String createHTML();

// ----------------- helpers -----------------
static void addToRxLog(const String& line) {
  rxLog[rxLogHead] = line;
  rxLogHead = (rxLogHead + 1) % RX_LOG_MAX;
  if (rxLogCount < RX_LOG_MAX) rxLogCount++;
}

static String sanitizeRx(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c >= 32 && c <= 126) out += c;  // printable ASCII only
  }
  out.trim();
  return out;
}

// parse: IMU,<ctr>,<pitch>,<roll>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>,<tC>,<tF>,MAG,<0/1>,DIR,<0/1>,STEPS,<N>
static bool parseTelemetry(const String& msg) {
  if (!msg.startsWith("IMU,")) return false;

  // tokenize by commas (simple)
  String toks[40];
  int nt = 0;

  int start = 0;
  while (nt < 40) {
    int comma = msg.indexOf(',', start);
    if (comma < 0) {
      toks[nt++] = msg.substring(start);
      break;
    } else {
      toks[nt++] = msg.substring(start, comma);
      start = comma + 1;
    }
  }

  // expected minimum tokens:
  // 0 IMU
  // 1 ctr
  // 2 pitch
  // 3 roll
  // 4 ax
  // 5 ay
  // 6 az
  // 7 gx
  // 8 gy
  // 9 gz
  // 10 tC
  // 11 tF
  // 12 MAG
  // 13 0/1
  // 14 DIR
  // 15 0/1
  // 16 STEPS
  // 17 N
  if (nt < 18) return false;
  if (toks[12] != "MAG" || toks[14] != "DIR" || toks[16] != "STEPS") return false;

  f_ctr   = toks[1];
  f_pitch = toks[2];
  f_roll  = toks[3];
  f_ax    = toks[4];
  f_ay    = toks[5];
  f_az    = toks[6];
  f_gx    = toks[7];
  f_gy    = toks[8];
  f_gz    = toks[9];
  f_tC    = toks[10];
  f_tF    = toks[11];
  f_mag   = (toks[13] == "1") ? "ON" : "OFF";
  f_dir   = (toks[15] == "1") ? "1" : "0";
  f_steps = toks[17];

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

  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();

  Serial.println("LoRa ready (SF7, BW125k, CR4/5, SW0x12, CRC ON).");

  // ---------- Web server routes ----------
  server.on("/", handle_OnConnect);
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
    while (LoRa.available()) rx += (char)LoRa.read();

    lastRxRSSI = LoRa.packetRssi();
    lastRxRaw  = sanitizeRx(rx);
    lastRxMs   = millis();

    Serial.print("RX raw (clean): ");
    Serial.println(lastRxRaw);
    Serial.print("RX RSSI: ");
    Serial.println(lastRxRSSI);
    Serial.println("========================");

    bool ok = parseTelemetry(lastRxRaw);
    if (ok) {
      addToRxLog(lastRxRaw + "  (RSSI " + String(lastRxRSSI) + ")");
      Serial.println("Parsed telemetry OK");
    } else {
      addToRxLog("UNPARSED: " + lastRxRaw + "  (RSSI " + String(lastRxRSSI) + ")");
      Serial.println("RX packet was NOT valid telemetry");
    }
  }
}

// --------- HANDLERS ----------
void handle_OnConnect() {
  server.send(200, "text/html", createHTML());
}

void handle_NotFound() {
  server.send(404, "text/plain", "Not found");
}

// --------- HTML PAGE ----------
String createHTML() {
  String str = "<!DOCTYPE html><html>";
  str += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">";
  str += "<meta http-equiv=\"refresh\" content=\"2\">"; // auto-refresh every 2s
  str += "<title>Telemetry Dashboard</title>";
  str += "<style>";
  str += "body{font-family:Arial,sans-serif;color:#f0f0f0;text-align:center;background:#0b1020;}";
  str += ".card{background:#151a2c;padding:20px;margin:18px auto;max-width:860px;border-radius:12px;box-shadow:0 0 10px rgba(0,0,0,0.4);}";
  str += "h1{color:#7fd1ff;}h2{color:#9be7ff;margin-top:0;}";
  str += ".grid{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;}";
  str += ".smallLabel{font-size:12px;opacity:0.9;text-align:left;}";
  str += ".valBox{padding:10px;border:1px solid #2a2f45;border-radius:10px;background:#0f1324;text-align:left;}";
  str += ".mono{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,\"Liberation Mono\",\"Courier New\",monospace;}";
  str += ".status{margin-top:12px;font-size:13px;color:#b0ffc4;word-wrap:break-word;text-align:left;}";
  str += ".log{margin-top:12px;padding:10px;border:1px solid #2a2f45;border-radius:10px;background:#0f1324;text-align:left;max-height:220px;overflow:auto;}";
  str += ".logLine{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,\"Liberation Mono\",\"Courier New\",monospace;font-size:12px;opacity:0.95;margin:6px 0;}";
  str += "</style></head><body>";

  str += "<h1>Cubesat Telemetry Dashboard</h1>";

  // Panel: Latest parsed telemetry
  str += "<div class=\"card\"><h2>Latest Telemetry</h2>";
  str += "<div class=\"grid\">";

  str += "<div class=\"valBox\"><div class=\"smallLabel\">Counter</div><div class=\"mono\">" + f_ctr + "</div></div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">Pitch (deg)</div><div class=\"mono\">" + f_pitch + "</div></div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">Roll (deg)</div><div class=\"mono\">" + f_roll + "</div></div>";

  str += "<div class=\"valBox\"><div class=\"smallLabel\">AcX</div><div class=\"mono\">" + f_ax + "</div></div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">AcY</div><div class=\"mono\">" + f_ay + "</div></div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">AcZ</div><div class=\"mono\">" + f_az + "</div></div>";

  str += "<div class=\"valBox\"><div class=\"smallLabel\">GyX</div><div class=\"mono\">" + f_gx + "</div></div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">GyY</div><div class=\"mono\">" + f_gy + "</div></div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">GyZ</div><div class=\"mono\">" + f_gz + "</div></div>";

  str += "<div class=\"valBox\"><div class=\"smallLabel\">Temp (C)</div><div class=\"mono\">" + f_tC + "</div></div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">Temp (F)</div><div class=\"mono\">" + f_tF + "</div></div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">Magnetorquer</div><div class=\"mono\">" + f_mag + "</div></div>";

  str += "<div class=\"valBox\"><div class=\"smallLabel\">Stepper DIR</div><div class=\"mono\">" + f_dir + "</div></div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">Stepper Steps Cmd</div><div class=\"mono\">" + f_steps + "</div></div>";
  str += "<div class=\"valBox\"><div class=\"smallLabel\">RX RSSI</div><div class=\"mono\">" + String(lastRxRSSI) + "</div></div>";

  str += "</div>"; // grid

  unsigned long age = (lastRxMs == 0) ? 0 : (millis() - lastRxMs);
  str += "<div class=\"status\">Last RX age: <span class=\"mono\">" + String(age) + " ms</span></div>";
  str += "<div class=\"status\" style=\"color:#ffd48a;\">Last RX raw: <span class=\"mono\">" + (lastRxRaw.length() ? lastRxRaw : String("None")) + "</span></div>";
  str += "</div>";

  // Panel: log
  str += "<div class=\"card\"><h2>Receive Log</h2>";
  str += "<div class=\"log\">";

  if (rxLogCount == 0) {
    str += "<div class=\"logLine\">No packets received yet.</div>";
  } else {
    for (int i = 0; i < rxLogCount; i++) {
      int idx = (rxLogHead - 1 - i);
      while (idx < 0) idx += RX_LOG_MAX;
      str += "<div class=\"logLine\">" + rxLog[idx] + "</div>";
    }
  }

  str += "</div></div>";

  str += "</body></html>";
  return str;
}
