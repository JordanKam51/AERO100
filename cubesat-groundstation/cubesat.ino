#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <SPI.h>
#include <LoRa.h>

#if !defined(ARDUINO_ARCH_ESP32)
#error "This sketch requires an ESP32 board selected in Tools → Board."
#endif

// =====================
// LORA (ORIGINAL PINS)
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
#define I2C_SDA    25
#define I2C_SCL    26
const int MPU = 0x68;

int16_t AcX, AcY, AcZ, Tmp, GyX, GyY, GyZ;
int AcXcal, AcYcal, AcZcal, GyXcal, GyYcal, GyZcal, tcal;
double tC, tx, tF, pitch, roll;

// =====================
// MAGNETORQUER
// =====================
#define MAG_PIN 13
bool magOn = false;

// =====================
// STEPPER (A4988/DRV8825 STEP/DIR)
// =====================
// CHANGE THESE to your wiring. These are safe defaults.
#define STEPPER_STEP_PIN 32
#define STEPPER_DIR_PIN  14
#define STEPPER_EN_PIN   -1   // set to a GPIO if used, else keep -1

const int      STEPS_EACH_SEC = 200;   // steps per second command
const uint32_t STEP_EDGE_US   = 800;   // toggles edge timing; smaller=faster

// Non-blocking stepper driver (toggle step pin edges)
volatile int stepsRemainingEdges = 0;
bool stepPinState = LOW;
uint32_t nextStepUs = 0;
bool stepperDir = false;

// =====================
// TIMING
// =====================
unsigned long last1HzMs = 0;
int counter = 0;

// =====================
// HELPERS
// =====================
void getAngle(int Ax, int Ay, int Az) {
  double x = Ax;
  double y = Ay;
  double z = Az;

  pitch = atan(x / sqrt((y * y) + (z * z)));
  roll  = atan(y / sqrt((x * x) + (z * z)));

  pitch = pitch * (180.0 / 3.14);
  roll  = roll  * (180.0 / 3.14);
}

void sampleIMU() {
  Wire.beginTransmission(MPU);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU, 14, true);

  // Your calibration offsets
  AcXcal = -950;
  AcYcal = -300;
  AcZcal = 0;
  tcal   = -1600;
  GyXcal = 480;
  GyYcal = 170;
  GyZcal = 210;

  AcX = Wire.read() << 8 | Wire.read();
  AcY = Wire.read() << 8 | Wire.read();
  AcZ = Wire.read() << 8 | Wire.read();
  Tmp = Wire.read() << 8 | Wire.read();
  GyX = Wire.read() << 8 | Wire.read();
  GyY = Wire.read() << 8 | Wire.read();
  GyZ = Wire.read() << 8 | Wire.read();

  tx = Tmp + tcal;
  tC = tx / 340.0 + 36.53;
  tF = (tC * 9.0 / 5.0) + 32.0;

  getAngle(AcX, AcY, AcZ);
}

void startStepperMove(int steps) {
  // 1 pulse = HIGH then LOW, so 2 edges per step
  stepsRemainingEdges = steps * 2;
  stepPinState = LOW;
  digitalWrite(STEPPER_STEP_PIN, stepPinState);
  nextStepUs = micros();
}

void serviceStepper() {
  if (stepsRemainingEdges <= 0) return;

  uint32_t now = micros();
  if ((int32_t)(now - nextStepUs) >= 0) {
    stepPinState = !stepPinState;
    digitalWrite(STEPPER_STEP_PIN, stepPinState);
    stepsRemainingEdges--;
    nextStepUs = now + STEP_EDGE_US;
  }
}

void sendLoRaTelemetry() {
  int ax = AcX + AcXcal;
  int ay = AcY + AcYcal;
  int az = AcZ + AcZcal;
  int gx = GyX + GyXcal;
  int gy = GyY + GyYcal;
  int gz = GyZ + GyZcal;

  // Packet format (easy to parse):
  // IMU,<ctr>,<pitch>,<roll>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>,<tC>,<tF>,MAG,<0/1>,DIR,<0/1>,STEPS,<N>
  String packet;
  packet.reserve(220);

  packet += "IMU,";
  packet += String(counter); packet += ",";
  packet += String(pitch, 2); packet += ",";
  packet += String(roll, 2);  packet += ",";
  packet += String(ax);       packet += ",";
  packet += String(ay);       packet += ",";
  packet += String(az);       packet += ",";
  packet += String(gx);       packet += ",";
  packet += String(gy);       packet += ",";
  packet += String(gz);       packet += ",";
  packet += String(tC, 2);    packet += ",";
  packet += String(tF, 2);    packet += ",";
  packet += "MAG,";           packet += (magOn ? "1" : "0"); packet += ",";
  packet += "DIR,";           packet += (stepperDir ? "1" : "0"); packet += ",";
  packet += "STEPS,";         packet += String(STEPS_EACH_SEC);

  LoRa.beginPacket();
  LoRa.print(packet);
  LoRa.endPacket();

  Serial.println("========== TX ==========");
  Serial.print("TX packet: ");
  Serial.println(packet);
  Serial.println("========================");
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("SENDER boot: LoRa + IMU + MAG + STEPPER");

  // Magnetorquer
  pinMode(MAG_PIN, OUTPUT);
  digitalWrite(MAG_PIN, LOW);

  // Stepper pins
  pinMode(STEPPER_STEP_PIN, OUTPUT);
  pinMode(STEPPER_DIR_PIN, OUTPUT);
  digitalWrite(STEPPER_STEP_PIN, LOW);
  digitalWrite(STEPPER_DIR_PIN, LOW);

  if (STEPPER_EN_PIN >= 0) {
    pinMode(STEPPER_EN_PIN, OUTPUT);
    // many drivers: enable LOW
    digitalWrite(STEPPER_EN_PIN, LOW);
  }

  // I2C + MPU6050
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.beginTransmission(MPU);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);

  // LoRa init (original pins)
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);

  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init failed!");
    while (true) { delay(100); }
  }

  // Match your other node settings
  LoRa.setSpreadingFactor(7);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();

  Serial.println("LoRa ready (SF7, BW125k, CR4/5, SW0x12, CRC ON).");

  last1HzMs = millis();
}

void loop() {
  serviceStepper();

  unsigned long nowMs = millis();
  if (nowMs - last1HzMs >= 1000) {
    last1HzMs += 1000;

    // 1) Sample IMU
    sampleIMU();

    // 2) Toggle magnetorquer
    magOn = !magOn;
    digitalWrite(MAG_PIN, magOn ? HIGH : LOW);
    Serial.print("MAG: "); Serial.println(magOn ? "ON" : "OFF");

    // 3) Stepper move each second
    stepperDir = !stepperDir;
    digitalWrite(STEPPER_DIR_PIN, stepperDir ? HIGH : LOW);
    startStepperMove(STEPS_EACH_SEC);
    Serial.print("Stepper: DIR="); Serial.print(stepperDir ? 1 : 0);
    Serial.print(" STEPS="); Serial.println(STEPS_EACH_SEC);

    // 4) Send telemetry over LoRa
    sendLoRaTelemetry();

    counter++;
  }
}
