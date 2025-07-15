#include <Wire.h> // Inclui a biblioteca Wire para comunicação I2C (Inter-Integrated Circuit), utilizada para interagir com o módulo RTC.
#include <RTClib.h> // Inclui a biblioteca RTClib, que fornece funcionalidades para o módulo de relógio de tempo real (RTC).
#include <EEPROM.h> // Inclui a biblioteca EEPROM, que permite ler e escrever dados de forma persistente na memória EEPROM do microcontrolador.
#include <SoftwareSerial.h> // Inclui a biblioteca SoftwareSerial, que permite criar portas seriais adicionais em quaisquer pinos digitais.

#define LATCH_PIN 3 // Define o pino digital 3 como o pino LATCH (ST_CP) para o registrador de deslocamento (shift register), geralmente um 74HC595.
#define CLOCK_PIN 4 // Define o pino digital 4 como o pino CLOCK (SH_CP) para o registrador de deslocamento.
#define DATA_PIN 2 // Define o pino digital 2 como o pino DATA (DS) para o registrador de deslocamento.

#define NUM_RELES 4 // Define o número total de relés que serão controlados por este sistema.
#define SLOTS_POR_RELE 2 // Define o número de "slots" ou agendamentos de alarme que cada relé pode ter.

RTC_DS1307 rtc; // Cria uma instância do objeto RTC_DS1307, representando o módulo de relógio de tempo real DS1307.

uint8_t estadoRele = 0; // Variável de 8 bits (um byte) para armazenar o estado ON/OFF dos relés (cada bit pode representar um relé).
bool statusRele[NUM_RELES] = {false, false, false, false}; // Array booleano para armazenar o estado manual (ON/OFF) de cada relé. Inicialmente todos desligados.
bool modoAutomatico[NUM_RELES] = {true, true, true, true}; // Array booleano para indicar se cada relé está no modo automático (controlado pelo timer) ou manual. Inicialmente todos automáticos.

// HC-05 Bluetooth - RX=10, TX=9 (ajuste conforme seu hardware)
// ATENÇÃO: Os pinos 1 (TX) e 0 (RX) são os pinos seriais HARDWARE do Arduino Uno/Nano.
// Usar SoftwareSerial nestes pinos pode causar conflito com a comunicação USB do Serial Monitor.
SoftwareSerial bluetoothSerial(1, 0); // Cria uma instância de SoftwareSerial para comunicação com o módulo Bluetooth, usando o pino digital 1 como RX e o pino digital 0 como TX.

// Definição da classe AlarmeSlot, que representa um agendamento individual para um relé.
class AlarmeSlot {
public:
  uint8_t diasSemana; // Campo para armazenar os dias da semana em que o alarme estará ativo. Cada bit representa um dia (0=domingo, 6=sábado).
  uint8_t horaOn, minutoOn; // Campos para armazenar a hora e o minuto em que o relé deve ligar.
  uint8_t horaOff, minutoOff; // Campos para armazenar a hora e o minuto em que o relé deve desligar.
  uint8_t relesAdd, modoAdd; // Campos adicionais, que podem ser para futuras funcionalidades ou depuração (não são usados nos métodos de agendamento aqui).

  // Construtor padrão da classe AlarmeSlot.
  AlarmeSlot() {
    diasSemana = 0; // Inicializa os dias da semana como 0 (nenhum dia selecionado).
    horaOn = minutoOn = horaOff = minutoOff = 0; // Inicializa todas as horas e minutos como 0.
  }

  // Método para carregar os dados de um AlarmeSlot da memória EEPROM.
  void loadFromEEPROM(int addr) {
    diasSemana = EEPROM.read(addr); // Lê o byte dos dias da semana do endereço 'addr'.
    horaOn = EEPROM.read(addr + 1); // Lê a hora de ligar do endereço 'addr + 1'.
    minutoOn = EEPROM.read(addr + 2); // Lê o minuto de ligar do endereço 'addr + 2'.
    horaOff = EEPROM.read(addr + 3); // Lê a hora de desligar do endereço 'addr + 3'.
    minutoOff = EEPROM.read(addr + 4); // Lê o minuto de desligar do endereço 'addr + 4'.
    relesAdd = EEPROM.read(addr + 5); // Lê o valor de 'relesAdd' do endereço 'addr + 5'.
    modoAdd = EEPROM.read(addr + 6); // Lê o valor de 'modoAdd' do endereço 'addr + 6'.
  }

  // Método para salvar os dados de um AlarmeSlot na memória EEPROM.
  void saveToEEPROM(int addr) {
    EEPROM.update(addr, diasSemana); // Escreve 'diasSemana' no endereço 'addr' apenas se o valor for diferente do que já está lá (otimização de vida útil da EEPROM).
    EEPROM.update(addr + 1, horaOn); // Escreve 'horaOn' no endereço 'addr + 1'.
    EEPROM.update(addr + 2, minutoOn); // Escreve 'minutoOn' no endereço 'addr + 2'.
    EEPROM.update(addr + 3, horaOff); // Escreve 'horaOff' no endereço 'addr + 3'.
    EEPROM.update(addr + 4, minutoOff); // Escreve 'minutoOff' no endereço 'addr + 4'.
    EEPROM.update(addr + 5, relesAdd); // Escreve 'relesAdd' no endereço 'addr + 5'.
    EEPROM.update(addr + 6, modoAdd); // Escreve 'modoAdd' no endereço 'addr + 6'.
  }

