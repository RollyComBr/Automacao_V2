#include <Wire.h>          // Inclui a biblioteca Wire para comunicação I2C (usada pelo RTC)
#include <RTClib.h>        // Inclui a biblioteca RTClib para interface com o módulo RTC (Real Time Clock)
#include <EEPROM.h>        // Inclui a biblioteca EEPROM para armazenar dados de forma persistente na memória Flash
#include <SoftwareSerial.h> // Inclui a biblioteca SoftwareSerial para comunicação serial em pinos digitais (usado pelo Bluetooth)

#define LATCH_PIN 3       // Define o pino digital para o LATCH (ST_CP) do Shift Register (74HC595)
#define CLOCK_PIN 4       // Define o pino digital para o CLOCK (SH_CP) do Shift Register
#define DATA_PIN 2        // Define o pino digital para o DATA (DS) do Shift Register

#define NUM_RELES 4       // Define o número total de relés controlados (neste caso, 4)
#define SLOTS_POR_RELE 2  // Define o número de "slots" de alarme (eventos ON/OFF) que cada relé pode ter

RTC_DS1307 rtc; // Cria uma instância do objeto RTC_DS1307 para interagir com o módulo RTC DS1307

uint8_t estadoRele = 0; // Variável de 8 bits para armazenar o estado de todos os relés (cada bit representa um relé)
bool statusRele[NUM_RELES] = {false, false, false, false}; // Array para armazenar o estado MANUAL de cada relé (true=ON, false=OFF)
bool modoAutomatico[NUM_RELES] = {true, true, true, true}; // Array para definir se cada relé está em modo automático (true) ou manual (false)

// HC-05 Bluetooth - RX=10, TX=9 (ajuste conforme seu hardware)
// IMPORTANTE: Em alguns Arduinos, os pinos 0 e 1 são usados para Serial Nativa.
// Se este código for para ESP8266/ESP32, SoftwareSerial em pinos arbitrários pode ser problemático ou não necessário,
// pois eles geralmente têm múltiplas UARTs ou uma Serial Nativa flexível.
// Para um UNO, RX no pino 10 e TX no pino 9 seria o padrão para SoftwareSerial.
// bluetoothSerial(1, 0) está usando os pinos TX e RX nativos do Arduino (0=RX, 1=TX), o que pode entrar em conflito com a Serial Monitor.
// Sugestão comum para HC-05 em UNO: SoftwareSerial bluetoothSerial(10, 11); onde 10 é RX e 11 é TX.
SoftwareSerial bluetoothSerial(1, 0);

// Definição da classe AlarmeSlot, que representa um único agendamento de ON/OFF para um relé
class AlarmeSlot {
public:
  uint8_t diasSemana; // Campo para armazenar os dias da semana (cada bit representa um dia: 0=domingo, ..., 6=sábado)
  uint8_t horaOn, minutoOn; // Horário de ligar (hora e minuto)
  uint8_t horaOff, minutoOff; // Horário de desligar (hora e minuto)
  uint8_t relesAdd, modoAdd; // Variáveis não utilizadas nos métodos fornecidos, possivelmente para futuras expansões ou debug

  // Construtor padrão da classe AlarmeSlot
  AlarmeSlot() {
    diasSemana = 0; // Inicializa os dias da semana como nenhum dia selecionado
    horaOn = minutoOn = horaOff = minutoOff = 0; // Inicializa todos os horários como 00:00
  }

  // Método para carregar os dados de um slot de alarme da EEPROM
  void loadFromEEPROM(int addr) {
    diasSemana = EEPROM.read(addr);       // Lê o byte dos dias da semana no endereço 'addr'
    horaOn = EEPROM.read(addr + 1);       // Lê a hora de ligar no endereço 'addr + 1'
    minutoOn = EEPROM.read(addr + 2);     // Lê o minuto de ligar no endereço 'addr + 2'
    horaOff = EEPROM.read(addr + 3);      // Lê a hora de desligar no endereço 'addr + 3'
    minutoOff = EEPROM.read(addr + 4);    // Lê o minuto de desligar no endereço 'addr + 4'
    relesAdd = EEPROM.read(addr + 5);     // Lê relesAdd no endereço 'addr + 5'
    modoAdd = EEPROM.read(addr + 6);      // Lê modoAdd no endereço 'addr + 6'
  }

