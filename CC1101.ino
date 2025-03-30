#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <RCSwitch.h>

// Wi‑Fi credentials
const char* ssid = "";
const char* password = "";

// RF pins definitions
#define CC1101_SCK 18   // SPI clock
#define CC1101_MISO 19  // SPI data (MISO)
#define CC1101_MOSI 23  // SPI data (MOSI)
#define CC1101_CS 5     // Chip select (CS)
#define CC1101_GDO0 2   // RF receive interrupt pin
#define CC1101_GDO2 4   // Optional secondary interrupt pin
#define RF_TX_PIN 14    // RF transmit pin

RCSwitch mySwitch = RCSwitch();

// System states
enum State {
  LISTEN,
  WAIT_FOR_KEY
};

State currentState = LISTEN;
volatile int lastRFValue = 0;  // Guarda o último valor RF recebido

// Cria um servidor web na porta 80
WebServer server(80);

/**
 * Converte uma string hexadecimal para uma string binária.
 * Cada dígito hexadecimal gera 4 bits.
 */
String hexToBinary(String hex) {
  String bin = "";
  for (int i = 0; i < hex.length(); i++) {
    char c = hex.charAt(i);
    uint8_t nibble;
    if (c >= '0' && c <= '9') {
      nibble = c - '0';
    } else if (c >= 'A' && c <= 'F') {
      nibble = c - 'A' + 10;
    } else if (c >= 'a' && c <= 'f') {
      nibble = c - 'a' + 10;
    } else {
      continue;  // ignora caracteres inválidos (ex.: espaços)
    }
    // Converte o nibble para uma string binária de 4 bits
    for (int j = 3; j >= 0; j--) {
      bin += ((nibble >> j) & 1) ? "1" : "0";
    }
  }
  return bin;
}

/**
 * Transmite manualmente o código binário usando digitalWrite() e delayMicroseconds().
 * Os tempos dos pulsos devem ser ajustados conforme o protocolo do sinal.
 */
void transmitBinary(String binaryCode) {
  const int pulseHigh = 490;      // microsegundos para pulso HIGH
  const int pulseLow  = 1630;     // microsegundos para pulso LOW
  const int repeatCount = 5;      // número de repetições

  pinMode(RF_TX_PIN, OUTPUT);

  for (int r = 0; r < repeatCount; r++) {
    for (int i = 0; i < binaryCode.length(); i++) {
      if (binaryCode.charAt(i) == '1') {
        digitalWrite(RF_TX_PIN, HIGH);
        delayMicroseconds(pulseHigh);
        digitalWrite(RF_TX_PIN, LOW);
        delayMicroseconds(pulseLow);
      } else {
        digitalWrite(RF_TX_PIN, HIGH);
        delayMicroseconds(pulseLow);
        digitalWrite(RF_TX_PIN, LOW);
        delayMicroseconds(pulseHigh);
      }
    }

    // Pequena pausa entre repetições
    delay(10);
  }

  // Sinal final para garantir "encerramento"
  digitalWrite(RF_TX_PIN, HIGH);
  delayMicroseconds(1000);
  digitalWrite(RF_TX_PIN, LOW);
}


/**
 * Endpoint REST que retorna o último valor RF recebido em JSON.
 */
void handleRF() {
  StaticJsonDocument<200> jsonDoc;
  jsonDoc["rf_value"] = lastRFValue;
  String jsonResponse;
  serializeJson(jsonDoc, jsonResponse);
  server.send(200, "application/json", jsonResponse);
}

/**
 * Endpoint REST para receber comandos via JSON.
 * Se o payload contiver o campo "hex", converte-o para binário, detecta o tamanho e transmite o sinal.
 */
