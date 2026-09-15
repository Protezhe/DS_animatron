// D9 Серва в телефоне
// DF Player (1) D5 IO 1
// DF Player (2) D6 IO 1
// D8 неизвестно в стену
// RELAY_POSTBOX     = D2 D3;


#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <Servo.h>
#include <avr/wdt.h>

// ============================
// СЕТЕВЫЕ НАСТРОЙКИ
// ============================

// MAC можно оставить таким, главное чтобы в сети не было другого такого же
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

// IP Arduino
IPAddress ip(192, 168, 0, 185);

// UDP порт, на который Watchout / программа будет отправлять команды
const unsigned int localPort = 8001;

EthernetUDP Udp;
bool udpStarted = false;

unsigned long lastEthernetCheck = 0;
const unsigned long ETHERNET_CHECK_INTERVAL = 2000;

bool ethernetLinkWasOff = false;
bool allRelaysAreOff = false;

// ============================
// РЕЛЕ
// ============================
//
// На Arduino Uno Ethernet Shield использует:
// D10 - CS Ethernet
// D11 - MOSI
// D12 - MISO
// D13 - SCK
//
// Поэтому их для реле не используем.
//
// D4 также часто занят SD-картой Ethernet Shield.
//

const byte RELAY_POSTBOX     = 3;
const byte RELAY_ENERGYMETER = A0;
const byte RELAY_LIFT_PANEL  = A1;
const byte RELAY_BALL        = A2;


// Большинство китайских релейных модулей включаются LOW.
// Если у тебя реле включается от HIGH,
// поменяй эти две строки местами.

const byte RELAY_ON  = LOW;
const byte RELAY_OFF = HIGH;
const unsigned long RELAY_AUTO_OFF_INTERVAL = 120000;

unsigned long relayPostboxStarted = 0;
unsigned long relayEnergymeterStarted = 0;
unsigned long relayLiftPanelStarted = 0;
unsigned long relayBallStarted = 0;

bool relayPostboxOn = false;
bool relayEnergymeterOn = false;
bool relayLiftPanelOn = false;
bool relayBallOn = false;


// ============================
// DF PLAYERS
// ============================
//
// MP3-TF-16P / DFPlayer Mini:
// IO1 обычно срабатывает коротким замыканием на GND.

const byte PLAYER1_IO1 = 2;
const byte PLAYER2_IO1 = 5;
const unsigned int PLAYER_TRIGGER_PULSE = 200;


// ============================
// СЕРВА ТЕЛЕФОНА
// ============================

const byte PHONE_SERVO_PIN = 9;
const byte PHONE_SERVO_START = 180;
const byte PHONE_SERVO_FORWARD = 0;
const unsigned long PHONE_SERVO_AUTO_RESET_INTERVAL = 120000;
const byte PHONE_SERVO_MOVE_COUNT = 4;
const byte PHONE_SERVO_FORWARD_STEP_DELAY_MS = 4;
const unsigned int PHONE_SERVO_START_DELAY_MS = 5300;

const unsigned int PHONE_SERVO_FORWARD_DURATIONS[PHONE_SERVO_MOVE_COUNT] = {
  900,
  259,
  661,
  545
};

const unsigned int PHONE_SERVO_RETURN_DURATIONS[PHONE_SERVO_MOVE_COUNT] = {
  635,
  259,
  672,
  676
};

const unsigned int PHONE_SERVO_FORWARD_HOLD_DURATIONS[PHONE_SERVO_MOVE_COUNT] = {
  555,
  899,
  526,
  583
};

const unsigned int PHONE_SERVO_RETURN_HOLD_DURATIONS[PHONE_SERVO_MOVE_COUNT] = {
  469,
  960,
  533,
  5866
};

Servo phoneServo;
byte phoneServoCurrentAngle = PHONE_SERVO_START;
unsigned long phoneServoResetStarted = 0;
bool phoneServoResetPending = false;


// ============================

char packetBuffer[80];
char serialBuffer[80];
byte serialBufferLength = 0;