  // Método para salvar os dados de um slot de alarme na EEPROM
  void saveToEEPROM(int addr) {
    EEPROM.update(addr, diasSemana);       // Atualiza (escreve apenas se diferente) os dias da semana
    EEPROM.update(addr + 1, horaOn);       // Atualiza a hora de ligar
    EEPROM.update(addr + 2, minutoOn);     // Atualiza o minuto de ligar
    EEPROM.update(addr + 3, horaOff);      // Atualiza a hora de desligar
    EEPROM.update(addr + 4, minutoOff);    // Atualiza o minuto de desligar
    EEPROM.update(addr + 5, relesAdd);     // Atualiza relesAdd
    EEPROM.update(addr + 6, modoAdd);      // Atualiza modoAdd
  }

  // Método para verificar se o dia da semana atual está incluído nos dias configurados para o alarme
  bool isDiaValido(uint8_t diaSemana) {
    // diaSemana: 0=domingo ... 6=sábado (formato da biblioteca RTClib)
    // Retorna true se o bit correspondente ao dia da semana estiver setado em 'diasSemana'
    return (diasSemana & (1 << diaSemana)) != 0;
  }

  // Método para verificar se o alarme está ativo no momento dado (hora e minuto)
  bool estaAtivo(uint8_t diaSemana, uint8_t horaAtual, uint8_t minAtual) {
    if (!isDiaValido(diaSemana)) return false; // Se o dia não for válido, o alarme não está ativo

    int minutoAtual = horaAtual * 60 + minAtual; // Converte a hora e minuto atuais para minutos totais do dia
    int minOn = horaOn * 60 + minutoOn;         // Converte a hora de ligar para minutos totais
    int minOff = horaOff * 60 + minutoOff;     // Converte a hora de desligar para minutos totais

    if (minOn < minOff) {
      // Caso normal: o horário de ligar é anterior ao horário de desligar (não passa da meia-noite)
      return (minutoAtual >= minOn && minutoAtual < minOff);
    } else {
      // Caso de intervalo passando da meia-noite (ex: liga às 22:00, desliga às 06:00)
      // O alarme está ativo se for depois da hora de ligar OU antes da hora de desligar (no dia seguinte)
      return (minutoAtual >= minOn || minutoAtual < minOff);
    }
  }
};

// Função para salvar o estado dos modos automáticos/manuais de cada relé na EEPROM
void salvaModosNaEEPROM() {
  for (int i = 0; i < NUM_RELES; i++) {
    // Salva 1 se modoAutomatico[i] for true, 0 se for false, no endereço 40 + i
    EEPROM.update(40 + i, modoAutomatico[i] ? 1 : 0);
  }
}

// Função para carregar o estado dos modos automáticos/manuais de cada relé da EEPROM
void carregaModosDaEEPROM() {
  for (int i = 0; i < NUM_RELES; i++) {
    uint8_t val = EEPROM.read(40 + i); // Lê o valor da EEPROM
    modoAutomatico[i] = (val == 1);     // Converte 1 para true, 0 para false
  }
}

// Função auxiliar para trocar o valor de duas variáveis inteiras (não utilizada no código fornecido)
void swapInt(int &a, int &b) {
  int temp = a; // Armazena o valor de 'a' em uma variável temporária
  a = b;        // Atribui o valor de 'b' a 'a'
  b = temp;     // Atribui o valor temporário (original de 'a') a 'b'
}

// Definição da classe TimerAlarme, que gerencia os slots de alarme para um relé específico
class TimerAlarme {
  public:
  uint8_t releIndex; // Índice do relé ao qual este timer está associado
  AlarmeSlot slots[SLOTS_POR_RELE]; // Array de slots de alarme para este relé
  int eepromAddrBase; // Endereço base na EEPROM para salvar/carregar os slots deste relé

  // Construtor da classe TimerAlarme
  TimerAlarme(uint8_t index, int eepromAddr) {
    releIndex = index;       // Atribui o índice do relé
    eepromAddrBase = eepromAddr; // Atribui o endereço base da EEPROM
    loadFromEEPROM();        // Carrega os slots de alarme da EEPROM ao inicializar
  }

  // Método para carregar todos os slots de alarme deste relé da EEPROM
  void loadFromEEPROM() {
    for (int i = 0; i < SLOTS_POR_RELE; i++) {
      // Carrega cada slot, calculando seu endereço na EEPROM (base + índice do slot * tamanho do slot)
      slots[i].loadFromEEPROM(eepromAddrBase + i * 5); // Cada slot ocupa 5 bytes (dias, hOn, mOn, hOff, mOff)
                                                     // Nota: a classe AlarmeSlot na verdade usa 7 bytes, mas aqui está 5. Isso pode ser um bug ou otimização.
    }
  }

  // Método para salvar todos os slots de alarme deste relé na EEPROM
  void saveToEEPROM() {
    for (int i = 0; i < SLOTS_POR_RELE; i++) {
      // Salva cada slot, calculando seu endereço na EEPROM
      slots[i].saveToEEPROM(eepromAddrBase + i * 5); // Mesmo detalhe sobre o tamanho do slot (5 vs 7 bytes)
    }
  }