  // Método para verificar se um dia da semana específico está habilitado para este alarme.
  bool isDiaValido(uint8_t diaSemana) {
    // 'diaSemana': 0 = domingo, 1 = segunda-feira, ..., 6 = sábado (compatível com RTClib).
    // Retorna true se o bit correspondente ao 'diaSemana' estiver setado na variável 'diasSemana'.
    return (diasSemana & (1 << diaSemana)) != 0;
  }

  // Método para verificar se o alarme está ativo em um determinado dia e hora.
  bool estaAtivo(uint8_t diaSemana, uint8_t horaAtual, uint8_t minAtual) {
    if (!isDiaValido(diaSemana)) return false; // Se o dia da semana não estiver habilitado, o alarme não está ativo.

    int minutoAtual = horaAtual * 60 + minAtual; // Converte a hora e minuto atuais para o total de minutos desde a meia-noite.
    int minOn = horaOn * 60 + minutoOn; // Converte a hora e minuto de ligar para o total de minutos.
    int minOff = horaOff * 60 + minutoOff; // Converte a hora e minuto de desligar para o total de minutos.

    if (minOn < minOff) {
      // Caso 1: O intervalo de alarme não passa da meia-noite (ex: liga às 08:00, desliga às 18:00).
      // O alarme está ativo se a hora atual estiver entre a hora de ligar e a hora de desligar.
      return (minutoAtual >= minOn && minutoAtual < minOff);
    } else {
      // Caso 2: O intervalo de alarme passa da meia-noite (ex: liga às 22:00, desliga às 06:00 do dia seguinte).
      // O alarme está ativo se a hora atual for maior ou igual à hora de ligar (no dia atual) OU
      // se a hora atual for menor que a hora de desligar (considerando que ligou no dia anterior).
      return (minutoAtual >= minOn || minutoAtual < minOff);
    }
  }
};

// Função para salvar o modo de operação (automático/manual) de cada relé na EEPROM.
void salvaModosNaEEPROM() {
  for (int i = 0; i < NUM_RELES; i++) { // Itera por todos os relés.
    // Salva 1 se o relé estiver em modo automático (true), ou 0 se estiver em modo manual (false), no endereço 40 + índice do relé.
    EEPROM.update(40 + i, modoAutomatico[i] ? 1 : 0);
  }
}

// Função para carregar o modo de operação (automático/manual) de cada relé da EEPROM.
void carregaModosDaEEPROM() {
  for (int i = 0; i < NUM_RELES; i++) { // Itera por todos os relés.
    uint8_t val = EEPROM.read(40 + i); // Lê o valor salvo na EEPROM para o relé atual.
    modoAutomatico[i] = (val == 1); // Converte o valor lido (1 ou 0) para booleano (true ou false) e atribui ao array 'modoAutomatico'.
  }
}

// Função auxiliar para trocar os valores de duas variáveis inteiras (não utilizada no código fornecido).
void swapInt(int &a, int &b) {
  int temp = a; // Armazena o valor de 'a' em uma variável temporária.
  a = b; // Atribui o valor de 'b' a 'a'.
  b = temp; // Atribui o valor temporário (original de 'a') a 'b'.
}

// Definição da classe TimerAlarme, que gerencia todos os slots de alarme para um único relé.
class TimerAlarme {
  public:
  uint8_t releIndex; // O índice do relé ao qual esta instância de TimerAlarme se refere.
  AlarmeSlot slots[SLOTS_POR_RELE]; // Um array de objetos AlarmeSlot, contendo os agendamentos para este relé.
  int eepromAddrBase; // O endereço inicial na EEPROM onde os dados dos slots deste relé são armazenados.

  // Construtor da classe TimerAlarme.
  TimerAlarme(uint8_t index, int eepromAddr) {
    releIndex = index; // Atribui o índice do relé.
    eepromAddrBase = eepromAddr; // Atribui o endereço base da EEPROM.
    loadFromEEPROM(); // Chama o método para carregar os dados dos slots da EEPROM assim que o objeto é criado.
  }

  // Método para carregar todos os slots de alarme deste relé da EEPROM.
  void loadFromEEPROM() {
    for (int i = 0; i < SLOTS_POR_RELE; i++) { // Itera sobre cada slot configurado para este relé.
      // Carrega os dados de cada slot da EEPROM, calculando o endereço específico para o slot atual.
      // Cada slot ocupa 7 bytes na EEPROM, conforme a definição da classe AlarmeSlot e seus métodos load/save.
      slots[i].loadFromEEPROM(eepromAddrBase + i * 7); // (Endereço base + (índice do slot * 7 bytes/slot)).
    }
  }

  // Método para salvar todos os slots de alarme deste relé na EEPROM.
  void saveToEEPROM() {
    for (int i = 0; i < SLOTS_POR_RELE; i++) { // Itera sobre cada slot.
      // Salva os dados de cada slot na EEPROM, calculando o endereço específico.
      slots[i].saveToEEPROM(eepromAddrBase + i * 7); // (Endereço base + (índice do slot * 7 bytes/slot)).
    }
  }

