#include <SPI.h>
#include <LoRa.h>


// Same LoRa pins and frequency as sender
#define LORA_SCK   5
#define LORA_MISO  21
#define LORA_MOSI  19
#define LORA_SS    27
#define LORA_RST   33
#define LORA_DIO0  15
#define LORA_FREQ  915E6


void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("LoRa receiver booting...");


  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);


  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init failed!");
    while (true) { delay(100); }
  }


  Serial.println("LoRa receiver ready. Waiting for packets...");
}


void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    Serial.print("Received packet: ");


    String incoming = "";
    while (LoRa.available()) {
      incoming += (char)LoRa.read();
    }
    Serial.println(incoming);


    Serial.print("  RSSI: ");
    Serial.print(LoRa.packetRssi());
    Serial.print(" dBm, SNR: ");
    Serial.println(LoRa.packetSnr());
  }
}