  // Método para determinar se o relé deve estar ligado no momento atual
  bool deveLigar(DateTime now) {
    uint8_t diaSemana = now.dayOfTheWeek();  // Obtém o dia da semana atual (0=domingo, 1=segunda...)
    uint8_t hora = now.hour();              // Obtém a hora atual
    uint8_t min = now.minute();             // Obtém o minuto atual
    int minutoAtual = hora * 60 + min;      // Converte a hora e minuto atuais para minutos totais do dia

    for (int i = 0; i < SLOTS_POR_RELE; i++) {
      AlarmeSlot slot = slots[i]; // Pega uma cópia do slot atual (para não modificar o original)

      int minutoOn = slot.horaOn * 60 + slot.minutoOn;     // Converte hora de ligar do slot para minutos totais
      int minutoOff = slot.horaOff * 60 + slot.minutoOff; // Converte hora de desligar do slot para minutos totais

      if (minutoOn < minutoOff) {
        // Caso normal: alarme não passa da meia-noite
        if (slot.isDiaValido(diaSemana) &&              // Verifica se o dia da semana é válido para este slot
            minutoAtual >= minutoOn &&                  // E se o tempo atual está após a hora de ligar
            minutoAtual < minutoOff) {                  // E se o tempo atual está antes da hora de desligar
          return true;  // Encontrou pelo menos UM slot válido que deveria ligar -> o relé deve ligar
        }
      } else {
        // Caso de intervalo passando da meia-noite (ex: 22:00 ON, 06:00 OFF)
        // O alarme pode estar ativo no dia atual (se for depois da hora ON)
        if (slot.isDiaValido(diaSemana) &&
            minutoAtual >= minutoOn) {
          return true;
        }
        // Ou o alarme pode estar ativo porque ele ligou no dia anterior e ainda não desligou (se for antes da hora OFF)
        int diaAnterior = (diaSemana + 6) % 7; // Calcula o dia da semana anterior (ex: segunda(1) -> domingo(0))
        if (slot.isDiaValido(diaAnterior) &&
            minutoAtual < minutoOff) {
          return true;
        }
      }
    }

    return false;  // Se nenhum slot ativo foi encontrado após verificar todos, o relé deve estar desligado
  }

  // Método para atualizar o estado do relé no 'estadoRele' global com base na lógica do timer
  void updateEstado(DateTime now) {
    bool ligar = deveLigar(now); // Determina se o relé deve estar ligado ou desligado
    bitWrite(estadoRele, releIndex, ligar); // Define o bit correspondente a este relé na variável global 'estadoRele'
  }
};

// Declaração do array de objetos TimerAlarme, um para cada relé
// O endereço base na EEPROM para cada relé é calculado com base no tamanho de cada slot (5 bytes)
// Rele 0: endereço 0
// Rele 1: endereço SLOTS_POR_RELE * 5 (2 * 5 = 10)
// Rele 2: endereço SLOTS_POR_RELE * 10 (2 * 10 = 20)
// Rele 3: endereço SLOTS_POR_RELE * 15 (2 * 15 = 30)
TimerAlarme alarmes[NUM_RELES] = {
  TimerAlarme(0, 0),
  TimerAlarme(1, SLOTS_POR_RELE * 5),
  TimerAlarme(2, SLOTS_POR_RELE * 10),
  TimerAlarme(3, SLOTS_POR_RELE * 15)
};

// Declaração de funções auxiliares de parsing e processamento de comandos (implementadas mais adiante)
bool parseHoraMinuto(String s, int &hora, int &minuto); // Analisa uma string no formato HH:MM
bool parseData(String s, int &dia, int &mes, int &ano); // Analisa uma string no formato DD/MM/AAAA
bool parseHoraCompleta(String s, int &hora, int &minuto, int &segundo); // Analisa uma string no formato HH:MM:SS
void processaComando(String cmd, Stream &porta); // Processa um comando recebido (via Serial ou Bluetooth)