  // Método para determinar se o relé deve ser ligado com base nos agendamentos (slots) e na hora atual.
  bool deveLigar(DateTime now) {
    uint8_t diaSemana = now.dayOfTheWeek(); // Obtém o dia da semana atual do RTC (0=domingo, 1=segunda...).
    uint8_t hora = now.hour(); // Obtém a hora atual do RTC.
    uint8_t min = now.minute(); // Obtém o minuto atual do RTC.
    int minutoAtual = hora * 60 + min; // Converte a hora e minuto atuais para o total de minutos desde a meia-noite.

    for (int i = 0; i < SLOTS_POR_RELE; i++) { // Itera sobre cada slot de alarme para este relé.
      AlarmeSlot slot = slots[i]; // Cria uma cópia do slot atual para trabalhar (evita modificar o original acidentalmente).

      int minutoOn = slot.horaOn * 60 + slot.minutoOn; // Converte a hora e minuto de ligar do slot para minutos totais.
      int minutoOff = slot.horaOff * 60 + slot.minutoOff; // Converte a hora e minuto de desligar do slot para minutos totais.

      if (minutoOn < minutoOff) {
        // Caso 1: O intervalo de alarme não passa da meia-noite (ex: liga às 08:00, desliga às 18:00 do mesmo dia).
        // Verifica se o dia da semana é válido E se o minuto atual está dentro do intervalo [minOn, minOff).
        if (slot.isDiaValido(diaSemana) &&
            minutoAtual >= minutoOn &&
            minutoAtual < minutoOff) {
          return true; // Se encontrou UM slot válido que deveria ligar, o relé deve ser ligado.
        }
      } else {
        // Caso 2: O intervalo de alarme passa da meia-noite (ex: liga às 22:00, desliga às 06:00 do dia seguinte).
        // O alarme pode estar ativo em duas situações:
        // a) Se for no dia atual E o minuto atual for maior ou igual à hora de ligar (ex: 22:00 -> 23:59).
        if (slot.isDiaValido(diaSemana) &&
            minutoAtual >= minutoOn) {
          return true;
        }
        // b) Ou se for no dia anterior (que ligou) E o minuto atual for menor que a hora de desligar (ex: 00:00 -> 05:59).
        int diaAnterior = (diaSemana + 6) % 7; // Calcula o dia da semana anterior (ex: segunda(1) -> domingo(0)).
        if (slot.isDiaValido(diaAnterior) &&
            minutoAtual < minutoOff) {
          return true;
        }
      }
    }

    return false; // Se nenhum slot ativo foi encontrado após verificar todos, o relé deve ser desligado.
  }

  // Método para atualizar o bit correspondente ao relé no byte global 'estadoRele'.
  void updateEstado(DateTime now) {
    bool ligar = deveLigar(now); // Determina se o relé deve ser ligado ou desligado com base nos timers.
    bitWrite(estadoRele, releIndex, ligar); // Define o bit do 'releIndex' em 'estadoRele' para 'ligar' (true/false).
  }
};

// Declaração de um array de objetos TimerAlarme, um para cada relé.
// Cada objeto é inicializado com seu índice de relé e o endereço base na EEPROM para seus slots.
// O cálculo do endereço base é (índice do relé * número de slots por relé * 7 bytes por slot).
// Ex: Relé 0: 0 * 2 * 7 = 0
//     Relé 1: 1 * 2 * 7 = 14
//     Relé 2: 2 * 2 * 7 = 28
//     Relé 3: 3 * 2 * 7 = 42
TimerAlarme alarmes[NUM_RELES] = {
  TimerAlarme(0, 0),
  TimerAlarme(1, SLOTS_POR_RELE * 7), // 2 slots * 7 bytes/slot = 14
  TimerAlarme(2, SLOTS_POR_RELE * 14), // 2 slots * 14 bytes/slot = 28
  TimerAlarme(3, SLOTS_POR_RELE * 21) // 2 slots * 21 bytes/slot = 42
};


// Declarações de funções auxiliares para parsing de strings de data e hora. (Implementadas mais abaixo no código)
bool parseHoraMinuto(String s, int &hora, int &minuto); // Analisa uma string no formato "HH:MM".
bool parseData(String s, int &dia, int &mes, int &ano); // Analisa uma string no formato "DD/MM/AAAA".
bool parseHoraCompleta(String s, int &hora, int &int segundo); // Analisa uma string no formato "HH:MM:SS".
void processaComando(String cmd, Stream &porta); // Declaração da função para processar comandos recebidos.

void setup() {
  Serial.begin(9600); // Inicializa a comunicação serial com o monitor de serial do computador, a 9600 bits por segundo.
  bluetoothSerial.begin(9600); // Inicializa a comunicação serial com o módulo Bluetooth, também a 9600 bits por segundo.

  Wire.begin(); // Inicializa a biblioteca Wire para comunicação I2C, necessária para o RTC.

  pinMode(LATCH_PIN, OUTPUT); // Configura o pino LATCH como saída.
  pinMode(CLOCK_PIN, OUTPUT); // Configura o pino CLOCK como saída.
  pinMode(DATA_PIN, OUTPUT); // Configura o pino DATA como saída.

  if (!rtc.begin()) { // Tenta inicializar o módulo RTC.
    Serial.println("Erro: RTC nao encontrado"); // Se o RTC não for detectado, imprime uma mensagem de erro na serial.
    //while(1); // Linha comentada: se descomentada, o programa travaria aqui se o RTC não fosse encontrado, útil para depuração inicial.
  }

  if (!rtc.isrunning()) { // Verifica se o RTC está rodando e mantendo a hora (indicativo de que a bateria está boa).
    Serial.println("RTC nao estava rodando, ajustando para o horario de compilacao."); // Se não estiver rodando, imprime uma mensagem.
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Ajusta a hora do RTC para a data e hora em que o código foi compilado.
  }

  Serial.println("Sistema iniciado."); // Imprime uma mensagem de que o sistema foi iniciado no Serial Monitor.
  bluetoothSerial.println("Sistema iniciado."); // Imprime a mesma mensagem no Bluetooth.
  carregaModosDaEEPROM(); // Chama a função para carregar os modos de operação (automático/manual) dos relés da EEPROM.
}

