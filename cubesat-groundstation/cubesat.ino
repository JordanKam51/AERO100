// sender_strict_match.ino
// ESC-style PWM on GPIO32 (50 Hz, 1000–2000 us)
// Magnetorquer on GPIO14

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LoRa.h>
#include <math.h>

// =====================
// LORA
// =====================
#define LORA_SCK   5
#define LORA_MISO  21
#define LORA_MOSI  19
#define LORA_SS    27
#define LORA_RST   33
#define LORA_DIO0  15
#define LORA_FREQ  915E6

// =====================
// IMU (MPU6050)
// =====================
#define I2C_SDA 25
#define I2C_SCL 26
#define MPU 0x68

int16_t AcX, AcY, AcZ, Tmp, GyX, GyY, GyZ;
double pitch = 0, roll = 0, tC = 0, tF = 0;

// =====================
// MAGNETORQUER
// =====================
#define MAG_PIN 14
bool magOn = false;

// =====================
// ESC / REACTION WHEEL
// =====================
#define RW_PIN 32
bool rwEnabled = false;

// PWM parameters (match MicroPython)
#define ESC_FREQ_HZ     50
#define ESC_PWM_BITS    16
#define ESC_PWM_MAX     65535

#define ESC_US_MIN      1000
#define ESC_US_MAX      2000
#define ESC_US_IDLE     1000
#define ESC_US_RUN      1700   // throttle when RW=1 (adjust)

// =====================
// TIMING
// =====================
unsigned long lastTx = 0;
int counter = 0;

// =====================
// HELPERS
// =====================
uint32_t usToDuty(uint16_t us) {
  // duty = (us / 20000) * 65535
  return (uint32_t)((((uint32_t)us) * ESC_PWM_MAX) / 20000UL);
}

void setESCmicroseconds(uint16_t us) {
  if (us < ESC_US_MIN) us = ESC_US_MIN;
  if (us > ESC_US_MAX) us = ESC_US_MAX;
  analogWrite(RW_PIN, usToDuty(us));
}

void applyOutputs() {
  digitalWrite(MAG_PIN, magOn ? HIGH : LOW);

  if (rwEnabled) {
    setESCmicroseconds(ESC_US_RUN);
  } else {
    setESCmicroseconds(ESC_US_IDLE);
  }
}

void getAngle(int Ax, int Ay, int Az) {
  pitch = atan((double)Ax / sqrt((double)Ay * Ay + (double)Az * Az)) * 180.0 / PI;
  roll  = atan((double)Ay / sqrt((double)Ax * Ax + (double)Az * Az)) * 180.0 / PI;
}

void sampleIMU() {
  Wire.beginTransmission(MPU);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU, 14, true);

  AcX = (Wire.read() << 8) | Wire.read();
  AcY = (Wire.read() << 8) | Wire.read();
  AcZ = (Wire.read() << 8) | Wire.read();
  Tmp = (Wire.read() << 8) | Wire.read();
  GyX = (Wire.read() << 8) | Wire.read();
  GyY = (Wire.read() << 8) | Wire.read();
  GyZ = (Wire.read() << 8) | Wire.read();

  tC = (double)Tmp / 340.0 + 36.53;
  tF = tC * 9.0 / 5.0 + 32.0;

  getAngle(AcX, AcY, AcZ);
}

static String sanitizeText(const String& in) {
  String out;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c >= 32 && c <= 126) out += c;
  }
  out.trim();
  return out;
}

// =====================
// COMMAND RX
// =====================
void handleLoRaCommands() {
  int packetSize = LoRa.parsePacket();
  if (!packetSize) return;

  String cmd;
  while (LoRa.available()) cmd += (char)LoRa.read();
  cmd = sanitizeText(cmd);

  if (!cmd.startsWith("CMD:")) return;
  if (!cmd.endsWith(";")) return;

  if (cmd.indexOf("MAG=1") >= 0) magOn = true;
  if (cmd.indexOf("MAG=0") >= 0) magOn = false;

  if (cmd.indexOf("RW=1") >= 0) rwEnabled = true;
  if (cmd.indexOf("RW=0") >= 0) rwEnabled = false;

  applyOutputs();

  Serial.print("RX CMD: ");
  Serial.println(cmd);
}

// =====================
// TELEMETRY
// =====================
void sendTelemetry() {
  String pkt;
  pkt.reserve(220);

  pkt += "IMU,";
  pkt += counter; pkt += ",";
  pkt += pitch; pkt += ",";
  pkt += roll; pkt += ",";
  pkt += AcX; pkt += ",";
  pkt += AcY; pkt += ",";
  pkt += AcZ; pkt += ",";
  pkt += GyX; pkt += ",";
  pkt += GyY; pkt += ",";
  pkt += GyZ; pkt += ",";
  pkt += tC; pkt += ",";
  pkt += tF; pkt += ",";
  pkt += "MAG,"; pkt += (magOn ? "1" : "0"); pkt += ",";
  pkt += "RW,";  pkt += (rwEnabled ? "1" : "0");

  LoRa.beginPacket();
  LoRa.print(pkt);
  LoRa.endPacket();
  LoRa.receive();

  Serial.println(pkt);
}

// =====================
// SETUP
// =====================
void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(MAG_PIN, OUTPUT);
  pinMode(RW_PIN, OUTPUT);

  // ESC PWM setup (ESP32 core 3.x)
  analogWriteFrequency(RW_PIN, ESC_FREQ_HZ);
  analogWriteResolution(RW_PIN, ESC_PWM_BITS);

  // Arm ESC
  setESCmicroseconds(ESC_US_IDLE);
  delay(3000);   // match MicroPython arming delay

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.beginTransmission(MPU);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  LoRa.begin(LORA_FREQ);
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();
  LoRa.setPreambleLength(8);
  LoRa.setTxPower(17);
  LoRa.receive();

  lastTx = millis();
  Serial.println("Sender ready: ESC PWM on GPIO32, MAG on GPIO14");
}

// =====================
// LOOP
// =====================
void loop() {
  handleLoRaCommands();

  if (millis() - lastTx >= 1000) {
    lastTx += 1000;
    sampleIMU();
    sendTelemetry();
    counter++;
  }
}
