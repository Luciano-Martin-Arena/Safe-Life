#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

MAX30105 pulsioximetro;

#define MAX_BRIGHTNESS 255

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)

// Arduino UNO
uint16_t pulsoBuffer[100];
uint16_t oxiBuffer[100];

#else

// ESP32
uint32_t pulsoBuffer[100];
uint32_t oxiBuffer[100];

#endif

int32_t BufferLongitud;
int32_t spo2;
int8_t SPO2valido;
int32_t rangopulsacion;
int8_t validrangopulsacion;

byte pulsoLED = 11;
byte lecturaLED = 13;

void setup()
{
  Serial.begin(115200);

  pinMode(pulsoLED, OUTPUT);
  pinMode(lecturaLED, OUTPUT);

  // ===== INICIAR I2C EN ESP32 =====
  // SDA = GPIO 21
  // SCL = GPIO 22
  Wire.begin(21, 22);

  // ===== INICIAR SENSOR =====
  if (!pulsioximetro.begin(Wire, I2C_SPEED_FAST))
  {
    Serial.println("MAX30102 no encontrado. Revisa conexiones.");
    while (1);
  }

  Serial.println("Coloca el dedo sobre el sensor...");

  byte brilloLED = 60;
  byte mediaMuestreo = 4;
  byte ModoLED = 2;
  byte rangoMuestreo = 100;
  int anchoPulso = 411;
  int rangoADC = 4096;

  pulsioximetro.setup(
    brilloLED,
    mediaMuestreo,
    ModoLED,
    rangoMuestreo,
    anchoPulso,
    rangoADC
  );
}

void loop()
{
  BufferLongitud = 100;

  // ===== LEER PRIMERAS 100 MUESTRAS =====
  for (byte i = 0; i < BufferLongitud; i++)
  {
    while (pulsioximetro.available() == false)
      pulsioximetro.check();

    oxiBuffer[i] = pulsioximetro.getRed();
    pulsoBuffer[i] = pulsioximetro.getIR();

    pulsioximetro.nextSample();

    Serial.print("RED=");
    Serial.print(oxiBuffer[i]);

    Serial.print(" IR=");
    Serial.println(pulsoBuffer[i]);
  }

  // ===== CALCULAR SPO2 Y BPM =====
  maxim_heart_rate_and_oxygen_saturation(
    pulsoBuffer,
    BufferLongitud,
    oxiBuffer,
    &spo2,
    &SPO2valido,
    &rangopulsacion,
    &validrangopulsacion
  );

  while (1)
  {
    // Desplazar buffers
    for (byte i = 25; i < 100; i++)
    {
      oxiBuffer[i - 25] = oxiBuffer[i];
      pulsoBuffer[i - 25] = pulsoBuffer[i];
    }

    // Leer nuevas muestras
    for (byte i = 75; i < 100; i++)
    {
      while (pulsioximetro.available() == false)
        pulsioximetro.check();

      digitalWrite(lecturaLED, !digitalRead(lecturaLED));

      oxiBuffer[i] = pulsioximetro.getRed();
      pulsoBuffer[i] = pulsioximetro.getIR();

      pulsioximetro.nextSample();

      Serial.print("Oxigeno=");
      Serial.print(spo2);

      Serial.print("%  BPM=");
      Serial.print(rangopulsacion);

      Serial.print("  RED=");
      Serial.print(oxiBuffer[i]);

      Serial.print("  IR=");
      Serial.println(pulsoBuffer[i]);
    }

    // Recalcular
    maxim_heart_rate_and_oxygen_saturation(
      pulsoBuffer,
      BufferLongitud,
      oxiBuffer,
      &spo2,
      &SPO2valido,
      &rangopulsacion,
      &validrangopulsacion
    );
  }
}
