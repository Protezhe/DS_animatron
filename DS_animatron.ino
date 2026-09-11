// D9 Серва в телефоне
// DF Player (1) D7 - bysy и D6 IO 1
// DF Player (2) D5 - bysy и D4 IO 1
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

const byte RELAY_POSTBOX     = 2;
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
// DF PLAYER 1
// ============================
//
// MP3-TF-16P / DFPlayer Mini:
// IO1 обычно срабатывает коротким замыканием на GND.

const byte PLAYER1_IO1 = 5;
const byte PLAYER1_BUSY = 7;
const unsigned int PLAYER_TRIGGER_PULSE = 200;


// ============================
// СЕРВА ТЕЛЕФОНА
// ============================

const byte PHONE_SERVO_PIN = 9;
const byte PHONE_SERVO_START = 0;
const byte PHONE_SERVO_FORWARD = 180;
const unsigned long PHONE_SERVO_AUTO_RESET_INTERVAL = 120000;
const byte PHONE_SERVO_NOTE_COUNT = 8;

const unsigned long PHONE_SERVO_NOTE_STARTS[PHONE_SERVO_NOTE_COUNT] = {
  5000, 6455, 7559, 8718, 9938, 11125, 12330, 13458
};

const byte PHONE_SERVO_NOTE_TARGETS[PHONE_SERVO_NOTE_COUNT] = {
  180, 0,
  51, 0,
  132, 0,
  109, 0
};

const unsigned int PHONE_SERVO_MOVE_DURATIONS[PHONE_SERVO_NOTE_COUNT] = {
  900, 635,
  255, 179,
  660, 465,
  545, 384
};

const unsigned int PHONE_SERVO_HOLD_DURATIONS[PHONE_SERVO_NOTE_COUNT] = {
  0, 0,
  4, 80,
  2, 207,
  0, 292
};

Servo phoneServo;
byte phoneServoCurrentAngle = PHONE_SERVO_START;
unsigned long phoneServoResetStarted = 0;
bool phoneServoResetPending = false;


// ============================

char packetBuffer[80];

void setup() {
  wdt_disable();
  delay(100);

  Serial.begin(9600);

  pinMode(RELAY_POSTBOX, OUTPUT);
  pinMode(RELAY_ENERGYMETER, OUTPUT);
  pinMode(RELAY_LIFT_PANEL, OUTPUT);
  pinMode(RELAY_BALL, OUTPUT);

  pinMode(PLAYER1_IO1, OUTPUT);
  digitalWrite(PLAYER1_IO1, HIGH);
  pinMode(PLAYER1_BUSY, INPUT_PULLUP);

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


  // -------- PHONE / DF PLAYER 1 --------

  else if (strcmp(command, "/phone,1") == 0) {
    unsigned long phoneAudioStarted = millis();
    playPhoneAudio();
    playPhoneServoMidiMotion(phoneAudioStarted);
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


void playPhoneAudio() {
  if (digitalRead(PLAYER1_BUSY) == LOW) {
    Serial.println("Phone audio BUSY LOW");
  }
  else {
    Serial.println("Phone audio BUSY HIGH");
  }

  Serial.println("Phone audio play");

  digitalWrite(PLAYER1_IO1, LOW);
  delay(PLAYER_TRIGGER_PULSE);
  digitalWrite(PLAYER1_IO1, HIGH);
}


void playPhoneServoMidiMotion(unsigned long startedAt) {
  Serial.println("Phone servo MIDI motion");

  for (byte i = 0; i < PHONE_SERVO_NOTE_COUNT; i += 1) {
    waitUntilFrom(startedAt, PHONE_SERVO_NOTE_STARTS[i]);
    movePhoneServoTimed(PHONE_SERVO_NOTE_TARGETS[i], PHONE_SERVO_MOVE_DURATIONS[i]);
    delayWithWatchdog(PHONE_SERVO_HOLD_DURATIONS[i]);
  }

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


void movePhoneServoTimed(byte targetAngle, unsigned int durationMs) {
  int fromAngle = phoneServoCurrentAngle;
  unsigned long startedAt = millis();

  while (millis() - startedAt < durationMs) {
    unsigned long elapsed = millis() - startedAt;
    int nextAngle = fromAngle + (long)(targetAngle - fromAngle) * elapsed / durationMs;

    phoneServo.write(nextAngle);
    phoneServoCurrentAngle = nextAngle;
    wdt_reset();
    delay(5);
  }

  phoneServo.write(targetAngle);
  phoneServoCurrentAngle = targetAngle;
}


void checkPhoneServoAutoReset() {
  if (!phoneServoResetPending) {
    return;
  }

  if (millis() - phoneServoResetStarted < PHONE_SERVO_AUTO_RESET_INTERVAL) {
    return;
  }

  Serial.println("Phone servo auto reset");

  movePhoneServoTimed(PHONE_SERVO_START, 1000);
  phoneServoResetPending = false;
}