void setup() {
  Serial.begin(9600);      // Inicializa a comunicação serial com o monitor (baud rate de 9600 bps)
  bluetoothSerial.begin(9600); // Inicializa a comunicação serial com o módulo Bluetooth (baud rate de 9600 bps)

  Wire.begin();            // Inicializa a biblioteca Wire para comunicação I2C (para o RTC)

  pinMode(LATCH_PIN, OUTPUT); // Define o pino LATCH como saída
  pinMode(CLOCK_PIN, OUTPUT); // Define o pino CLOCK como saída
  pinMode(DATA_PIN, OUTPUT);  // Define o pino DATA como saída

  if (!rtc.begin()) {      // Tenta inicializar o módulo RTC
    Serial.println("Erro: RTC nao encontrado"); // Se falhar, imprime mensagem de erro
    //while(1); // Se descomentado, o programa travaria aqui se o RTC não fosse encontrado
  }

  if (!rtc.isrunning()) {  // Verifica se o RTC está mantendo a hora (se a bateria estiver funcionando)
    Serial.println("RTC nao estava rodando, ajustando para o horario de compilacao."); // Se não, imprime mensagem
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // E ajusta o RTC para a data e hora de compilação do sketch
  }

  Serial.println("Sistema iniciado.");   // Imprime mensagem de inicialização no Serial Monitor
  bluetoothSerial.println("Sistema iniciado."); // Imprime mensagem de inicialização no Bluetooth
  carregaModosDaEEPROM(); // Carrega os modos (automático/manual) dos relés da EEPROM
}

void loop() {
  // Checa comandos Serial
  if (Serial.available()) {                 // Verifica se há dados disponíveis para leitura na Serial
    String cmd = Serial.readStringUntil('\n'); // Lê a string até o caractere de nova linha
    processaComando(cmd, Serial);          // Processa o comando lido, usando a Serial como porta de resposta
  }

  // Checa comandos Bluetooth
  if (bluetoothSerial.available()) {           // Verifica se há dados disponíveis para leitura no Bluetooth
    String cmd = bluetoothSerial.readStringUntil('\n'); // Lê a string até o caractere de nova linha
    //Serial.println(cmd);                      // Linha comentada de debug: imprime o comando Bluetooth recebido na Serial
    processaComando(cmd, bluetoothSerial);    // Processa o comando lido, usando o Bluetooth como porta de resposta
  }

  DateTime now = rtc.now(); // Obtém a data e hora atual do RTC
  uint8_t novoEstado = 0;   // Variável temporária para calcular o novo estado de todos os relés

  for (int i = 0; i < NUM_RELES; i++) { // Itera sobre cada relé
    if (modoAutomatico[i]) {            // Se o relé estiver em modo automático
      bool ligar = alarmes[i].deveLigar(now); // Calcula se o relé deve ligar/desligar com base nos alarmes do timer
      bitWrite(novoEstado, i, ligar);     // Define o bit correspondente no 'novoEstado' global
    } else {                              // Se o relé estiver em modo manual
      bitWrite(novoEstado, i, statusRele[i]); // Usa o estado manual armazenado em 'statusRele'
    }
  }

  estadoRele = novoEstado;       // Atualiza a variável global 'estadoRele' com o novo estado calculado
  atualizaShiftRegister();       // Envia o novo estado para o Shift Register, controlando os relés fisicamente

  delay(1000); // Aguarda 1 segundo antes de repetir o loop (para evitar verificações muito rápidas do RTC e comandos)
}

// Função para enviar o estado dos relés para o Shift Register 74HC595
void atualizaShiftRegister() {
  digitalWrite(LATCH_PIN, LOW);                         // Coloca o pino LATCH em LOW para preparar o Shift Register para receber dados
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, estadoRele); // Envia o byte 'estadoRele' bit a bit para o Shift Register
                                                        // MSBFIRST significa Most Significant Bit First (bit mais significativo primeiro)
  digitalWrite(LATCH_PIN, HIGH);                        // Coloca o pino LATCH em HIGH para transferir os dados para as saídas do Shift Register
}

// Função auxiliar para imprimir hora e minuto formatados (HH:MM) em uma porta serial específica
void printHora(uint8_t h, uint8_t m, Stream &porta) {
  if (h < 10) porta.print("0"); // Se a hora for menor que 10, adiciona um zero à esquerda
  porta.print(h);               // Imprime a hora
  porta.print(":");             // Imprime o separador de minutos
  if (m < 10) porta.print("0"); // Se o minuto for menor que 10, adiciona um zero à esquerda
  porta.print(m);               // Imprime o minuto
}

