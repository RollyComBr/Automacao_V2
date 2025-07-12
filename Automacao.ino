#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <SoftwareSerial.h>

#define LATCH_PIN 3
#define CLOCK_PIN 4
#define DATA_PIN 2

#define NUM_RELES 4
#define SLOTS_POR_RELE 2  // 2 slots por relé

RTC_DS1307 rtc;

uint8_t estadoRele = 0; // 4 bits para os relé
bool statusRele[NUM_RELES] = {false, false, false, false};
bool modoAutomatico[NUM_RELES] = {true, true, true, true};

// HC-05 Bluetooth - RX=10, TX=9 (ajuste conforme seu hardware)
SoftwareSerial bluetoothSerial(1, 0);

class AlarmeSlot {
public:
  uint8_t diasSemana; // bits 0 a 6: domingo=bit0, sábado=bit6
  uint8_t horaOn, minutoOn;
  uint8_t horaOff, minutoOff;
  uint8_t relesAdd, modoAdd;

  AlarmeSlot() {
    diasSemana = 0;
    horaOn = minutoOn = horaOff = minutoOff = 0;
  }

  void loadFromEEPROM(int addr) {
    diasSemana = EEPROM.read(addr);
    horaOn = EEPROM.read(addr + 1);
    minutoOn = EEPROM.read(addr + 2);
    horaOff = EEPROM.read(addr + 3);
    minutoOff = EEPROM.read(addr + 4);
    relesAdd = EEPROM.read(addr + 5);
    modoAdd = EEPROM.read(addr + 6);
  }

  void saveToEEPROM(int addr) {
    EEPROM.update(addr, diasSemana);
    EEPROM.update(addr + 1, horaOn);
    EEPROM.update(addr + 2, minutoOn);
    EEPROM.update(addr + 3, horaOff);
    EEPROM.update(addr + 4, minutoOff);
    EEPROM.update(addr + 5, relesAdd);
    EEPROM.update(addr + 6, modoAdd);
  }

  bool isDiaValido(uint8_t diaSemana) {
    // diaSemana: 0=domingo ... 6=sábado
    return (diasSemana & (1 << diaSemana)) != 0;
  }

  bool estaAtivo(uint8_t diaSemana, uint8_t horaAtual, uint8_t minAtual) {
    if (!isDiaValido(diaSemana)) return false;

    int minutoAtual = horaAtual * 60 + minAtual;
    int minOn = horaOn * 60 + minutoOn;
    int minOff = horaOff * 60 + minutoOff;

    if (minOn < minOff) {
      return (minutoAtual >= minOn && minutoAtual < minOff);
    } else {
      // Intervalo passando da meia-noite
      return (minutoAtual >= minOn || minutoAtual < minOff);
    }
  }
};

void salvaModosNaEEPROM() {
  for (int i = 0; i < NUM_RELES; i++) {
    EEPROM.update(40 + i, modoAutomatico[i] ? 1 : 0);
  }
}

void carregaModosDaEEPROM() {
  for (int i = 0; i < NUM_RELES; i++) {
    uint8_t val = EEPROM.read(40 + i);
    modoAutomatico[i] = (val == 1);
  }
}

void swapInt(int &a, int &b) {
  int temp = a;
  a = b;
  b = temp;
}

class TimerAlarme {
  public:
  uint8_t releIndex;
  AlarmeSlot slots[SLOTS_POR_RELE];
  int eepromAddrBase;

  TimerAlarme(uint8_t index, int eepromAddr) {
    releIndex = index;
    eepromAddrBase = eepromAddr;
    loadFromEEPROM();
  }

  void loadFromEEPROM() {
    for (int i = 0; i < SLOTS_POR_RELE; i++) {
      slots[i].loadFromEEPROM(eepromAddrBase + i * 5);
    }
  }

  void saveToEEPROM() {
    for (int i = 0; i < SLOTS_POR_RELE; i++) {
      slots[i].saveToEEPROM(eepromAddrBase + i * 5);
    }
  }

