// http://172.20.10.5/capture

#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"

// =======================
// HOTSPOT Wi-Fi CONFIG
// =======================
const char* WIFI_SSID = "JordyKam";            // <-- your hotspot SSID
const char* WIFI_PASS = "PirateKingLuffy#2451"; // <-- your hotspot password

WebServer server(80);

// =======================================================
// CAMERA PIN MAP (MATCH THIS TO YOUR TimerX MODULE)
// =======================================================
// If camera init fails, this is the FIRST thing to verify.

#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  15
#define CAM_PIN_XCLK   27
#define CAM_PIN_SIOD   25
#define CAM_PIN_SIOC   23

#define CAM_PIN_D7     19
#define CAM_PIN_D6     36
#define CAM_PIN_D5     18
#define CAM_PIN_D4     39
#define CAM_PIN_D3     5
#define CAM_PIN_D2     34
#define CAM_PIN_D1     35
#define CAM_PIN_D0     32

#define CAM_PIN_VSYNC  22
#define CAM_PIN_HREF   26
#define CAM_PIN_PCLK   21

// =======================
// CAMERA INIT
// =======================
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;

  config.pin_d0       = CAM_PIN_D0;
  config.pin_d1       = CAM_PIN_D1;
  config.pin_d2       = CAM_PIN_D2;
  config.pin_d3       = CAM_PIN_D3;
  config.pin_d4       = CAM_PIN_D4;
  config.pin_d5       = CAM_PIN_D5;
  config.pin_d6       = CAM_PIN_D6;
  config.pin_d7       = CAM_PIN_D7;

  config.pin_xclk     = CAM_PIN_XCLK;
  config.pin_pclk     = CAM_PIN_PCLK;
  config.pin_vsync    = CAM_PIN_VSYNC;
  config.pin_href     = CAM_PIN_HREF;

  config.pin_sccb_sda = CAM_PIN_SIOD;
  config.pin_sccb_scl = CAM_PIN_SIOC;

  config.pin_pwdn     = CAM_PIN_PWDN;
  config.pin_reset    = CAM_PIN_RESET;

  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Choose resolution based on PSRAM availability
  if (psramFound()) {
    config.frame_size   = FRAMESIZE_UXGA;  // 1600x1200
    config.jpeg_quality = 10;              // lower = better (0-63)
    config.fb_count     = 2;
  } else {
    config.frame_size   = FRAMESIZE_SVGA;  // 800x600
    config.jpeg_quality = 12;
    config.fb_count     = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed: 0x%x\n", err);
    return false;
  }

  // Ensure sensor matches the chosen size
  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, psramFound() ? FRAMESIZE_UXGA : FRAMESIZE_SVGA);

    // If the image is upside down, uncomment ONE of these:
    // s->set_vflip(s, 1);
    // s->set_hmirror(s, 1);
  }

  return true;
}

// =======================
// WEB ROUTES
// =======================
void handleRoot() {
  String html =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<title>TimerX Capture</title></head><body style='font-family:sans-serif;'>"
    "<h2>TimerX Hi-Res Capture</h2>"
    "<p><a href='/capture'>Capture & Download JPEG</a></p>"
    "<p><a href='/view'>View in Browser</a></p>"
    "<p><a href='/status'>Status</a></p>"
    "</body></html>";
  server.send(200, "text/html", html);
}

void handleStatus() {
  String s;
  s += "SSID: " + String(WIFI_SSID) + "\n";
  s += "IP: " + WiFi.localIP().toString() + "\n";
  s += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
  s += "PSRAM: " + String(psramFound() ? "YES" : "NO") + "\n";
  server.send(200, "text/plain", s);
}

// Returns a JPEG as a downloadable file
void handleCapture() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Capture failed (no frame buffer).");
    return;
  }

  // Critical: set content length so browser doesn't save 0 bytes
  server.sendHeader("Content-Disposition", "attachment; filename=\"timerx.jpg\"");
  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(fb->len);

  // Start response with empty body, then write raw bytes
  server.send(200, "image/jpeg", "");

  WiFiClient client = server.client();
  size_t sent = client.write((const uint8_t*)fb->buf, fb->len);
  client.flush();

  Serial.printf("JPEG size=%u, sent=%u\n", (unsigned)fb->len, (unsigned)sent);

  esp_camera_fb_return(fb);
}

// Returns a JPEG inline (preview in browser)
void handleView() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Capture failed (no frame buffer).");
    return;
  }

  server.sendHeader("Cache-Control", "no-store");
  server.setContentLength(fb->len);
  server.send(200, "image/jpeg", "");

  WiFiClient client = server.client();
  size_t sent = client.write((const uint8_t*)fb->buf, fb->len);
  client.flush();

  Serial.printf("VIEW JPEG size=%u, sent=%u\n", (unsigned)fb->len, (unsigned)sent);

  esp_camera_fb_return(fb);
}

// =======================
// SETUP / LOOP
// =======================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nTimerX Hi-Res Capture (Fixed) booting...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to WiFi");
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
    if (millis() - t0 > 20000) {
      Serial.println("\nWiFi connect timeout. Check SSID/PASS and hotspot settings.");
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected.");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Continuing without WiFi (server will not be reachable).");
  }

  if (!initCamera()) {
    Serial.println("Camera failed to initialize. Check pin map + power + board selection.");
    while (true) delay(1000);
  }
  Serial.println("Camera initialized.");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/view", HTTP_GET, handleView);

  server.begin();
  Serial.println("HTTP server started.");
  Serial.println("Open these on your laptop (same hotspot):");
  Serial.print("  http://"); Serial.print(WiFi.localIP()); Serial.println("/");
  Serial.print("  http://"); Serial.print(WiFi.localIP()); Serial.println("/capture");
  Serial.print("  http://"); Serial.print(WiFi.localIP()); Serial.println("/view");
}

void loop() {
  server.handleClient();
}