// Função para listar os dados dos alarmes em formato JSON
void listData(String cmd, Stream &porta){ // Recebe o comando (não usado aqui) e a porta de saída
  porta.print("{"); // Início do objeto JSON
    for (int i = 0; i < NUM_RELES; i++) { // Itera sobre cada relé
      porta.print("\"r"); porta.print(i); porta.print("\":["); // Nome do relé como chave do array (ex: "r0": [ ... ])
      for (int s = 0; s < SLOTS_POR_RELE; s++) { // Itera sobre cada slot de alarme para o relé atual
        AlarmeSlot &slot = alarmes[i].slots[s]; // Pega a referência para o slot de alarme atual
        porta.print("{\"d\":"); porta.print(slot.diasSemana); // Início do objeto slot JSON (ex: {"d":62, ...)
        porta.print(",\"on\":\""); printHora(slot.horaOn, slot.minutoOn, porta); // Campo "on" com hora formatada
        porta.print("\",\"off\":\""); printHora(slot.horaOff, slot.minutoOff, porta); // Campo "off" com hora formatada
        porta.print("\"}"); // Fecha o objeto slot JSON
        if (s < SLOTS_POR_RELE - 1) porta.print(","); // Adiciona vírgula se não for o último slot
      }
      porta.print("]"); // Fecha o array de slots para o relé atual
      if (i < NUM_RELES - 1) porta.print(","); // Adiciona vírgula se não for o último relé
    }
    porta.println("}"); // Fecha o objeto JSON e adiciona uma nova linha
}

// Função para configurar um alarme (comando SET)
void cmdSet(String cmd, Stream &porta, bool imprime) { // Recebe o comando, a porta de saída e um booleano para imprimir ou não
  String partes[6]; // Array para armazenar as partes do comando (rele, slot, dias, etc.)
  byte pos = 4;     // Começa a analisar a string a partir do 4º caractere (depois de "SET ")

  for (int i = 0; i < 6; i++) { // Itera 6 vezes para extrair as 6 partes do comando
    byte nextSpace = cmd.indexOf(' ', pos); // Encontra o próximo espaço

    if (nextSpace == -1 && i < 5) { // Se não houver mais espaços e ainda faltarem partes
      porta.println("Erro: faltam parametros"); // Erro de sintaxe
      return; // Sai da função
    }

    if (nextSpace == -1) {                  // Se for o último campo (não há mais espaços depois)
      partes[i] = cmd.substring(pos);       // Pega o restante da string
    } else {                                // Se não for o último campo
      partes[i] = cmd.substring(pos, nextSpace); // Pega a substring entre 'pos' e 'nextSpace'
      pos = nextSpace + 1;                  // Atualiza 'pos' para o caractere após o espaço
    }

    // Debug: Linhas comentadas para depuração
    // Serial.print(i);
    // Serial.print(" = ");
    // Serial.println(partes[i]);
  }

  byte rele = partes[0].toInt(); // Converte a primeira parte para o número do relé
  byte slot = partes[1].toInt(); // Converte a segunda parte para o número do slot

  int dias;       // Variável para armazenar os dias da semana
  String diasStr = partes[2]; // Pega a string dos dias da semana

  bool isBinario = true; // Flag para verificar se a string dos dias é binária
  for (char c : diasStr) { // Itera sobre cada caractere da string dos dias
    if (c != '0' && c != '1') { // Se encontrar um caractere que não seja '0' ou '1'
      isBinario = false; // Não é binário
      break;             // Sai do loop
    }
  }

  if (isBinario) { // Se a string for binária (ex: "1010101")
    dias = strtol(diasStr.c_str(), nullptr, 2); // Converte a string binária para um inteiro base 2
  } else { // Se não for binária (presume que é um número decimal)
    dias = diasStr.toInt(); // Converte a string para um inteiro decimal
  }

  if (rele < 0 || rele >= NUM_RELES || slot < 0 || slot >= SLOTS_POR_RELE) { // Valida os números do relé e slot
    porta.println("Erro: rele ou slot inválido"); // Erro se forem inválidos
    return; // Sai da função
  }

  int hOn, mOn, hOff, mOff; // Variáveis para horas e minutos ON/OFF
  if (!parseHoraMinuto(partes[4], hOn, mOn) || !parseHoraMinuto(partes[5], hOff, mOff)) { // Analisa as strings de hora
    porta.println("Erro: formato hora inválido"); // Erro se o formato for inválido
    return; // Sai da função
  }

  AlarmeSlot &slotAlarme = alarmes[rele].slots[slot]; // Obtém uma referência para o objeto AlarmeSlot específico
  slotAlarme.diasSemana = dias;       // Atribui os dias da semana
  slotAlarme.horaOn = hOn;            // Atribui a hora de ligar
  slotAlarme.minutoOn = mOn;          // Atribui o minuto de ligar
  slotAlarme.horaOff = hOff;          // Atribui a hora de desligar
  slotAlarme.minutoOff = mOff;        // Atribui o minuto de desligar

  alarmes[rele].saveToEEPROM(); // Salva os alarmes deste relé na EEPROM
  if(imprime){ // Se a flag 'imprime' for true
    porta.println("Alarme atualizado."); // Imprime mensagem de sucesso
    modoAutomatico[rele] = true;        // Define o modo do relé para automático
    salvaModosNaEEPROM();               // Salva os modos na EEPROM
  }else{ // Se a flag 'imprime' for false (usado pelo comando RESET)
    modoAutomatico[rele] = false;       // Define o modo do relé para manual (para o comando RESET)
    salvaModosNaEEPROM();               // Salva os modos na EEPROM
  }
}

