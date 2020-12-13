/*
  Скетч к проекту портативного генератора сигналов на AD9833
  Проект на EasyEda https://oshwlab.com/Nich1con/generator
*/

#define OLED_SOFT_BUFFER_64

#define BTN_UP 6      // кнопки и энкодер
#define BTN_DOWN 5
#define ENC_SW 9
#define ENC_DT 3
#define ENC_CK 2

#define FPS 60                // Частота обновления экрана
#define EE_DATA_ADDR 5        // Адрес настроек в EEPROM
#define BAT_CHECK_PERIOD 1000 // Период опроса аккумулятора
#define INTERNAL_REF_MV 1100  // напряжение внутрненнего опорного в мВ
#define FULL_BAT_MV   4200	  // Напряжение заряженной акб в мВ	
#define EMPTY_BAT_MV  3100    // Напряжение разряженной акб в мВ	

#include <EEPROM.h>           // Библиотеки
#include <GyverOLED.h>
#include <GyverButton.h>
#include <GyverEncoder.h>
#include <AD9833.h>

AD9833 generator(10);		  // Обьекты классов
GButton up(BTN_UP); 
GButton down(BTN_DOWN);
GyverOLED oled;
Encoder enc (ENC_CK, ENC_DT, ENC_SW, TYPE2);

struct {					 // переменные, которые планируем сохранять храним в структуре
  float freq0 = 1000.0f;
  float freq1 = 1000.0f;
  int8_t freq0_dim = 0;
  int8_t freq1_dim = 0;
  int8_t waveform = 0;
  bool eeprom_used = false;
} data;

int8_t output_mode = 0;		// Остальные просто глобальные
int8_t display_pointer = 1;
uint32_t message_timepoint = 0;
uint8_t message = 0;
uint8_t bat_charge = 5;

void setup() {
  oled.init(OLED128x64, 500);		// Инит дисплея
  EEPROM.get(EE_DATA_ADDR, data);	// Чтенеи настроек
  enc.tick();                       // опрос энкодера
  if (enc.isHold() or !data.eeprom_used) {  // Если при запуске нажат энк или настроки не были записаны
    resetSettings();					    // Очистка настроек
  }
  ad9833Setup();		// Инициализация генератора
  adcSetup();			// И АЦП для чтения акб
  batteryCheck();		// Сразу проверим АКБ
  attachInterrupt(0, tickEnc, CHANGE); // Подключение внешних прерываний для опрос энка
  attachInterrupt(1, tickEnc, CHANGE);
}

void loop() {  
  buttonsCheck();     // Опрос кнопок
  encoderCheck();     // опрос энкодера
  ad9833ModeUpdate(); // Обновленеи настроек генератора (если поменялись)

  static uint32_t batTimer = millis();				// Таймер акб
  if (millis() - batTimer >= BAT_CHECK_PERIOD) {   
    batTimer = millis();
    batteryCheck();  // проверка акб
  }

  static uint32_t oledTimer = millis();			// Таймер дисплея
  if (millis() - oledTimer >= 1000 / FPS) {
    oledTimer = millis();
    printMenu();      // Отрисовка меню
  }
}

void batteryCheck() {
  int16_t data = 0;						// буфер
  for (uint8_t i = 0; i < 4; i++) {		// 4 измерения напряжения
    ADCSRA |= 1 << ADSC;
    while (ADCSRA & (1 << ADSC));
    data += ADC;
  }
  data >>= 2;							// Усреднение
  data = (uint32_t)((INTERNAL_REF_MV * 1024UL) / data); // Перевод в мВ
  if (data < EMPTY_BAT_MV - 50) {		// Если акб полностью села
    oled.clear();						// Вывод на дисплей предупрежения
    oled.print
    (F(
       "\n\n\n"
       "     BATTERY LOW\n"
       "      SHUTDOWN"
     ));
    oled.update();	
    delay(1000);
    oled.setPower(false);				// Выкл дисплей
    SMCR = 1 << SM1 | 1 << SE;			// Сон - powerdown
    asm volatile						// бесконечный сон (разбудит только резет)
    (
      "CLI    \n\t"
      "SLEEP  \n\t"
    );
  }
  bat_charge = constrain(map(data, EMPTY_BAT_MV, FULL_BAT_MV, 1, 13), 1, 13); // Шкала АКБ
}


