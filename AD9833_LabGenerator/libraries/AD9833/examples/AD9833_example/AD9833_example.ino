/*
  Пример генерации сигнала на AD9833
*/

#define PIN_CS 10
#include <AD9833.h>

AD9833 myGen (PIN_CS);

void setup() {
  myGen.begin();                                  // Инициализируем генератор
  myGen.writeFrequency(FREQ0, 1234.5);            // Можно записать две разные частоты
  myGen.writeFrequency(FREQ1, 5432.1);
  myGen.writePhaseDeg(FREQ0, 60);                 // Настроить фазу сигнала в градусах (если нужно)
  myGen.setWaveform (WAVEFORM_SQUARE);            // Выбрать форму сигнала
  myGen.outputEnable(true);                       // Включить выход
}

void loop() {
  myGen.selectFrequency(FREQ0);                   // Выбрать - откуда брать частоту
  delay(1000);
  myGen.selectFrequency(FREQ1);                   // Переключаем частоту
  delay(1000);
}