void setup() {
  wdt_disable();
  delay(100);

  Serial.begin(9600);

  pinMode(RELAY_POSTBOX, OUTPUT);
  pinMode(RELAY_ENERGYMETER, OUTPUT);
  pinMode(RELAY_LIFT_PANEL, OUTPUT);
  pinMode(RELAY_BALL, OUTPUT);

  pinMode(PLAYER1_IO1, OUTPUT);
  releasePlayerTrigger(PLAYER1_IO1);
  pinMode(PLAYER2_IO1, OUTPUT);
  releasePlayerTrigger(PLAYER2_IO1);

  phoneServo.attach(PHONE_SERVO_PIN);
  phoneServo.write(PHONE_SERVO_START);
  phoneServoCurrentAngle = PHONE_SERVO_START;

  // При включении Arduino всё выключаем
  allRelaysOff();

  Ethernet.init(10);
  Ethernet.begin(mac, ip);

  delay(500);

  if (Ethernet.hardwareStatus() != EthernetNoHardware &&
      Ethernet.linkStatus() == LinkON) {
    if (Udp.begin(localPort)) {
      udpStarted = true;
    }
  }

  Serial.println("Animatron controller started");
  Serial.print("IP: ");
  Serial.println(Ethernet.localIP());

  Serial.print("UDP port: ");
  Serial.println(localPort);

  wdt_enable(WDTO_4S);
}


void loop() {
  wdt_reset();

  checkRelayAutoOff();
  checkPhoneServoAutoReset();
  readSerialCommands();
  checkEthernet();

  if (!udpStarted) {
    return;
  }

  int packetSize = Udp.parsePacket();

  if (packetSize > 0) {

    int len = Udp.read(packetBuffer, sizeof(packetBuffer) - 1);

    if (len > 0) {
      packetBuffer[len] = '\0';
    }

    Serial.print("Received: ");
    Serial.println(packetBuffer);

    processCommand(packetBuffer);
  }
}


// ============================
// ОТЛАДОЧНЫЕ КОМАНДЫ ЧЕРЕЗ SERIAL
// ============================

void readSerialCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\n' || c == '\r') {
      if (serialBufferLength > 0) {
        serialBuffer[serialBufferLength] = '\0';
        processSerialCommand(serialBuffer);
        serialBufferLength = 0;
      }

      continue;
    }

    if (serialBufferLength < sizeof(serialBuffer) - 1) {
      serialBuffer[serialBufferLength] = c;
      serialBufferLength += 1;
    }
  }
}


void processSerialCommand(char* command) {
  trimCommand(command);

  if (command[0] == '\0') {
    return;
  }

  Serial.print("Serial command: ");
  Serial.println(command);

  if (command[0] == '/') {
    processCommand(command);
    return;
  }

  char normalizedCommand[80];

  if (strchr(command, ',') == NULL) {
    snprintf(normalizedCommand, sizeof(normalizedCommand), "/%s,1", command);
  }
  else {
    snprintf(normalizedCommand, sizeof(normalizedCommand), "/%s", command);
  }

  processCommand(normalizedCommand);
}


void trimCommand(char* command) {
  byte start = 0;

  while (command[start] == ' ' || command[start] == '\t') {
    start += 1;
  }

  if (start > 0) {
    byte i = 0;

    do {
      command[i] = command[start + i];
      i += 1;
    } while (command[i - 1] != '\0');
  }

  int end = strlen(command) - 1;

  while (end >= 0 && (command[end] == ' ' || command[end] == '\t')) {
    command[end] = '\0';
    end -= 1;
  }
}


// ============================
// КОНТРОЛЬ ETHERNET / UDP
// ============================

void checkEthernet() {

  if (millis() - lastEthernetCheck < ETHERNET_CHECK_INTERVAL) {
    return;
  }

  lastEthernetCheck = millis();

  EthernetHardwareStatus hw = Ethernet.hardwareStatus();
  EthernetLinkStatus link = Ethernet.linkStatus();

  if (hw == EthernetNoHardware) {
    Serial.println("ERROR: W5500 not found");

    allRelaysOff();
    restartEthernet();

    return;
  }

  if (link == LinkOFF) {
    Serial.println("ERROR: Ethernet link OFF");

    allRelaysOff();

    if (udpStarted) {
      Udp.stop();
      udpStarted = false;
    }

    ethernetLinkWasOff = true;

    return;
  }

  if (link == LinkON && ethernetLinkWasOff) {
    Serial.println("Link restored");

    ethernetLinkWasOff = false;
    restartEthernet();

    return;
  }

  if (link == LinkON && !udpStarted) {
    Serial.println("UDP not started, restart Ethernet");
    restartEthernet();

    return;
  }
}


