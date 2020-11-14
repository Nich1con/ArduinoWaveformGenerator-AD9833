/*
 Библиотека для работы с генератором сигналов AD9833 по интерфуйсу SPI
 Разработано Egor 'Nich1con' Zakharov
 V1.0 от 11.11.2020
*/

#pragma once
#include <SPI.h>
#include <Arduino.h>

/* Битовые маски конфигурационных регистров */
#define B28_CFG       (1 << 13)
#define HLB_CFG       (1 << 12)
#define F_SELECT_CFG  (1 << 11)
#define P_SELECT_CFG  (1 << 10)
#define RESET_CFG     (1 << 8)
#define SLEEP1_CFG    (1 << 7)
#define SLEEP12_CFG   (1 << 6)
#define OPBITEN_CFG   (1 << 5)
#define DIV2_CFG      (1 << 3)
#define MODE_CFG      (1 << 1)

/* Битовые маски доступа к регистрам */
#define FREQ0_ACCESS  0x4000
#define FREQ1_ACCESS  0x8000
#define PHASE0_ACCESS 0xC000
#define PHASE1_ACCESS 0xE000

/* Конфигурация выходного сигнала */
#define WAVEFORM_SINE         0
#define WAVEFORM_TRIANGLE     MODE_CFG
#define WAVEFORM_SQUARE       OPBITEN_CFG | DIV2_CFG
#define WAVEFORM_SQUARE_DIV2  OPBITEN_CFG

/* Конфигурация режимов сна */
#define NO_POWERDOWN      0
#define DAC_POWERDOWN     SLEEP12_CFG
#define CLOCK_POWERDOWN   SLEEP1_CFG
#define FULL_POWERDOWN    SLEEP12_CFG | SLEEP1_CFG

/* Выбор частоты и фазы */
#define FREQ0   0
#define FREQ1   1
#define PHASE0  0
#define PHASE1  1

SPISettings AD9833_SPI (8000000UL, MSBFIRST, SPI_MODE2);

class AD9833 {
  public:
    AD9833(uint8_t cs);								// Конструктор обьекта
    void begin(void);								// Инцииализация
    void writeFrequency(bool reg, float freq);	    // Записать частоту в один из регистров - FREQ0/FREQ1
    void selectFrequency(bool reg);	    			// Выбрать, из какого регистра генерировать частоту - FREQ0/FREQ1 
    void writePhaseRad(bool reg, float rad);	    // Записать фазу в радианах в один из регистров - FREQ0/FREQ1 
    void writePhaseDeg(bool reg, float rad);	    // Записать фазу в градусах в один из регистров - FREQ0/FREQ1 
    void selectPhase(bool reg);	    				// Выбрать, из какого регистра генерировать фазу - FREQ0/FREQ1  
    void setWaveform (uint8_t mode);	   			// Выбрать форму сигнала - WAVEFORM_SINE/WAVEFORM_TRIANGLE/WAVEFORM_SQUARE/WAVEFORM_SQUARE_DIV2
    void outputEnable(bool state);	                // Включить / выключить выход
    void sleep(uint8_t mode);	                    // Установить режим сна - NO_POWERDOWN/DAC_POWERDOWN/CLOCK_POWERDOWN/FULL_POWERDOWN
    void generate(bool reg, float freq, uint8_t form); // Сразу настроить генератор на нужные параметры сигнала
   private:
    void writeFreqReg(bool reg, uint32_t data);
    void writePhaseReg(bool reg, uint16_t data);
    void writeCfgReg();
    void writeReg(uint16_t data);
    uint8_t _cs_pin = 0;
    uint8_t _waveform = WAVEFORM_SINE;
    uint8_t _sleep_mode = NO_POWERDOWN;
    uint8_t _freq_source = FREQ0;
    uint8_t _phase_source = PHASE0;
    uint8_t _reset_state = 0;
};

float degToRad(float deg);     