void loop() {
  // Checa comandos Serial
  if (Serial.available()) { // Verifica se há dados disponíveis para leitura na porta serial USB.
    String cmd = Serial.readStringUntil('\n'); // Lê a string de comando da serial até encontrar um caractere de nova linha.
    processaComando(cmd, Serial); // Chama a função 'processaComando' para lidar com o comando, passando 'Serial' como porta de resposta.
  }

  // Checa comandos Bluetooth
  if (bluetoothSerial.available()) { // Verifica se há dados disponíveis para leitura na porta serial do Bluetooth.
    String cmd = bluetoothSerial.readStringUntil('\n'); // Lê a string de comando do Bluetooth até encontrar um caractere de nova linha.
    //Serial.println(cmd); // Linha comentada: Se descomentada, imprimiria o comando recebido via Bluetooth no Serial Monitor (para debug).
    processaComando(cmd, bluetoothSerial); // Chama a função 'processaComando' para lidar com o comando, passando 'bluetoothSerial' como porta de resposta.
  }

  DateTime now = rtc.now(); // Obtém a data e hora atuais do módulo RTC.
  uint8_t novoEstado = 0; // Variável temporária para calcular o próximo estado dos relés antes de aplicá-lo.

  for (int i = 0; i < NUM_RELES; i++) { // Itera por cada um dos relés.
    if (modoAutomatico[i]) { // Se o relé atual estiver configurado para o modo automático.
      // estado pelo timer
      bool ligar = alarmes[i].deveLigar(now); // Chama o método 'deveLigar' do TimerAlarme correspondente para determinar se o relé deve ser ligado ou desligado com base nos agendamentos.
      bitWrite(novoEstado, i, ligar); // Define o bit correspondente ao relé 'i' em 'novoEstado' para 'ligar' (true=1, false=0).
    } else { // Se o relé atual estiver configurado para o modo manual.
      // estado manual armazenado em statusRele
      bitWrite(novoEstado, i, statusRele[i]); // Define o bit correspondente ao relé 'i' em 'novoEstado' para o valor armazenado em 'statusRele[i]'.
    }
  }

  estadoRele = novoEstado; // Atualiza a variável global 'estadoRele' com o estado calculado para todos os relés.
  atualizaShiftRegister(); // Chama a função para enviar o novo estado de 'estadoRele' para o registrador de deslocamento, atualizando os relés fisicamente.

  delay(1000); // Pausa o programa por 1000 milissegundos (1 segundo) antes de repetir o loop. Isso controla a frequência de verificação do RTC e processamento de comandos.
}

// Função para enviar o estado dos relés para o registrador de deslocamento 74HC595.
void atualizaShiftRegister() {
  digitalWrite(LATCH_PIN, LOW); // Coloca o pino LATCH em LOW para preparar o registrador de deslocamento para receber novos dados.
  shiftOut(DATA_PIN, CLOCK_PIN, MSBFIRST, estadoRele); // Envia o byte 'estadoRele' bit a bit para o registrador de deslocamento.
                                                      // 'MSBFIRST' significa que o bit mais significativo é enviado primeiro.
  digitalWrite(LATCH_PIN, HIGH); // Coloca o pino LATCH em HIGH para transferir os dados do registrador de entrada para as saídas, atualizando o estado dos relés.
}

// Função auxiliar para imprimir a hora e o minuto formatados (HH:MM) em uma porta serial (Serial ou Bluetooth).
void printHora(uint8_t h, uint8_t m, Stream &porta) {
  if (h < 10) porta.print("0"); // Se a hora for menor que 10, adiciona um '0' à esquerda para formatação (ex: 08:00).
  porta.print(h); // Imprime a hora.
  porta.print(":"); // Imprime o separador de minutos.
  if (m < 10) porta.print("0"); // Se o minuto for menor que 10, adiciona um '0' à esquerda.
  porta.print(m); // Imprime o minuto.
}

