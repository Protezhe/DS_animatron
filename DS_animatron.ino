// D9 Серва в телефоне
// DF Player (1) D7 - bysy и D6 IO 1
// DF Player (2) D5 - bysy и D4 IO 1
// D8 неизвестно в стену
// RELAY_POSTBOX     = D2 D3;


#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

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
const byte RELAY_LIFT_PANEL  = 1;
const byte RELAY_BALL        = A2;
const byte RELAY_BREAKER     = 7;


// Большинство китайских релейных модулей включаются LOW.
// Если у тебя реле включается от HIGH,
// поменяй эти две строки местами.

const byte RELAY_ON  = LOW;
const byte RELAY_OFF = HIGH;


// ============================

char packetBuffer[80];

void setup() {
  Serial.begin(9600);

  pinMode(RELAY_POSTBOX, OUTPUT);
  pinMode(RELAY_ENERGYMETER, OUTPUT);
  pinMode(RELAY_LIFT_PANEL, OUTPUT);
  pinMode(RELAY_BALL, OUTPUT);
  pinMode(RELAY_BREAKER, OUTPUT);

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
}


void loop() {

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

    return;
  }

  if (link == LinkON && !udpStarted) {
    Serial.println("Link restored");

    allRelaysOff();

    if (Udp.begin(localPort)) {
      udpStarted = true;
      Serial.println("UDP started");
    }
    else {
      Serial.println("ERROR: UDP start failed");
    }
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

  else if (strcmp(command, "/breaker,1") == 0) {
    relayOn(RELAY_BREAKER);
  }

  else if (strcmp(command, "/breaker,0") == 0) {
    relayOff(RELAY_BREAKER);
  }


  // Телефон пока ничего не делает

  else if (
    strcmp(command, "/phone,1") == 0 ||
    strcmp(command, "/phone,0") == 0
  ) {
    Serial.println("Phone command ignored");
  }


  else {
    Serial.println("Unknown command");
  }
}


// ============================

void relayOn(byte pin) {
  digitalWrite(pin, RELAY_ON);

  Serial.print("Relay ");
  Serial.print(pin);
  Serial.println(" ON");
}


void relayOff(byte pin) {
  digitalWrite(pin, RELAY_OFF);

  Serial.print("Relay ");
  Serial.print(pin);
  Serial.println(" OFF");
}


void allRelaysOff() {
  digitalWrite(RELAY_POSTBOX, RELAY_OFF);
  digitalWrite(RELAY_ENERGYMETER, RELAY_OFF);
  digitalWrite(RELAY_LIFT_PANEL, RELAY_OFF);
  digitalWrite(RELAY_BALL, RELAY_OFF);
  digitalWrite(RELAY_BREAKER, RELAY_OFF);

  Serial.println("ALL RELAYS OFF");
}