// Função para ajustar a hora do RTC (comando SETTIME)
void cmdSetTime(String cmd, Stream &porta){ // Recebe o comando e a porta de saída
  // SETTIME DD/MM/AAAA HH:MM:SS
  String param = cmd.substring(8); // Pega a parte da string após "SETTIME "
  param.trim();                    // Remove espaços em branco no início/fim
  int spaceIndex = param.indexOf(' '); // Encontra o espaço que separa a data da hora
  if (spaceIndex == -1) { // Se não encontrar o espaço
    porta.println("Erro: formato SETTIME inválido"); // Erro de formato
    return; // Sai da função
  }
  String dataStr = param.substring(0, spaceIndex);     // Extrai a string da data
  String horaStr = param.substring(spaceIndex + 1);    // Extrai a string da hora

  int dia, mes, ano;         // Variáveis para dia, mês, ano
  int hora, minuto, segundo; // Variáveis para hora, minuto, segundo

  // Analisa a string de data e hora completa
  if (!parseData(dataStr, dia, mes, ano) || !parseHoraCompleta(horaStr, hora, minuto, segundo)) {
    porta.println("Erro: formato data/hora inválido"); // Erro se o formato for inválido
    return; // Sai da função
  }

  rtc.adjust(DateTime(ano, mes, dia, hora, minuto, segundo)); // Ajusta o RTC com a nova data e hora
  porta.println("RTC ajustado."); // Mensagem de sucesso
}

// Função para obter e imprimir o status dos relés (comando STATUS)
void cmdStatus(String cmd, Stream &porta){ // Recebe o comando (não usado aqui) e a porta de saída
  porta.print("{\"relays\":["); // Início do objeto JSON com um array de relés
  for (int i = 0; i < NUM_RELES; i++) { // Itera sobre cada relé
    porta.print("{\"id\":"); // Início do objeto relé JSON
    porta.print(i);          // ID do relé
    porta.print(",\"Modo\":\""); // Campo "Modo"
    porta.print(modoAutomatico[i] ? "A" : "M"); // "A" para Automático, "M" para Manual
    porta.print("\",\"status\":\""); // Campo "status"
    porta.print(bitRead(estadoRele, i) == 1 ? "ON" : "OFF"); // "ON" se o bit do relé for 1, "OFF" se for 0
    porta.print("\"}"); // Fecha o objeto relé JSON
    if (i < NUM_RELES - 1) { // Se não for o último relé
      porta.print(","); // Adiciona vírgula
    }
  }
  porta.println("]}"); // Fecha o array de relés e o objeto JSON, adiciona nova linha
}

// Função para exibir a lista de comandos disponíveis (comando HELP)
void cmdHelp(String cmd, Stream &porta){ // Recebe o comando (não usado aqui) e a porta de saída
  porta.println("Comandos:"); // Cabeçalho
  porta.println("LIST - lista alarmes"); // Comando LIST
  porta.println("SET <rele> <slot> <dias> <horaOn> <horaOff> - configura alarme"); // Comando SET e seus parâmetros
  porta.println("  exemplo: SET 1 0 62 08:00 18:00"); // Exemplo de uso do SET
  porta.println("SETTIME <DD/MM/AAAA> <HH:MM:SS> - ajusta RTC"); // Comando SETTIME e seus parâmetros
  porta.println("  exemplo: SETTIME 10/07/2025 15:45:00"); // Exemplo de uso do SETTIME
  porta.println("STATUS - mostra estado dos relés"); // Comando STATUS
  porta.println("HELP - mostra comandos"); // Comando HELP
}

// Função para ativar manualmente um relé (comando ON)
void cmdOn(String cmd, Stream &porta){ // Recebe o comando e a porta de saída
  int rele = cmd.substring(3).toInt(); // Extrai o número do relé da string (após "ON ")
  if (rele >= 0 && rele < NUM_RELES) { // Valida o número do relé
    modoAutomatico[rele] = false;    // Define o modo do relé para manual
    statusRele[rele] = true;         // Define o estado manual para ON
    //bitWrite(estadoRele, rele, true); // Linha comentada: não atualiza 'estadoRele' diretamente aqui
    atualizaShiftRegister();         // Força a atualização do Shift Register com o novo estado (será pego no próximo loop)
    porta.print("Relé ");           // Imprime mensagem de confirmação
    porta.print(rele);
    porta.println(" ativado manualmente.");
    salvaModosNaEEPROM();           // Salva os modos dos relés na EEPROM
  } else {
    porta.println("Relé inválido."); // Erro de relé inválido
  }
}

