#include <SPI.h>
#include <LoRa.h>


#define LORA_SCK   5
#define LORA_MISO  21
#define LORA_MOSI  19


#define LORA_SS    27
#define LORA_RST   33
#define LORA_DIO0  15


#define LORA_FREQ  915E6


int counter = 0;


void setup() {
  Serial.begin(115200);


  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);


  if (!LoRa.begin(LORA_FREQ)) {
    Serial.println("LoRa init failed");
    while (1);
  }


  Serial.println("LoRa sender ready!");
}


void loop() {
  LoRa.beginPacket();
  LoRa.print("Hello Jordan this is Larry ");
  LoRa.print(counter++);
  LoRa.endPacket();


  delay(1000);
}