void handleCommand() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"No JSON payload provided\"}");
    return;
  }

  StaticJsonDocument<400> jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, server.arg("plain"));
  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  // Se o payload contém o campo "hex", processa a transmissão do código hexadecimal
  if (jsonDoc.containsKey("hex")) {
    String hexCode = jsonDoc["hex"].as<String>();
    // Remove espaços, se houver
    hexCode.replace(" ", "");
    String binaryCode = hexToBinary(hexCode);
    int bitLength = binaryCode.length();

    if (bitLength == 0) {
      server.send(400, "application/json", "{\"error\":\"Invalid hex string\"}");
      return;
    }

    Serial.print("Transmitting hex code: ");
    Serial.println(hexCode);
    Serial.print("Binary representation: ");
    Serial.println(binaryCode);
    Serial.print("Bit length: ");
    Serial.println(bitLength);

    // Se o código for de 32 bits ou menos, utiliza o método numérico da RCSwitch
    if (bitLength <= 32) {
      unsigned long code = strtoul(binaryCode.c_str(), NULL, 2);
      mySwitch.send(code, bitLength);
      Serial.println("Transmitted using RCSwitch numeric method.");
    } else {
      // Para códigos maiores, utiliza a transmissão manual
      transmitBinary(binaryCode);
      Serial.println("Transmitted using manual binary transmission.");
    }

    StaticJsonDocument<200> res;
    res["status"] = "hex code transmitted";
    res["hex"] = hexCode;
    res["bit_length"] = bitLength;
    String response;
    serializeJson(res, response);
    server.send(200, "application/json", response);
    return;
  }

  // Se não tiver o campo "hex", processa comandos baseados no campo "command"
  String command = jsonDoc["command"] | "";
  if (command == "") {
    server.send(400, "application/json", "{\"error\":\"Command is required\"}");
    return;
  }

  // Processa comandos de acordo com o estado atual
  if (currentState == LISTEN && command.equalsIgnoreCase("listen")) {
    StaticJsonDocument<200> res;
    res["status"] = "listening mode confirmed";
    String response;
    serializeJson(res, response);
    server.send(200, "application/json", response);
  } else if (currentState == LISTEN && command.equalsIgnoreCase("transmit")) {
    currentState = WAIT_FOR_KEY;
    StaticJsonDocument<200> res;
    res["status"] = "transmit mode activated, send key";
    String response;
    serializeJson(res, response);
    server.send(200, "application/json", response);
  } else if (currentState == WAIT_FOR_KEY) {
    long key = jsonDoc["key"] | 0;
    if (key == 0) {
      StaticJsonDocument<200> res;
      res["status"] = "invalid key";
      String response;
      serializeJson(res, response);
      server.send(400, "application/json", response);
    } else {
      // Transmite a chave via RF com 24 bits (ajuste conforme necessário)
      mySwitch.send(key, 24);
      StaticJsonDocument<200> res;
      res["status"] = "key transmitted";
      res["key"] = key;
      String response;
      serializeJson(res, response);
      server.send(200, "application/json", response);
    }
    // Retorna para o modo de escuta após a transmissão
    currentState = LISTEN;
  } else {
    StaticJsonDocument<200> res;
    res["status"] = "invalid command";
    String response;
    serializeJson(res, response);
    server.send(400, "application/json", response);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Initializing RF and REST service...");

  // Configura RF: habilita recepção e transmissão
  mySwitch.enableReceive(digitalPinToInterrupt(CC1101_GDO0));
  mySwitch.enableTransmit(RF_TX_PIN);

  // Conecta ao Wi‑Fi
  Serial.println("Connecting to Wi‑Fi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected! IP Address: ");
  Serial.println(WiFi.localIP());

  // Configura os endpoints REST
  server.on("/rf", HTTP_GET, handleRF);
  server.on("/command", HTTP_POST, handleCommand);
  server.begin();
  Serial.println("REST server started");
}

void loop() {
  server.handleClient();

  // Em modo de escuta, verifica se há sinais RF recebidos
  if (currentState == LISTEN && mySwitch.available()) {
    int value = mySwitch.getReceivedValue();
    if (value != 0) {
      lastRFValue = value;
      Serial.print("RF signal received: ");
      Serial.println(value);
    } else {
      Serial.println("Invalid RF signal received");
    }
    mySwitch.resetAvailable();
  }

  delay(50);  // Pequeno delay para evitar sobrecarga no loop
}
