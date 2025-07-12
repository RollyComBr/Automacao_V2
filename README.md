# 📟 Sistema de Controle de Relés com RTC, Alarmes e Bluetooth

Este projeto em Arduino permite o controle de até **4 relés** com **dois alarmes independentes por relé**, com base no **RTC DS1307** e com suporte a **comunicação via Bluetooth (HC-05)** e **Serial**.

---

## 🚀 Funcionalidades

- ✅ Controle de 4 relés de forma automática (via alarme) ou manual.
- 🕒 Agendamento de horários para ligar/desligar os relés por dias da semana.
- 📅 Ajuste de data/hora via comando.
- 🔄 Alternância entre modo **Automático (A)** e **Manual (M)** para cada relé.
- 🔁 Armazenamento dos alarmes e modos na EEPROM.
- 📲 Comunicação via Serial e HC-05 Bluetooth.
- 🔧 Comandos simples para configuração via terminal.

---

## ⚙️ Hardware Necessário

- Arduino (UNO, Nano ou compatível)
- Módulo RTC DS1307
- Módulo Relé (até 4 canais)
- Módulo Bluetooth HC-05
- Registrador de deslocamento (74HC595)
- Jumpers e Protoboard

---

## 🛠️ Pinagem (padrão)

| Componente     | Pino Arduino |
|----------------|--------------|
| DATA_PIN       | 2            |
| LATCH_PIN      | 3            |
| CLOCK_PIN      | 4            |
| Bluetooth TX   | 1 (RX do Arduino) |
| Bluetooth RX   | 0 (TX do Arduino) |
| RTC SDA        | A4           |
| RTC SCL        | A5           |

> ⚠️ O Bluetooth está usando `SoftwareSerial` nas portas 0 e 1 (ajuste conforme necessário).

---

## 💻 Comandos Disponíveis

### ✅ Status e Relógio

- `STATUS`  
  Exibe o status atual de todos os relés, incluindo o modo (M/A) e estado (ON/OFF).

- `TIME`  
  Mostra a data e hora atuais do RTC.

- `SETTIME DD/MM/AAAA HH:MM:SS`  
  Ajusta a data e hora do RTC.  
  Exemplo: `SETTIME 12/07/2025 14:45:00`

---

### 🕓 Alarmes

- `SET <rele> <slot> <dias> <horaOn> <horaOff>`  
  Define um alarme para um relé em um dos dois slots disponíveis.
  - `rele`: 0 a 3 (relé alvo)
  - `slot`: 0 ou 1 (slot de alarme)
  - `dias`: máscara de bits com os dias da semana (ex: `62` = segunda a sábado)
  - `horaOn` / `horaOff`: formato `HH:MM`  
  Exemplo: `SET 1 0 62 08:00 18:00`

- `LIST`  
  Lista todos os alarmes configurados nos 4 relés.

- `RESET`  
  Reseta todos os alarmes para configuração padrão.

---

### 🔄 Modo de Operação

- `AUTO <0|1> <rele>`  
  Altera o modo de operação de um relé:
  - `AUTO 1 2` → Ativa o modo automático no relé 2
  - `AUTO 0 0` → Coloca o relé 0 em modo manual

- `ON <rele>`  
  Liga manualmente o relé especificado (modo manual deve estar ativado).

- `OFF <rele>`  
  Desliga manualmente o relé especificado (modo manual deve estar ativado).

---

### ℹ️ Ajuda

- `HELP`  
  Exibe a lista de comandos disponíveis com instruções breves.

---

## 🧠 Estrutura da EEPROM

- Cada `AlarmeSlot` ocupa 7 bytes (dias + horários).
- São 2 slots por relé → 14 bytes por relé.
- Para 4 relés → 56 bytes usados para alarmes.
- Modos dos relés (M/A) são salvos nos últimos 4 bytes → total 60 bytes usados.

---

## 📦 Estrutura de Código

- `AlarmeSlot` — estrutura com dias da semana, hora ON/OFF.
- `TimerAlarme` — possui dois `AlarmeSlot` por relé e lógica de ativação.
- `modoAutomatico[]` — vetor que guarda o modo atual de cada relé (salvo na EEPROM).
- `statusRele[]` — estado ON/OFF manual de cada relé.
- `estadoRele` — byte que representa o estado de todos os relés no registrador 74HC595.

---

## 🧪 Exemplo de Uso

```plaintext
> SETTIME 12/07/2025 14:45:00
RTC ajustado.

> SET 0 0 62 08:00 18:00
Alarme atualizado.

> AUTO 1 0
Relé 0 definido para modo automático.

> STATUS
{"relays":[
  {"id":0,"Modo":"A","status":"OFF"},
  {"id":1,"Modo":"M","status":"ON"},
  {"id":2,"Modo":"A","status":"OFF"},
  {"id":3,"Modo":"M","status":"OFF"}
]}