void restartEthernet() {
  Serial.println("Restart Ethernet...");

  allRelaysOff();

  Udp.stop();
  udpStarted = false;

  Ethernet.init(10);
  Ethernet.begin(mac, ip);

  delay(100);

  if (Ethernet.hardwareStatus() != EthernetNoHardware &&
      Ethernet.linkStatus() == LinkON) {
    if (Udp.begin(localPort)) {
      udpStarted = true;

      Serial.println("UDP restarted");
      Serial.print("IP: ");
      Serial.println(Ethernet.localIP());
    }
  }
}


// ============================
// ОБРАБОТКА КОМАНД
// ============================

void processCommand(char* command) {

  // -------- POSTBOX --------

  if (strcmp(command, "/postbox,1") == 0) {
    relayOn(RELAY_POSTBOX);
  }

  else if (strcmp(command, "/postbox,0") == 0) {
    relayOff(RELAY_POSTBOX);
  }


  // -------- ENERGY METER --------

  else if (strcmp(command, "/energymeter_flash,1") == 0) {
    relayOn(RELAY_ENERGYMETER);
  }

  else if (strcmp(command, "/energymeter_flash,0") == 0) {
    relayOff(RELAY_ENERGYMETER);
  }


  // -------- LIFT PANEL --------

  else if (strcmp(command, "/lift_panel,1") == 0) {
    relayOn(RELAY_LIFT_PANEL);
  }

  else if (strcmp(command, "/lift_panel,0") == 0) {
    relayOff(RELAY_LIFT_PANEL);
  }


  // -------- BALL --------

  else if (strcmp(command, "/ball,1") == 0) {
    relayOn(RELAY_BALL);
  }

  else if (strcmp(command, "/ball,0") == 0) {
    relayOff(RELAY_BALL);
  }


  // -------- BREAKER --------

  else if (
    strcmp(command, "/breaker,1") == 0 ||
    strcmp(command, "/breaker,0") == 0
  ) {
    Serial.println("Breaker command ignored");
  }


  // -------- PHONE / DF PLAYERS --------

  else if (strcmp(command, "/phone,1") == 0) {
    playPhoneSequence();
  }

  else if (strcmp(command, "/phone,0") == 0) {
    Serial.println("Phone audio command OFF ignored");
  }


  else {
    Serial.println("Unknown command");
  }
}


// ============================

void relayOn(byte pin) {
  digitalWrite(pin, RELAY_ON);
  allRelaysAreOff = false;
  rememberRelayOn(pin);

  Serial.print("Relay ");
  Serial.print(pin);
  Serial.println(" ON");
}


void relayOff(byte pin) {
  digitalWrite(pin, RELAY_OFF);
  rememberRelayOff(pin);

  Serial.print("Relay ");
  Serial.print(pin);
  Serial.println(" OFF");
}


void allRelaysOff() {
  digitalWrite(RELAY_POSTBOX, RELAY_OFF);
  digitalWrite(RELAY_ENERGYMETER, RELAY_OFF);
  digitalWrite(RELAY_LIFT_PANEL, RELAY_OFF);
  digitalWrite(RELAY_BALL, RELAY_OFF);

  if (!allRelaysAreOff) {
    Serial.println("ALL RELAYS OFF");
  }

  relayPostboxOn = false;
  relayEnergymeterOn = false;
  relayLiftPanelOn = false;
  relayBallOn = false;

  allRelaysAreOff = true;
}


void rememberRelayOn(byte pin) {
  unsigned long now = millis();

  if (pin == RELAY_POSTBOX) {
    relayPostboxStarted = now;
    relayPostboxOn = true;
  }
  else if (pin == RELAY_ENERGYMETER) {
    relayEnergymeterStarted = now;
    relayEnergymeterOn = true;
  }
  else if (pin == RELAY_LIFT_PANEL) {
    relayLiftPanelStarted = now;
    relayLiftPanelOn = true;
  }
  else if (pin == RELAY_BALL) {
    relayBallStarted = now;
    relayBallOn = true;
  }
}


void rememberRelayOff(byte pin) {
  if (pin == RELAY_POSTBOX) {
    relayPostboxOn = false;
  }
  else if (pin == RELAY_ENERGYMETER) {
    relayEnergymeterOn = false;
  }
  else if (pin == RELAY_LIFT_PANEL) {
    relayLiftPanelOn = false;
  }
  else if (pin == RELAY_BALL) {
    relayBallOn = false;
  }
}