void adcSetup() {		 		// Инит ацп
  ADMUX = 1 << REFS0 | 1 << MUX3 | 1 << MUX2 | 1 << MUX1;    // Вход ацп к опорному
  ADCSRA = 1 << ADEN | 1 << ADPS2 | 1 << ADPS1 | 1 << ADPS0; // макс делитель и вкл ацп
  delayMicroseconds(100);		// Небольшая задержка перед стартом
}

void resetSettings(void) {		
  data.freq0 = 1000.0f;     // Дефолт настройки
  data.freq1 = 1000.0f;
  data.freq0_dim = 0;
  data.freq1_dim = 0;
  data.waveform = 0;
  data.eeprom_used = true;	
  EEPROM.put(EE_DATA_ADDR, data);	// Записать их
  message_timepoint = millis();		// Таймер для вывода сообщения
  message = 2;						// тип сообщения 
}

void ad9833Setup(void) {            
  generator.begin();// инит и загрузка стандарт параметров
  generator.writeFrequency(FREQ0, data.freq0);
  generator.writeFrequency(FREQ1, data.freq1);
  switch (data.waveform) {
    case 0: generator.setWaveform (WAVEFORM_SQUARE);  break;
    case 1: generator.setWaveform (WAVEFORM_SINE);  break;
    case 2: generator.setWaveform (WAVEFORM_TRIANGLE);  break;
  }
}


void ad9833ModeUpdate(void) {			// Если нажатия на кнопки поменяли часть параметров - загружаем их в генератор
  static int8_t _old_output = 0;
  static int8_t _old_waveform = 0;
  if (_old_output != output_mode) {
    _old_output = output_mode;
    switch (output_mode) {
      case 0:
        generator.outputEnable(false); break;
      case 1:
        generator.selectFrequency(FREQ0);
        generator.outputEnable(true);
        break;
      case 2:
        generator.selectFrequency(FREQ1);
        generator.outputEnable(true);
        break;
    }
  }

  if (_old_waveform != data.waveform) {
    _old_waveform = data.waveform;
    switch (data.waveform) {
      case 0: generator.setWaveform (WAVEFORM_SQUARE);  break;
      case 1: generator.setWaveform (WAVEFORM_SINE);  break;
      case 2: generator.setWaveform (WAVEFORM_TRIANGLE);  break;
    }
  }
}


void tickEnc(void) {  // опрос энка в прерывании
  enc.tick();
}

 
void buttonsCheck(void) {  // опрос кнопок
  up.tick();
  down.tick();

  if (up.isClick()) {
    display_pointer = constrain(display_pointer - 2, 1, 7); // указатель меню
  }
  if (down.isClick()) {
    display_pointer = constrain(display_pointer + 2, 1, 7);
  }
}


void encoderCheck(void) {
  enc.tick();

  if (enc.isHolded()) {               // сохранение настроек по длительнмоу нажатию
    EEPROM.put(EE_DATA_ADDR, data);
    message_timepoint = millis();
    message = 1;
  }

  if (enc.isDouble()) {				// переключенеи размерности по дабл клику
    switch (display_pointer) {
      case 5: if (++data.freq0_dim > 2) data.freq0_dim = 0; break;
      case 7: if (++data.freq1_dim > 2) data.freq1_dim = 0; break;
    }
  }

  if (enc.isTurn()) {

    if (enc.isRight()) {			// Дальше обработка вращений
      switch (display_pointer) {
        case 1: if (++output_mode > 2) output_mode = 0; break;
        case 3: if (++data.waveform > 2) data.waveform = 0; break;
        case 5: trimFreq0(0.1f, 100.0f, 10000.0f); break;
        case 7: trimFreq1(0.1f, 100.0f, 10000.0f); break;
      }
    }

    if (enc.isLeft()) {
      switch (display_pointer) {
        case 1: if (--output_mode < 0) output_mode = 2; break;
        case 3: if (--data.waveform < 0) data.waveform = 2; break;
        case 5: trimFreq0(-0.1f, -100.0f, -10000.0f); break;
        case 7: trimFreq1(-0.1f, -100.0f, -10000.0f); break;
      }
    }

    if (enc.isFastR()) {
      switch (display_pointer) {
        case 1: if (++output_mode > 2) output_mode = 0; break;
        case 3: if (++data.waveform > 2) data.waveform = 0; break;
        case 5: trimFreq0(10.0f, 1000.0f, 100000.0f); break;
        case 7: trimFreq1(10.0f, 1000.0f, 100000.0f); break;
      }
    }

    if (enc.isFastL()) {
      switch (display_pointer) {
        case 1: if (--output_mode < 0) output_mode = 2; break;
        case 3: if (--data.waveform < 0) data.waveform = 2; break;
        case 5: trimFreq0(-10.0f, -1000.0f, -100000.0f); break;
        case 7: trimFreq1(-10.0f, -1000.0f, -100000.0f); break;
      }
    }

    if (enc.isRightH()) {
      switch (display_pointer) {
        case 1: if (++output_mode > 2) output_mode = 0; break;
        case 3: if (++data.waveform > 2) data.waveform = 0; break;
        case 5: trimFreq0(100.0f, 10000.0f, 1000000.0f); break;
        case 7: trimFreq1(100.0f, 10000.0f, 1000000.0f); break;
      }
    }

    if (enc.isLeftH()) {
      switch (display_pointer) {
        case 1: if (--output_mode < 0) output_mode = 2; break;
        case 3: if (--data.waveform < 0) data.waveform = 2; break;
        case 5: trimFreq0(-100.0f, -10000.0f, -1000000.0f); break;
        case 7: trimFreq1(-100.0f, -10000.0f, -1000000.0f); break;
      }
    }
  }
}

