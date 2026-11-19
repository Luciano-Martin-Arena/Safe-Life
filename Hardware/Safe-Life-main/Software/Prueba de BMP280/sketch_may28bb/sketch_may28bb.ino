#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

Adafruit_BME280 bme;

void setup() {
  Serial.begin(115200);

  Wire.begin(21, 22);

  Serial.println("Iniciando BME280...");

  if (!bme.begin(0x76)) {
    Serial.println("BME280 NO encontrado");
    while (1);
  }

  Serial.println("BME280 detectado correctamente");
}

void loop() {

  Serial.print("Temperatura: ");
  Serial.print(bme.readTemperature());
  Serial.println(" °C");

  Serial.print("Presion: ");
  Serial.print(bme.readPressure() / 100.0F);
  Serial.println(" hPa");

  Serial.print("Humedad: ");
  Serial.print(bme.readHumidity());
  Serial.println(" %");

  Serial.println("----------------");

  delay(2000);
}