// Função para desativar manualmente um relé (comando OFF)
void cmdOff(String cmd, Stream &porta){ // Recebe o comando e a porta de saída
  int rele = cmd.substring(4).toInt(); // Extrai o número do relé da string (após "OFF ")
  if (rele >= 0 && rele < NUM_RELES) { // Valida o número do relé
    modoAutomatico[rele] = false;    // Define o modo do relé para manual
    statusRele[rele] = false;        // Define o estado manual para OFF
    //bitWrite(estadoRele, rele, false); // Linha comentada: não atualiza 'estadoRele' diretamente aqui
    atualizaShiftRegister();         // Força a atualização do Shift Register com o novo estado (será pego no próximo loop)
    porta.print("Relé ");           // Imprime mensagem de confirmação
    porta.print(rele);
    porta.println(" desativado manualmente.");
    salvaModosNaEEPROM();           // Salva os modos dos relés na EEPROM
  } else {
    porta.println("Relé inválido."); // Erro de relé inválido
  }
}

// Função para definir o modo de um relé para automático ou manual (comando AUTO)
void cmdAuto(String cmd, Stream &porta){ // Recebe o comando e a porta de saída
  int modo = cmd.substring(5, 6).toInt(); // Extrai o modo (0 ou 1) da string (ex: "AUTO 1")
  if (modo != 0 && modo != 1) { // Valida o modo
    porta.println("Uso: AUTO <modo> <rele>"); // Mensagem de uso correto
    return; // Sai da função
  }
  int releNum = cmd.substring(7).toInt(); // Extrai o número do relé da string (ex: "AUTO 1 0")
  if (releNum < 0 || releNum >= NUM_RELES) { // Valida o número do relé
    porta.println("Rele invalido."); // Erro de relé inválido
    return; // Sai da função
  }
  modoAutomatico[releNum] = (modo == 1); // Define o modo do relé com base no valor de 'modo'
  porta.print("Rele "); // Imprime mensagem de confirmação
  porta.print(releNum);
  porta.print(" voltou para modo ");
  porta.println((modo == 1) ? "AUTOMATICO." : "MANUAL."); // Indica o modo definido
  salvaModosNaEEPROM(); // Salva os modos dos relés na EEPROM
}

// Função para obter e imprimir a hora atual do RTC (comando TIME)
void cmdTime(String cmd, Stream &porta){ // Recebe o comando (não usado aqui) e a porta de saída
  DateTime now = rtc.now(); // Obtém a data e hora atual do RTC

  porta.print("{\"data\":\""); // Início do objeto JSON e campo "data"
  if (now.day() < 10) porta.print('0'); // Adiciona zero à esquerda se o dia for menor que 10
  porta.print(now.day()); porta.print('/'); // Imprime dia e '/'
  if (now.month() < 10) porta.print('0'); // Adiciona zero à esquerda se o mês for menor que 10
  porta.print(now.month()); porta.print('/'); // Imprime mês e '/'
  porta.print(now.year()); // Imprime ano

  porta.print("\",\"hora\":\""); // Campo "hora"
  if (now.hour() < 10) porta.print('0'); // Adiciona zero à esquerda se a hora for menor que 10
  porta.print(now.hour()); porta.print(':'); // Imprime hora e ':'
  if (now.minute() < 10) porta.print('0'); // Adiciona zero à esquerda se o minuto for menor que 10
  porta.print(now.minute()); porta.print(':'); // Imprime minuto e ':'
  if (now.second() < 10) porta.print('0'); // Adiciona zero à esquerda se o segundo for menor que 10
  porta.print(now.second()); // Imprime segundo
  porta.println("\"}"); // Fecha o campo "hora", o objeto JSON e adiciona nova linha
}