void trimFreq0(float dim0, float dim1, float dim2) {  // корректировка частоты в трех разных размерностях
  switch (data.freq0_dim) {
    case 0: data.freq0 = constrain(data.freq0 + dim0, 0.0f, 12500000.0f); break;
    case 1: data.freq0 = constrain(data.freq0 + dim1, 0.0f, 12500000.0f); break;
    case 2: data.freq0 = constrain(data.freq0 + dim2, 0.0f, 12500000.0f); break;
  }
  generator.writeFrequency(FREQ0, data.freq0);
}

void trimFreq1(float dim0, float dim1, float dim2) {
  switch (data.freq1_dim) {
    case 0: data.freq1 = constrain(data.freq1 + dim0, 0.0f, 12500000.0f); break;
    case 1: data.freq1 = constrain(data.freq1 + dim1, 0.0f, 12500000.0f); break;
    case 2: data.freq1 = constrain(data.freq1 + dim2, 0.0f, 12500000.0f); break;
  }
  generator.writeFrequency(FREQ1, data.freq1);
}

void printMenu(void) {			// Построчное меню
  oled.clear();

  oled.setCursor(0, display_pointer);  // указатель меню
  oled.print(F(">"));

  oled.rect(114, 0, 126, 6);			// индикатор акб
  oled.line(112, 1, 112, 6);
  for (int8_t i = 0; i < bat_charge; i++) {
    oled.line(126 - i, 0, 126 - i, 7);
  }

  oled.home();
  if (millis() - message_timepoint < 3000 and message) {  // вывод сообщений левый верхн угол
    switch (message) {
      case 1: oled.print(F("(SAVED)")); break;
      case 2: oled.print(F("(ERASED)")); break;
    }
  } else {
    message = 0;
  }

  oled.print			// Вывод пунктов
  (F(
     "\n OUT  : \n"
     "\n FORM : \n"
     "\n FREQ0: \n"
     "\n FREQ1: \n"
   ));
  oled.setCursor(7, 1);		// Контент пунктов
  switch (output_mode) {
    case 0: oled.print(F("DISABLE")); break;
    case 1: oled.print(F("FROM FREQ0")); break;
    case 2: oled.print(F("FROM FREQ1")); break;
  }
  oled.setCursor(7, 3);
  switch (data.waveform) {
    case 0: oled.print(F("MEANDER")); break;
    case 1: oled.print(F("SINUSOIDAL")); break;
    case 2: oled.print(F("TRIANGULAR")); break;
  }
  oled.setCursor(7, 5);
  switch (data.freq0_dim) {
    case 0: oled.print(data.freq0, 1); oled.print(F(" Hz")); break;
    case 1: oled.print(data.freq0 / 1000.0f, 1); oled.print(F(" kHz")); break;
    case 2: oled.print(data.freq0 / 1000000.0f, 2); oled.print(F(" MHz")); break;
  }
  oled.setCursor(7, 7);
  switch (data.freq1_dim) {
    case 0: oled.print(data.freq1, 1); oled.print(F(" Hz")); break;
    case 1: oled.print(data.freq1 / 1000.0f, 1); oled.print(F(" kHz")); break;
    case 2: oled.print(data.freq1 / 1000000.0f, 2); oled.print(F(" MHz")); break;
  }
  oled.line(0, 19, 127, 19);	// Линии разделители
  oled.line(0, 35, 127, 35);
  oled.line(0, 51, 127, 51);
  oled.update();				// Апдейт дисплея
}