// Função para listar todos os agendamentos (alarmes) em formato JSON.
void listData(String cmd, Stream &porta){ // Recebe o comando (não utilizado diretamente nesta função) e a porta serial para saída.
  porta.print("{"); // Início do objeto JSON principal.
    for (int i = 0; i < NUM_RELES; i++) { // Itera sobre cada relé.
      porta.print("\"r"); porta.print(i); porta.print("\":["); // Cria uma chave para o relé (ex: "r0") e inicia um array JSON para seus slots.
      for (int s = 0; s < SLOTS_POR_RELE; s++) { // Itera sobre cada slot de alarme para o relé atual.
        AlarmeSlot &slot = alarmes[i].slots[s]; // Obtém uma referência para o objeto AlarmeSlot atual.
        porta.print("{\"d\":"); porta.print(slot.diasSemana); // Início do objeto JSON para o slot, com o campo "d" (dias da semana).
        porta.print(",\"on\":\""); printHora(slot.horaOn, slot.minutoOn, porta); // Adiciona o campo "on" com a hora de ligar formatada.
        porta.print("\",\"off\":\""); printHora(slot.horaOff, slot.minutoOff, porta); // Adiciona o campo "off" com a hora de desligar formatada.
        porta.print("\"}"); // Fecha o objeto JSON do slot.
        if (s < SLOTS_POR_RELE - 1) porta.print(","); // Adiciona uma vírgula se não for o último slot no array.
      }
      porta.print("]"); // Fecha o array JSON de slots para o relé atual.
      if (i < NUM_RELES - 1) porta.print(","); // Adiciona uma vírgula se não for o último relé no objeto principal.
    }
    porta.println("}"); // Fecha o objeto JSON principal e adiciona uma nova linha.
}

// Função para configurar um agendamento de alarme (comando SET).
void cmdSet(String cmd, Stream &porta, bool imprime) { // Recebe a string do comando, a porta serial para resposta e um booleano para controlar a impressão de mensagens.
  String partes[6]; // Array de strings para armazenar as 6 partes esperadas do comando (rele, slot, dias, horaOn, horaOff).
  byte pos = 4; // Define a posição inicial para extrair as partes, pulando "SET " (4 caracteres).

  for (int i = 0; i < 6; i++) { // Itera 6 vezes para extrair cada parte do comando.
    byte nextSpace = cmd.indexOf(' ', pos); // Encontra a posição do próximo espaço na string do comando.
    
    if (nextSpace == -1 && i < 5) { // Se não houver mais espaços e ainda faltarem parâmetros (menos de 5 partes lidas).
      porta.println("Erro: faltam parametros"); // Imprime uma mensagem de erro de sintaxe.
      return; // Sai da função.
    }

    if (nextSpace == -1) { // Se não houver mais espaços (é a última parte do comando).
      partes[i] = cmd.substring(pos); // Pega o restante da string como a última parte.
    } else { // Se ainda houver mais espaços (não é a última parte).
      partes[i] = cmd.substring(pos, nextSpace); // Pega a substring entre a 'pos' atual e o 'nextSpace'.
      pos = nextSpace + 1; // Atualiza 'pos' para o caractere após o espaço.
    }
  }

  byte rele = partes[0].toInt(); // Converte a primeira parte (índice do relé) para um byte.
  byte slot = partes[1].toInt(); // Converte a segunda parte (índice do slot) para um byte.

  int dias; // Variável para armazenar o valor dos dias da semana.
  String diasStr = partes[2]; // Pega a string que representa os dias da semana.

  bool isBinario = true; // Flag booleana para verificar se a string dos dias é um número binário.
  for (char c : diasStr) { // Itera sobre cada caractere na string 'diasStr'.
    if (c != '0' && c != '1') { // Se um caractere não for '0' nem '1'.
      isBinario = false; // Marca a flag como falsa (não é binário).
      break; // Sai do loop.
    }
  }

  if (isBinario) { // Se a string dos dias for reconhecida como binária.
    dias = strtol(diasStr.c_str(), nullptr, 2); // Converte a string binária para um inteiro base 2.
  } else { // Se a string dos dias não for binária (presume-se que seja um número decimal).
    dias = diasStr.toInt(); // Converte a string para um inteiro decimal.
  }

  if (rele < 0 || rele >= NUM_RELES || slot < 0 || slot >= SLOTS_POR_RELE) { // Valida se os valores de relé e slot estão dentro dos limites permitidos.
    porta.println("Erro: rele ou slot inválido"); // Imprime uma mensagem de erro se forem inválidos.
    return; // Sai da função.
  }

  int hOn, mOn, hOff, mOff; // Variáveis para armazenar a hora e minuto de ligar e desligar.
  if (!parseHoraMinuto(partes[4], hOn, mOn) || !parseHoraMinuto(partes[5], hOff, mOff)) { // Tenta analisar as strings de hora de ligar e desligar usando 'parseHoraMinuto'.
    porta.println("Erro: formato hora inválido"); // Imprime um erro se o formato da hora for inválido.
    return; // Sai da função.
  }

  AlarmeSlot &slotAlarme = alarmes[rele].slots[slot]; // Obtém uma referência para o objeto AlarmeSlot específico que será configurado.
  slotAlarme.diasSemana = dias; // Define os dias da semana para o slot.
  slotAlarme.horaOn = hOn; // Define a hora de ligar para o slot.
  slotAlarme.minutoOn = mOn; // Define o minuto de ligar para o slot.
  slotAlarme.horaOff = hOff; // Define a hora de desligar para o slot.
  slotAlarme.minutoOff = mOff; // Define o minuto de desligar para o slot.

  alarmes[rele].saveToEEPROM(); // Salva todos os slots de alarme deste relé na EEPROM.
  if(imprime){ // Se a flag 'imprime' for verdadeira (usado em comandos que devem dar feedback ao usuário).
    porta.println("Alarme atualizado."); // Imprime uma mensagem de sucesso.
    modoAutomatico[rele] = true; // Define o modo do relé para automático.
    salvaModosNaEEPROM(); // Salva o modo de operação dos relés na EEPROM.
  }else{ // Se a flag 'imprime' for falsa (usado internamente, como no comando RESET).
    modoAutomatico[rele] = false; // Define o modo do relé para manual (o comando RESET configura slots com '0 12:00 12:01', essencialmente desativando-os).
    salvaModosNaEEPROM(); // Salva o modo de operação dos relés na EEPROM.
  }
}

