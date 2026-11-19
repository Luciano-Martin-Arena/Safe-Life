#include <Wire.h>

void setup() {
  Wire.begin(21, 22);

  Serial.begin(115200);
  Serial.println("\nEscaner I2C");
}

void loop() {
  byte error, address;
  int devices = 0;

  Serial.println("Buscando dispositivos...");

  for(address = 1; address < 127; address++ ) {

    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("Dispositivo encontrado en 0x");
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);

      devices++;
    }
  }

  if (devices == 0)
    Serial.println("No se encontraron dispositivos");

  Serial.println("------------------");

  delay(3000);
}