// Função principal que despacha os comandos recebidos para as funções apropriadas
void processaComando(String cmd, Stream &porta) {
  cmd.trim();        // Remove espaços em branco do início e fim da string do comando
  cmd.toUpperCase(); // Converte o comando para letras maiúsculas para facilitar a comparação

  if (cmd == "LIST") { // Se o comando for "LIST"
    listData(cmd, porta); // Chama a função para listar os alarmes
  } else if (cmd.startsWith("SET ")) { // Se o comando começar com "SET "
    cmdSet(cmd, porta); // Chama a função para configurar um alarme
  } else if (cmd.startsWith("SETTIME ")) { // Se o comando começar com "SETTIME "
    cmdSetTime(cmd, porta); // Chama a função para ajustar a hora do RTC
  } else if (cmd == "STATUS") { // Se o comando for "STATUS"
    cmdStatus(cmd, porta); // Chama a função para mostrar o status dos relés
  } else if (cmd == "HELP") { // Se o comando for "HELP"
    cmdHelp(cmd, porta); // Chama a função para mostrar a ajuda
  } else if (cmd.startsWith("ON ")) { // Se o comando começar com "ON "
    cmdOn(cmd, porta); // Chama a função para ligar um relé manualmente
  } else if (cmd.startsWith("OFF ")) { // Se o comando começar com "OFF "
    cmdOff(cmd, porta); // Chama a função para desligar um relé manualmente
  } else if (cmd.startsWith("AUTO ")) { // Se o comando começar com "AUTO "
    cmdAuto(cmd, porta); // Chama a função para definir o modo automático/manual
  } else if (cmd == "TIME") { // Se o comando for "TIME"
    cmdTime(cmd, porta); // Chama a função para obter a hora atual
  } else if (cmd == "RESET") { // Se o comando for "RESET" (resetar todos os alarmes)
    String comandoReset; // Variável temporária para construir o comando SET
    for(int i = 0; i < NUM_RELES; i++){ // Itera sobre cada relé
      for(int j = 0; j < SLOTS_POR_RELE; j++){ // Itera sobre cada slot de alarme
        // Constrói um comando SET para cada slot, definindo-o para ligar às 12:00 e desligar às 12:01 (essencialmente desativando-o)
        comandoReset = "SET " + String(i) + " " + String(j) + " 0 12:00 12:01";
        comandoReset.trim(); // Remove espaços em branco
        cmdSet(comandoReset, porta, false); // Chama cmdSet para aplicar a configuração, sem imprimir mensagens (false)
      }
    }
    porta.println("Alarmes resetados."); // Mensagem de confirmação de reset
  } else { // Se o comando não for reconhecido
    porta.println("Comando nao reconhecido."); // Imprime mensagem de erro
  }
}

// Função auxiliar para analisar uma string de hora no formato "HH:MM"
bool parseHoraMinuto(String s, int &hora, int &minuto) {
  int colon = s.indexOf(':'); // Encontra a posição do ':'
  if (colon == -1) return false; // Se não encontrar, formato inválido
  hora = s.substring(0, colon).toInt(); // Extrai a hora
  minuto = s.substring(colon + 1).toInt(); // Extrai o minuto
  if (hora < 0 || hora > 23 || minuto < 0 || minuto > 59) return false; // Valida os valores de hora e minuto
  return true; // Parsing bem-sucedido
}

// Função auxiliar para analisar uma string de data no formato "DD/MM/AAAA"
bool parseData(String s, int &dia, int &mes, int &ano) {
  int firstSlash = s.indexOf('/');    // Encontra a posição da primeira '/'
  int lastSlash = s.lastIndexOf('/'); // Encontra a posição da última '/'
  if (firstSlash == -1 || lastSlash == -1 || firstSlash == lastSlash) return false; // Verifica se as barras estão presentes e separadas

  dia = s.substring(0, firstSlash).toInt(); // Extrai o dia
  mes = s.substring(firstSlash + 1, lastSlash).toInt(); // Extrai o mês
  ano = s.substring(lastSlash + 1).toInt(); // Extrai o ano

  if (dia < 1 || dia > 31 || mes < 1 || mes > 12 || ano < 2000 || ano > 2099) return false; // Valida os valores de dia, mês, ano
  return true; // Parsing bem-sucedido
}

// Função auxiliar para analisar uma string de hora completa no formato "HH:MM:SS"
bool parseHoraCompleta(String s, int &hora, int &minuto, int &segundo) {
  int firstColon = s.indexOf(':');    // Encontra a posição do primeiro ':'
  int lastColon = s.lastIndexOf(':'); // Encontra a posição do último ':'
  if (firstColon == -1 || lastColon == -1 || firstColon == lastColon) return false; // Verifica se os dois ':' estão presentes e separados

  hora = s.substring(0, firstColon).toInt(); // Extrai a hora
  minuto = s.substring(firstColon + 1, lastColon).toInt(); // Extrai o minuto
  segundo = s.substring(lastColon + 1).toInt(); // Extrai o segundo

  if (hora < 0 || hora > 23 || minuto < 0 || minuto > 59 || segundo < 0 || segundo > 59) return false; // Valida os valores de hora, minuto, segundo
  return true; // Parsing bem-sucedido
}