// Função para ajustar a hora e data do módulo RTC (comando SETTIME).
void cmdSetTime(String cmd, Stream &porta){ // Recebe a string do comando e a porta serial para resposta.
  // Formato esperado: SETTIME DD/MM/AAAA HH:MM:SS
  String param = cmd.substring(8); // Extrai a parte da string após "SETTIME " (8 caracteres).
  param.trim(); // Remove quaisquer espaços em branco no início ou fim da string.
  int spaceIndex = param.indexOf(' '); // Encontra a posição do espaço que separa a data da hora.
  if (spaceIndex == -1) { // Se não encontrar o espaço.
    porta.println("Erro: formato SETTIME inválido"); // Imprime uma mensagem de erro de formato.
    return; // Sai da função.
  }
  String dataStr = param.substring(0, spaceIndex); // Extrai a string da data.
  String horaStr = param.substring(spaceIndex + 1); // Extrai a string da hora.

  int dia, mes, ano; // Variáveis para armazenar o dia, mês e ano.
  int hora, minuto, segundo; // Variáveis para armazenar a hora, minuto e segundo.

  if (!parseData(dataStr, dia, mes, ano) || !parseHoraCompleta(horaStr, hora, minuto, segundo)) { // Tenta analisar as strings de data e hora completas.
    porta.println("Erro: formato data/hora inválido"); // Imprime um erro se o formato for inválido.
    return; // Sai da função.
  }

  rtc.adjust(DateTime(ano, mes, dia, hora, minuto, segundo)); // Ajusta a hora e data do RTC com os valores analisados.
  porta.println("RTC ajustado."); // Imprime uma mensagem de confirmação.
}

// Função para obter e imprimir o status atual de todos os relés em formato JSON.
void cmdStatus(String cmd, Stream &porta){ // Recebe o comando (não utilizado diretamente) e a porta serial para resposta.
  porta.print("{\"relays\":["); // Início do objeto JSON, com uma chave "relays" que contém um array.
  for (int i = 0; i < NUM_RELES; i++) { // Itera sobre cada relé.
    porta.print("{\"id\":"); // Início de um objeto JSON para o relé atual, com seu "id".
    porta.print(i); // Imprime o ID do relé.
    porta.print(",\"Modo\":\""); // Campo "Modo".
    porta.print(modoAutomatico[i] ? "A" : "M"); // Imprime "A" se automático, "M" se manual.
    porta.print("\",\"status\":\""); // Campo "status".
    porta.print(bitRead(estadoRele, i) == 1 ? "ON" : "OFF"); // Lê o bit correspondente ao relé em 'estadoRele' e imprime "ON" ou "OFF".
    porta.print("\"}"); // Fecha o objeto JSON do relé.
    if (i < NUM_RELES - 1) { // Se não for o último relé no array.
      porta.print(","); // Adiciona uma vírgula.
    }
  }
  porta.println("]}"); // Fecha o array "relays", o objeto JSON principal e adiciona uma nova linha.
}

// Função para exibir a lista de comandos disponíveis (comando HELP).
void cmdHelp(String cmd, Stream &porta){ // Recebe o comando (não utilizado diretamente) e a porta serial para resposta.
  porta.println("Comandos:"); // Cabeçalho da lista de comandos.
  porta.println("LIST - lista alarmes"); // Descrição do comando LIST.
  porta.println("SET <rele> <slot> <dias> <horaOn> <horaOff> - configura alarme"); // Descrição do comando SET e seus parâmetros.
  porta.println("  exemplo: SET 1 0 62 08:00 18:00"); // Exemplo de uso do comando SET.
  porta.println("SETTIME <DD/MM/AAAA> <HH:MM:SS> - ajusta RTC"); // Descrição do comando SETTIME e seus parâmetros.
  porta.println("  exemplo: SETTIME 10/07/2025 15:45:00"); // Exemplo de uso do comando SETTIME.
  porta.println("STATUS - mostra estado dos relés"); // Descrição do comando STATUS.
  porta.println("HELP - mostra comandos"); // Descrição do comando HELP.
}

// Função para ativar manualmente um relé (comando ON).
void cmdOn(String cmd, Stream &porta){ // Recebe a string do comando e a porta serial para resposta.
  int rele = cmd.substring(3).toInt(); // Extrai o número do relé da string (após "ON ").
  if (rele >= 0 && rele < NUM_RELES) { // Valida se o número do relé está dentro dos limites.
    modoAutomatico[rele] = false; // Define o modo do relé para manual.
    statusRele[rele] = true; // Define o estado manual do relé para ON (true).
    //bitWrite(estadoRele, rele, true); // Linha comentada: não é necessário atualizar 'estadoRele' aqui, pois 'atualizaShiftRegister' fará isso com o valor de 'statusRele'.
    atualizaShiftRegister(); // Força uma atualização imediata do registrador de deslocamento para refletir a mudança.
    porta.print("Relé "); // Imprime uma mensagem de confirmação.
    porta.print(rele);
    porta.println(" ativado manualmente.");
    salvaModosNaEEPROM(); // Salva os modos de operação dos relés na EEPROM.
  } else {
    porta.println("Relé inválido."); // Imprime uma mensagem de erro se o relé for inválido.
  }
}