  bool deveLigar(DateTime now) {
    uint8_t diaSemana = now.dayOfTheWeek();  // 0=domingo, 1=segunda...
    uint8_t hora = now.hour();
    uint8_t min = now.minute();
    int minutoAtual = hora * 60 + min;

    for (int i = 0; i < SLOTS_POR_RELE; i++) {
      AlarmeSlot slot = slots[i];

      int minutoOn = slot.horaOn * 60 + slot.minutoOn;
      int minutoOff = slot.horaOff * 60 + slot.minutoOff;

      if (minutoOn < minutoOff) {
        if (slot.isDiaValido(diaSemana) &&
            minutoAtual >= minutoOn &&
            minutoAtual < minutoOff) {
          return true;  // Encontrou UM slot válido -> mantém ligado
        }
      } else {
        if (slot.isDiaValido(diaSemana) &&
            minutoAtual >= minutoOn) {
          return true;
        }
        int diaAnterior = (diaSemana + 6) % 7;
        if (slot.isDiaValido(diaAnterior) &&
            minutoAtual < minutoOff) {
          return true;
        }
      }
    }

    return false;  // Nenhum slot ativo -> desliga
  }

  void updateEstado(DateTime now) {
    bool ligar = deveLigar(now);
    bitWrite(estadoRele, releIndex, ligar);
  }
};

TimerAlarme alarmes[NUM_RELES] = {
  TimerAlarme(0, 0),
  TimerAlarme(1, SLOTS_POR_RELE * 5),
  TimerAlarme(2, SLOTS_POR_RELE * 10),
  TimerAlarme(3, SLOTS_POR_RELE * 15)
};

bool parseHoraMinuto(String s, int &hora, int &minuto);
bool parseData(String s, int &dia, int &mes, int &ano);
bool parseHoraCompleta(String s, int &hora, int &minuto, int &segundo);

void setup() {
  Serial.begin(9600);
  bluetoothSerial.begin(9600);

  Wire.begin();

  pinMode(LATCH_PIN, OUTPUT);
  pinMode(CLOCK_PIN, OUTPUT);
  pinMode(DATA_PIN, OUTPUT);

  if (!rtc.begin()) {
    Serial.println("Erro: RTC nao encontrado");
    //while(1);
  }

  if (!rtc.isrunning()) {
    Serial.println("RTC nao estava rodando, ajustando para o horario de compilacao.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  Serial.println("Sistema iniciado.");
  bluetoothSerial.println("Sistema iniciado.");
  carregaModosDaEEPROM();
}

void loop() {
  // Checa comandos Serial
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    processaComando(cmd, Serial);
  }

  // Checa comandos Bluetooth
  if (bluetoothSerial.available()) {
    String cmd = bluetoothSerial.readStringUntil('\n');
    //Serial.println(cmd);
    processaComando(cmd, bluetoothSerial);
  }

  DateTime now = rtc.now();
  uint8_t novoEstado = 0;

  for (int i = 0; i < NUM_RELES; i++) {
    if (modoAutomatico[i]) {
      // estado pelo timer
      bool ligar = alarmes[i].deveLigar(now);
      bitWrite(novoEstado, i, ligar);
    } else {
      // estado manual armazenado em statusRele
      bitWrite(novoEstado, i, statusRele[i]);
    }
  }

  estadoRele = novoEstado;
  atualizaShiftRegister();

  delay(1000);
}

void atualizaShiftRegister() {
  digitalWrite(LATCH_PIN, LOW);
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, estadoRele);
  digitalWrite(LATCH_PIN, HIGH);
}
void printHora(uint8_t h, uint8_t m, Stream &porta) {
  if (h < 10) porta.print("0");
  porta.print(h);
  porta.print(":");
  if (m < 10) porta.print("0");
  porta.print(m);
}
void listData(String cmd, Stream &porta){
  porta.print("{");
    for (int i = 0; i < NUM_RELES; i++) {
      porta.print("\"r"); porta.print(i); porta.print("\":[");
      for (int s = 0; s < SLOTS_POR_RELE; s++) {
        AlarmeSlot &slot = alarmes[i].slots[s];
        porta.print("{\"d\":"); porta.print(slot.diasSemana);
        porta.print(",\"on\":\""); printHora(slot.horaOn, slot.minutoOn, porta);
        porta.print("\",\"off\":\""); printHora(slot.horaOff, slot.minutoOff, porta);
        porta.print("\"}");
        if (s < SLOTS_POR_RELE - 1) porta.print(",");
      }
      porta.print("]");
      if (i < NUM_RELES - 1) porta.print(",");
    }
    porta.println("}");
}
void cmdSet(String cmd, Stream &porta, bool imprime=true) {
  String partes[6];
  byte pos = 4;  // pula "SET "

  for (int i = 0; i < 6; i++) {
    byte nextSpace = cmd.indexOf(' ', pos);
    
    if (nextSpace == -1 && i < 5) {
      porta.println("Erro: faltam parametros");
      return;
    }

    if (nextSpace == -1) {
      partes[i] = cmd.substring(pos); // último campo
    } else {
      partes[i] = cmd.substring(pos, nextSpace);
      pos = nextSpace + 1;
    }

    // Debug:
    // Serial.print(i);
    // Serial.print(" = ");
    // Serial.println(partes[i]);
  }

  byte rele = partes[0].toInt();
  byte slot = partes[1].toInt();

  int dias;
  String diasStr = partes[2];

  bool isBinario = true;
  for (char c : diasStr) {
    if (c != '0' && c != '1') {
      isBinario = false;
      break;
    }
  }

  if (isBinario) {
    dias = strtol(diasStr.c_str(), nullptr, 2);
  } else {
    dias = diasStr.toInt();
  }

  if (rele < 0 || rele >= NUM_RELES || slot < 0 || slot >= SLOTS_POR_RELE) {
    porta.println("Erro: rele ou slot inválido");
    return;
  }

  int hOn, mOn, hOff, mOff;
  if (!parseHoraMinuto(partes[4], hOn, mOn) || !parseHoraMinuto(partes[5], hOff, mOff)) {
    porta.println("Erro: formato hora inválido");
    return;
  }

  AlarmeSlot &slotAlarme = alarmes[rele].slots[slot];
  slotAlarme.diasSemana = dias;
  slotAlarme.horaOn = hOn;
  slotAlarme.minutoOn = mOn;
  slotAlarme.horaOff = hOff;
  slotAlarme.minutoOff = mOff;

  alarmes[rele].saveToEEPROM();
  if(imprime){
    porta.println("Alarme atualizado.");
    modoAutomatico[rele] = true;
    salvaModosNaEEPROM();
  }else{
    modoAutomatico[rele] = false;
    salvaModosNaEEPROM();
  }
}