void checkRelayAutoOff() {
  unsigned long now = millis();

  if (relayPostboxOn && now - relayPostboxStarted >= RELAY_AUTO_OFF_INTERVAL) {
    Serial.println("Auto OFF: postbox");
    relayOff(RELAY_POSTBOX);
  }

  if (relayEnergymeterOn && now - relayEnergymeterStarted >= RELAY_AUTO_OFF_INTERVAL) {
    Serial.println("Auto OFF: energymeter");
    relayOff(RELAY_ENERGYMETER);
  }

  if (relayLiftPanelOn && now - relayLiftPanelStarted >= RELAY_AUTO_OFF_INTERVAL) {
    Serial.println("Auto OFF: lift_panel");
    relayOff(RELAY_LIFT_PANEL);
  }

  if (relayBallOn && now - relayBallStarted >= RELAY_AUTO_OFF_INTERVAL) {
    Serial.println("Auto OFF: ball");
    relayOff(RELAY_BALL);
  }
}


void playPhoneSequence() {
  unsigned long phoneStarted = millis();

  triggerPlayer(PLAYER1_IO1, "Phone player 1 play");
  playPhoneServoSequence(phoneStarted);
  triggerPlayer(PLAYER2_IO1, "Phone player 2 play");
}


void triggerPlayer(byte pin, const char* message) {
  Serial.println(message);

  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delay(PLAYER_TRIGGER_PULSE);
  releasePlayerTrigger(pin);
}


void releasePlayerTrigger(byte pin) {
  pinMode(pin, INPUT_PULLUP);
}


void playPhoneServoSequence(unsigned long startedAt) {
  Serial.println("Phone servo sequence");

  waitUntilFrom(startedAt, PHONE_SERVO_START_DELAY_MS);

  for (byte i = 0; i < PHONE_SERVO_MOVE_COUNT; i += 1) {
    movePhoneServoForwardFor(PHONE_SERVO_FORWARD_DURATIONS[i]);

    if (PHONE_SERVO_FORWARD_HOLD_DURATIONS[i] > 0) {
      delayWithWatchdog(PHONE_SERVO_FORWARD_HOLD_DURATIONS[i]);
    }

    movePhoneServoToStartTimed(PHONE_SERVO_RETURN_DURATIONS[i]);

    if (PHONE_SERVO_RETURN_HOLD_DURATIONS[i] > 0) {
      delayWithWatchdog(PHONE_SERVO_RETURN_HOLD_DURATIONS[i]);
    }
  }

  Serial.println("Phone servo sequence done");

  phoneServoResetStarted = millis();
  phoneServoResetPending = true;
}


void waitUntilFrom(unsigned long startedAt, unsigned long targetOffset) {
  while (millis() - startedAt < targetOffset) {
    wdt_reset();
    delay(1);
  }
}


void delayWithWatchdog(unsigned int durationMs) {
  unsigned long startedAt = millis();

  while (millis() - startedAt < durationMs) {
    wdt_reset();
    delay(1);
  }
}


void movePhoneServoForwardFor(unsigned int durationMs) {
  unsigned long startedAt = millis();

  while (millis() - startedAt < durationMs &&
         phoneServoCurrentAngle > PHONE_SERVO_FORWARD) {
    phoneServoCurrentAngle -= 1;
    phoneServo.write(phoneServoCurrentAngle);

    wdt_reset();
    delay(PHONE_SERVO_FORWARD_STEP_DELAY_MS);
  }
}


void movePhoneServoToStartTimed(unsigned int durationMs) {
  int fromAngle = phoneServoCurrentAngle;

  if (fromAngle >= PHONE_SERVO_START) {
    phoneServo.write(PHONE_SERVO_START);
    phoneServoCurrentAngle = PHONE_SERVO_START;
    return;
  }

  int distance = PHONE_SERVO_START - fromAngle;
  unsigned int stepDelay = durationMs / distance;
  unsigned int extraDelay = durationMs % distance;

  for (int pos = fromAngle; pos <= PHONE_SERVO_START; pos += 1) {
    phoneServo.write(pos);
    phoneServoCurrentAngle = pos;

    wdt_reset();

    if (pos < PHONE_SERVO_START) {
      delay(stepDelay);

      if (extraDelay > 0) {
        delay(1);
        extraDelay -= 1;
      }
    }
  }
}


void checkPhoneServoAutoReset() {
  if (!phoneServoResetPending) {
    return;
  }

  if (millis() - phoneServoResetStarted < PHONE_SERVO_AUTO_RESET_INTERVAL) {
    return;
  }

  Serial.println("Phone servo auto reset");

  movePhoneServoToStartTimed(1000);
  phoneServoResetPending = false;
}