// Função para desativar manualmente um relé (comando OFF).
void cmdOff(String cmd, Stream &porta){ // Recebe a string do comando e a porta serial para resposta.
  int rele = cmd.substring(4).toInt(); // Extrai o número do relé da string (após "OFF ").
  if (rele >= 0 && rele < NUM_RELES) { // Valida se o número do relé está dentro dos limites.
    modoAutomatico[rele] = false; // Define o modo do relé para manual.
    statusRele[rele] = false; // Define o estado manual do relé para OFF (false).
    //bitWrite(estadoRele, rele, false); // Linha comentada: não é necessário atualizar 'estadoRele' aqui.
    atualizaShiftRegister(); // Força uma atualização imediata do registrador de deslocamento.
    porta.print("Relé "); // Imprime uma mensagem de confirmação.
    porta.print(rele);
    porta.println(" desativado manualmente.");
    salvaModosNaEEPROM(); // Salva os modos de operação dos relés na EEPROM.
  } else {
    porta.println("Relé inválido."); // Imprime uma mensagem de erro se o relé for inválido.
  }
}

// Função para definir o modo de um relé para automático ou manual (comando AUTO).
void cmdAuto(String cmd, Stream &porta){ // Recebe a string do comando e a porta serial para resposta.
  int modo = cmd.substring(5).toInt(); // Extrai o valor do modo (0 para manual, 1 para automático) da string.
  if (modo != 0 && modo != 1) { // Valida se o valor do modo é 0 ou 1.
    porta.println("Uso: AUTO <modo> <rele>"); // Imprime uma mensagem de uso correto se o modo for inválido.
    return; // Sai da função.
  }
  int releNum = cmd.substring(7).toInt(); // Extrai o número do relé da string.
  if (releNum < 0 || releNum >= NUM_RELES) { // Valida se o número do relé é válido.
    porta.println("Rele invalido."); // Imprime uma mensagem de erro se o relé for inválido.
    return; // Sai da função.
  }
  modoAutomatico[releNum] = (modo) ? true : false; // Define o modo do relé (true para automático se 'modo' for 1, false para manual se 'modo' for 0).
  porta.print("Rele "); // Imprime uma mensagem de confirmação.
  porta.print(releNum);
  porta.print(" voltou para modo ");
  porta.println((modo) ? "AUTOMATICO." : "MANUAL."); // Indica se o relé está em modo AUTOMÁTICO ou MANUAL.
  salvaModosNaEEPROM(); // Salva os modos de operação dos relés na EEPROM.
}

// Função para obter e imprimir a hora e data atual do RTC (comando TIME).
void cmdTime(String cmd, Stream &porta){ // Recebe o comando (não utilizado diretamente) e a porta serial para resposta.
  DateTime now = rtc.now(); // Obtém a data e hora atuais do módulo RTC.

  porta.print("{\"data\":\""); // Início de um objeto JSON, com a chave "data".
  if (now.day() < 10) porta.print('0'); // Adiciona um '0' à esquerda se o dia for menor que 10.
  porta.print(now.day()); porta.print('/'); // Imprime o dia e o separador '/'.
  if (now.month() < 10) porta.print('0'); // Adiciona um '0' à esquerda se o mês for menor que 10.
  porta.print(now.month()); porta.print('/'); // Imprime o mês e o separador '/'.
  porta.print(now.year()); // Imprime o ano.

  porta.print("\",\"hora\":\""); // Chave "hora".
  if (now.hour() < 10) porta.print('0'); // Adiciona um '0' à esquerda se a hora for menor que 10.
  porta.print(now.hour()); porta.print(':'); // Imprime a hora e o separador ':'.
  if (now.minute() < 10) porta.print('0'); // Adiciona um '0' à esquerda se o minuto for menor que 10.
  porta.print(now.minute()); porta.print(':'); // Imprime o minuto e o separador ':'.
  if (now.second() < 10) porta.print('0'); // Adiciona um '0' à esquerda se o segundo for menor que 10.
  porta.print(now.second()); // Imprime o segundo.
  porta.println("\"}"); // Fecha a string da hora, o objeto JSON e adiciona uma nova linha.
}

