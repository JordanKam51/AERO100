#include <Wire.h>
#include <math.h>
#include <SPI.h>
#include <LoRa.h>


#define LORA_SCK   5
#define LORA_MISO  21
#define LORA_MOSI  19
#define LORA_SS    27
#define LORA_RST   33
#define LORA_DIO0  15
#define LORA_FREQ  915E6


#define I2C_SDA    25
#define I2C_SCL    26


const int MPU = 0x68;
int16_t AcX, AcY, AcZ, Tmp, GyX, GyY, GyZ;
int AcXcal, AcYcal, AcZcal, GyXcal, GyYcal, GyZcal, tcal;
double t, tx, tf, pitch, roll;


int counter = 0;


void setup() {
 Serial.begin(115200);
 delay(1000);
 Serial.println("ESP32 LoRa IMU sender booting...");


 // I2C init (use ESP32 pins 25/26)
 Wire.begin(I2C_SDA, I2C_SCL);


 // Wake up MPU6050
 Wire.beginTransmission(MPU);
 Wire.write(0x6B);   // PWR_MGMT_1
 Wire.write(0);      // set to zero (wakes up the MPU-6050)
 Wire.endTransmission(true);


 // LoRa init
 SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
 LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);


 if (!LoRa.begin(LORA_FREQ)) {
   Serial.println("LoRa init failed!");
   while (true) { delay(100); }
 }
 Serial.println("LoRa sender ready.");
}


void loop() {

 Wire.beginTransmission(MPU);
 Wire.write(0x3B);           
 Wire.endTransmission(false);
 Wire.requestFrom(MPU, 14, true);  


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
 t  = tx / 340.0 + 36.53;       
 tf = (t * 9.0 / 5.0) + 32.0;   


 getAngle(AcX, AcY, AcZ);       


 LoRa.beginPacket();


 LoRa.print(counter); LoRa.print(',');
 LoRa.print(pitch, 2); LoRa.print(',');
 LoRa.print(roll, 2);  LoRa.print(',');


 LoRa.print(AcX + AcXcal); LoRa.print(',');
 LoRa.print(AcY + AcYcal); LoRa.print(',');
 LoRa.print(AcZ + AcZcal); LoRa.print(',');


 LoRa.print(GyX + GyXcal); LoRa.print(',');
 LoRa.print(GyY + GyYcal); LoRa.print(',');
 LoRa.print(GyZ + GyZcal); LoRa.print(',');


 LoRa.print(t, 2);  LoRa.print(',');
 LoRa.print(tf, 2);


 LoRa.endPacket();


 Serial.print("Packet #"); Serial.print(counter);
 Serial.print(" | pitch="); Serial.print(pitch, 2);
 Serial.print(" roll=");    Serial.print(roll, 2);
 Serial.print(" | AcX=");   Serial.print(AcX + AcXcal);
 Serial.print(" AcY=");     Serial.print(AcY + AcYcal);
 Serial.print(" AcZ=");     Serial.print(AcZ + AcZcal);
 Serial.print(" | GyX=");   Serial.print(GyX + GyXcal);
 Serial.print(" GyY=");     Serial.print(GyY + GyYcal);
 Serial.print(" GyZ=");     Serial.print(GyZ + GyZcal);
 Serial.print(" | tC=");    Serial.print(t, 2);
 Serial.print(" tF=");      Serial.println(tf, 2);


 counter++;
 delay(1000);  
}


void getAngle(int Ax, int Ay, int Az) {
 double x = Ax;
 double y = Ay;
 double z = Az;


 pitch = atan(x / sqrt((y * y) + (z * z)));
 roll  = atan(y / sqrt((x * x) + (z * z)));


 pitch = pitch * (180.0 / 3.14);
 roll  = roll  * (180.0 / 3.14);
}