void cmdSetTime(String cmd, Stream &porta){
  // SETTIME DD/MM/AAAA HH:MM:SS
  String param = cmd.substring(8);
  param.trim();
  int spaceIndex = param.indexOf(' ');
  if (spaceIndex == -1) {
    porta.println("Erro: formato SETTIME inválido");
    return;
  }
  String dataStr = param.substring(0, spaceIndex);
  String horaStr = param.substring(spaceIndex + 1);

  int dia, mes, ano;
  int hora, minuto, segundo;

  if (!parseData(dataStr, dia, mes, ano) || !parseHoraCompleta(horaStr, hora, minuto, segundo)) {
    porta.println("Erro: formato data/hora inválido");
    return;
  }

  rtc.adjust(DateTime(ano, mes, dia, hora, minuto, segundo));
  porta.println("RTC ajustado.");
}
void cmdStatus(String cmd, Stream &porta){
  porta.print("{\"relays\":[");
  for (int i = 0; i < NUM_RELES; i++) {
    porta.print("{\"id\":");
    porta.print(i);
    porta.print(",\"Modo\":\"");
    porta.print(modoAutomatico[i] ? "A" : "M");
    porta.print("\",\"status\":\"");
    porta.print(bitRead(estadoRele, i) == 1 ? "ON" : "OFF");
    porta.print("\"}");
    if (i < NUM_RELES - 1) {
      porta.print(",");
    }
  }
  porta.println("]}");
}
void cmdHelp(String cmd, Stream &porta){
  porta.println("Comandos:");
  porta.println("LIST - lista alarmes");
  porta.println("SET <rele> <slot> <dias> <horaOn> <horaOff> - configura alarme");
  porta.println("  exemplo: SET 1 0 62 08:00 18:00");
  porta.println("SETTIME <DD/MM/AAAA> <HH:MM:SS> - ajusta RTC");
  porta.println("  exemplo: SETTIME 10/07/2025 15:45:00");
  porta.println("STATUS - mostra estado dos relés");
  porta.println("HELP - mostra comandos");
}
void cmdOn(String cmd, Stream &porta){
  int rele = cmd.substring(3).toInt();
  if (rele >= 0 && rele < NUM_RELES) {
    modoAutomatico[rele] = false;
    statusRele[rele] = true;
    //bitWrite(estadoRele, rele, true);
    atualizaShiftRegister();
    porta.print("Relé ");
    porta.print(rele);
    porta.println(" ativado manualmente.");
    salvaModosNaEEPROM();
  } else {
    porta.println("Relé inválido.");
  }
}
void cmdOff(String cmd, Stream &porta){
  int rele = cmd.substring(4).toInt();
  if (rele >= 0 && rele < NUM_RELES) {
    modoAutomatico[rele] = false;
    statusRele[rele] = false;
    //bitWrite(estadoRele, rele, false);
    atualizaShiftRegister();
    porta.print("Relé ");
    porta.print(rele);
    porta.println(" desativado manualmente.");
    salvaModosNaEEPROM();
  } else {
    porta.println("Relé inválido.");
  }
}
void cmdAuto(String cmd, Stream &porta){
  int modo = cmd.substring(5).toInt();
  if (modo != 0 && modo != 1) {
    porta.println("Uso: AUTO <modo> <rele>");
    return;
  }
  int releNum = cmd.substring(7).toInt();
  if (releNum < 0 || releNum >= NUM_RELES) {
    porta.println("Rele invalido.");
    return;
  }
  modoAutomatico[releNum] = (modo) ? true : false;
  porta.print("Rele ");
  porta.print(releNum);
  porta.print(" voltou para modo ");
  porta.println((modo) ? "AUTOMATICO." : "MANUAL.");
}
void cmdTime(String cmd, Stream &porta){
  DateTime now = rtc.now();

  porta.print("{\"data\":\"");
  if (now.day() < 10) porta.print('0');
  porta.print(now.day()); porta.print('/');
  if (now.month() < 10) porta.print('0');
  porta.print(now.month()); porta.print('/');
  porta.print(now.year());

  porta.print("\",\"hora\":\"");
  if (now.hour() < 10) porta.print('0');
  porta.print(now.hour()); porta.print(':');
  if (now.minute() < 10) porta.print('0');
  porta.print(now.minute()); porta.print(':');
  if (now.second() < 10) porta.print('0');
  porta.print(now.second());
  porta.println("\"}");
}
void processaComando(String cmd, Stream &porta) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "LIST") {
    listData(cmd, porta);
  } else if (cmd.startsWith("SET ")) {
    cmdSet(cmd, porta);
  } else if (cmd.startsWith("SETTIME ")) {
    cmdSetTime(cmd, porta);
  } else if (cmd == "STATUS") {
    cmdStatus(cmd, porta);
  } else if (cmd == "HELP") {
    cmdHelp(cmd, porta);
  } else if (cmd.startsWith("ON ")) {
    cmdOn(cmd, porta);
  } else if (cmd.startsWith("OFF ")) {
    cmdOff(cmd, porta);
  } else if (cmd.startsWith("AUTO ")) {
    cmdAuto(cmd, porta);
  } else if (cmd == "TIME") { 
    cmdTime(cmd, porta);
  } else if (cmd == "RESET") {
    String comandoReset;
    for(int i = 0; i < NUM_RELES; i++){
      for(int j = 0; j < SLOTS_POR_RELE; j++){
        comandoReset = "SET " + String(i) + " " + String(j) + " 0 12:00 12:01";
        comandoReset.trim();
        cmdSet(comandoReset, porta, false);
      }
    }
    porta.println("Alarmes resetados.");
  } else {
    porta.println("Comando nao reconhecido.");
  }
}
bool parseHoraMinuto(String s, int &hora, int &minuto) {
  int colon = s.indexOf(':');
  if (colon == -1) return false;
  hora = s.substring(0, colon).toInt();
  minuto = s.substring(colon + 1).toInt();
  if (hora < 0 || hora > 23 || minuto < 0 || minuto > 59) return false;
  return true;
}
bool parseData(String s, int &dia, int &mes, int &ano) {
  int firstSlash = s.indexOf('/');
  int lastSlash = s.lastIndexOf('/');
  if (firstSlash == -1 || lastSlash == -1 || firstSlash == lastSlash) return false;

  dia = s.substring(0, firstSlash).toInt();
  mes = s.substring(firstSlash + 1, lastSlash).toInt();
  ano = s.substring(lastSlash + 1).toInt();

  if (dia < 1 || dia > 31 || mes < 1 || mes > 12 || ano < 2000 || ano > 2099) return false;
  return true;
}
bool parseHoraCompleta(String s, int &hora, int &minuto, int &segundo) {
  int firstColon = s.indexOf(':');
  int lastColon = s.lastIndexOf(':');
  if (firstColon == -1 || lastColon == -1 || firstColon == lastColon) return false;

  hora = s.substring(0, firstColon).toInt();
  minuto = s.substring(firstColon + 1, lastColon).toInt();
  segundo = s.substring(lastColon + 1).toInt();

  if (hora < 0 || hora > 23 || minuto < 0 || minuto > 59 || segundo < 0 || segundo > 59) return false;
  return true;
}