// Função principal que processa os comandos recebidos de qualquer porta serial (USB ou Bluetooth).
void processaComando(String cmd, Stream &porta) {
  cmd.trim(); // Remove espaços em branco do início e do fim da string do comando.
  cmd.toUpperCase(); // Converte a string do comando para letras maiúsculas para facilitar a comparação (case-insensitive).
  
  if (cmd == "LIST") { // Se o comando for "LIST".
    listData(cmd, porta); // Chama a função para listar os agendamentos.
  } else if (cmd.startsWith("SET ")) { // Se o comando começar com "SET ".
    cmdSet(cmd, porta); // Chama a função para configurar um agendamento.
  } else if (cmd.startsWith("SETTIME ")) { // Se o comando começar com "SETTIME ".
    cmdSetTime(cmd, porta); // Chama a função para ajustar a hora do RTC.
  } else if (cmd == "STATUS") { // Se o comando for "STATUS".
    cmdStatus(cmd, porta); // Chama a função para mostrar o status dos relés.
  } else if (cmd == "HELP") { // Se o comando for "HELP".
    cmdHelp(cmd, porta); // Chama a função para exibir a ajuda.
  } else if (cmd.startsWith("ON ")) { // Se o comando começar com "ON ".
    cmdOn(cmd, porta); // Chama a função para ligar um relé manualmente.
  } else if (cmd.startsWith("OFF ")) { // Se o comando começar com "OFF ".
    cmdOff(cmd, porta); // Chama a função para desligar um relé manualmente.
  } else if (cmd.startsWith("AUTO ")) { // Se o comando começar com "AUTO ".
    cmdAuto(cmd, porta); // Chama a função para definir o modo automático/manual de um relé.
  } else if (cmd == "TIME") { // Se o comando for "TIME".
    cmdTime(cmd, porta); // Chama a função para exibir a hora e data atual.
  } else if (cmd == "RESET") { // Se o comando for "RESET" (para resetar todos os agendamentos).
    String comandoReset; // Variável temporária para construir o comando SET.
    for(int i = 0; i < NUM_RELES; i++){ // Itera sobre cada relé.
      for(int j = 0; j < SLOTS_POR_RELE; j++){ // Itera sobre cada slot de alarme.
        // Constrói um comando "SET" que define o slot para um agendamento mínimo (12:00 ON, 12:01 OFF) em nenhum dia (dias=0).
        // Isso efetivamente desativa o slot sem apagá-lo.
        comandoReset = "SET " + String(i) + " " + String(j) + " 0 12:00 12:01";
        comandoReset.trim(); // Remove espaços em branco do comando construído.
        cmdSet(comandoReset, porta, false); // Chama a função 'cmdSet' para aplicar a configuração, passando 'false' para não imprimir mensagem de "Alarme atualizado" para cada slot.
      }
    }
    porta.println("Alarmes resetados."); // Imprime uma mensagem de confirmação após resetar todos os alarmes.
  } else { // Se o comando não corresponder a nenhum dos comandos conhecidos.
    porta.println("Comando nao reconhecido."); // Imprime uma mensagem de que o comando não foi reconhecido.
  }
}

// Função auxiliar para analisar uma string de hora no formato "HH:MM" e extrair hora e minuto.
bool parseHoraMinuto(String s, int &hora, int &minuto) {
  int colon = s.indexOf(':'); // Encontra a posição do caractere de dois pontos ':'.
  if (colon == -1) return false; // Se ':' não for encontrado, o formato é inválido, retorna false.
  hora = s.substring(0, colon).toInt(); // Extrai a substring antes do ':' e a converte para inteiro (hora).
  minuto = s.substring(colon + 1).toInt(); // Extrai a substring após o ':' e a converte para inteiro (minuto).
  if (hora < 0 || hora > 23 || minuto < 0 || minuto > 59) return false; // Valida se hora e minuto estão dentro dos intervalos válidos.
  return true; // Retorna true se a análise for bem-sucedida e os valores forem válidos.
}

// Função auxiliar para analisar uma string de data no formato "DD/MM/AAAA" e extrair dia, mês e ano.
bool parseData(String s, int &dia, int &mes, int &ano) {
  int firstSlash = s.indexOf('/'); // Encontra a posição da primeira barra '/'.
  int lastSlash = s.lastIndexOf('/'); // Encontra a posição da última barra '/'.
  if (firstSlash == -1 || lastSlash == -1 || firstSlash == lastSlash) return false; // Verifica se há duas barras e se elas estão separadas.

  dia = s.substring(0, firstSlash).toInt(); // Extrai o dia.
  mes = s.substring(firstSlash + 1, lastSlash).toInt(); // Extrai o mês.
  ano = s.substring(lastSlash + 1).toInt(); // Extrai o ano.

  if (dia < 1 || dia > 31 || mes < 1 || mes > 12 || ano < 2000 || ano > 2099) return false; // Valida se dia, mês e ano estão dentro de intervalos razoáveis.
  return true; // Retorna true se a análise for bem-sucedida e os valores forem válidos.
}

// Função auxiliar para analisar uma string de hora completa no formato "HH:MM:SS" e extrair hora, minuto e segundo.
bool parseHoraCompleta(String s, int &hora, int &minuto, int &segundo) {
  int firstColon = s.indexOf(':'); // Encontra a posição do primeiro ':'.
  int lastColon = s.lastIndexOf(':'); // Encontra a posição do último ':'.
  if (firstColon == -1 || lastColon == -1 || firstColon == lastColon) return false; // Verifica se há dois ':' e se eles estão separados.

  hora = s.substring(0, firstColon).toInt(); // Extrai a hora.
  minuto = s.substring(firstColon + 1, lastColon).toInt(); // Extrai o minuto.
  segundo = s.substring(lastColon + 1).toInt(); // Extrai o segundo.

  if (hora < 0 || hora > 23 || minuto < 0 || minuto > 59 || segundo < 0 || segundo > 59) return false; // Valida se hora, minuto e segundo estão dentro dos intervalos válidos.
  return true; // Retorna true se a análise for bem-sucedida e os valores forem válidos.
}