void AD9833::begin(void) {
  SPI.begin();				// Инцииализация SPI	 
  _reset_state = true;		// Установить состояние сброса
  writeCfgReg();			// Загрузить настройки 
}

void AD9833::writeReg(uint16_t data) {
  SPI.beginTransaction(AD9833_SPI);    // Передаем настройки SPI
  digitalWrite(_cs_pin, LOW);		   // Тянем к земле chip select
  SPI.transfer(highByte(data));        // Передаем старший байт
  SPI.transfer(lowByte(data));         // Передаем младший байт
  digitalWrite(_cs_pin, HIGH);		   // Отпускаем chip select
  SPI.endTransaction();                // Освобождаем шину SPI
}

void AD9833::writeFrequency(bool reg, float freq) {
  uint32_t freqData = (float)(freq * 10.7374182f);   // Частота в целых числах
  writeFreqReg(reg, freqData);
}

void AD9833::generate(bool reg, float freq, uint8_t form) { 
  outputEnable(false);          // Выкл выход
  _phase_source = reg;		    // Сохранить параметры
  _waveform = form;
  writeCfgReg();				// Отправить параметры
  writeFrequency(reg, freq);	// Отправить частоту
  outputEnable(true);			// Вкл выход
}

void AD9833::writeCfgReg(void) {
  uint16_t cfg = 0;							  // Образ регистра конфигурации				
  cfg |= _waveform;						      // Учесть форму сигнала
  cfg |= _sleep_mode; 					      // Учесть режим сна		
  cfg |= (_freq_source ? F_SELECT_CFG : 0);	  // Учесть регистр частоты
  cfg |= (_phase_source ? P_SELECT_CFG : 0);  // Учесть регистр фазы
  cfg |= (_reset_state ? RESET_CFG : 0);	  // Состояние выхода
  cfg |= B28_CFG;							  // Передавать частоту будем целиком (28 бит)
  writeReg(cfg);							  // Отправить образ регистра
}

void AD9833::writeFreqReg(bool reg, uint32_t data) {
  uint16_t target = (reg ? FREQ1_ACCESS : FREQ0_ACCESS);  // Выбор регистра частоты
  uint16_t data_lsb = target  | (uint16_t)data & 0x3FFF;  // Составить два слова данных 
  uint16_t data_msb = target  | (uint16_t)(data  >> 14);
  writeCfgReg();										  // Обновить регистр конфигурации
  writeReg(data_lsb);									  // Отправить оба слова со значением частоты
  writeReg(data_msb);
}

void AD9833::writePhaseDeg(bool reg, float deg) {      
  uint16_t phaseData = (float)deg * (4096.0f / 360.0f);  // Фаза в целых числах
  writePhaseReg(reg, phaseData);
}

void AD9833::writePhaseRad(bool reg, float rad) {
  uint16_t phaseData = (float)rad * (4096.0f / (2.0f * PI)); // фаза в целых числах
  writePhaseReg(reg, phaseData);
}

void AD9833::writePhaseReg(bool reg, uint16_t data) {
  uint16_t target = (reg ? PHASE1_ACCESS : PHASE0_ACCESS);  // Выбор регистра фазы
  writeReg(target | (uint16_t) (data & 0xFFF));
}

AD9833::AD9833(uint8_t cs) {   											
  pinMode(cs, OUTPUT);		   
  _cs_pin = cs;
}

void AD9833::selectPhase(bool reg) {
  _phase_source = reg;			
  writeCfgReg();
}

void AD9833::selectFrequency(bool reg) {
  _freq_source = reg;
  writeCfgReg();
}

void AD9833::sleep(uint8_t mode) {
  _sleep_mode = mode;
  writeCfgReg();
}

void AD9833::setWaveform (uint8_t mode) {
  _waveform = mode;
  writeCfgReg();
}

void AD9833::outputEnable(bool state) {	
  _reset_state = !state;
  writeCfgReg();
}


float degToRad(float deg) {
  return (float)deg * DEG_TO_RAD;
}
