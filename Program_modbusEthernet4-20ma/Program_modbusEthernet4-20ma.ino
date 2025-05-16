#include <SPI.h>
#include <Ethernet2.h>
#include <Modbus.h>
#include <ModbusIP.h>
#include <EEPROM.h>

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 0, 155);

ModbusIP mb;
EthernetServer server(80);

float scaleMin[6], scaleMax[6];
int adcMin[6], adcMax[6];
float lastScaled[6];

void saveConfig() {
  int addr = 0;
  for (int i = 0; i < 6; i++) {
    EEPROM.put(addr, scaleMin[i]); addr += sizeof(float);
    EEPROM.put(addr, scaleMax[i]); addr += sizeof(float);
    EEPROM.put(addr, adcMin[i]); addr += sizeof(int);
    EEPROM.put(addr, adcMax[i]); addr += sizeof(int);
  }
}

void resetEEPROM() {
  for (int i = 0; i < sizeof(scaleMin) + sizeof(scaleMax) + sizeof(adcMin) + sizeof(adcMax); i++) {
    EEPROM.write(i, 0xFF);
  }
  Serial.println("EEPROM has been reset to factory defaults.");
}

void loadConfig() {
  int addr = 0;
  for (int i = 0; i < 6; i++) {
    EEPROM.get(addr, scaleMin[i]); addr += sizeof(float);
    EEPROM.get(addr, scaleMax[i]); addr += sizeof(float);
    EEPROM.get(addr, adcMin[i]); addr += sizeof(int);
    EEPROM.get(addr, adcMax[i]); addr += sizeof(int);

    if (scaleMin[i] < -10000 || scaleMin[i] > 10000) {
      scaleMin[i] = 0; scaleMax[i] = 100;
      adcMin[i] = 205; adcMax[i] = 1023;
    }
  }
}

void setup() {
  Serial.begin(9600);
  Ethernet.begin(mac, ip);
  mb.config(mac, ip);
  server.begin();

  for (int i = 0; i < 10; i++) {
    mb.addCoil(i);
    mb.addHreg(i);
  }

  loadConfig();
  Serial.print("Server IP: ");
  Serial.println(Ethernet.localIP());
}

void loop() {
  mb.task();
  handleWebServer();

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 1000) {
    lastUpdate = millis();
    Serial.println("=== Channel Data Update ===");
    for (int i = 0; i < 6; i++) {
      int raw = analogRead(i);
      float scaled = (raw - adcMin[i]) * (scaleMax[i] - scaleMin[i]) / (adcMax[i] - adcMin[i]) + scaleMin[i];
      lastScaled[i] = scaled;
      mb.Hreg(i + 1, (int)scaled);

      Serial.print("CH "); Serial.print(i + 1);
      Serial.print(" | Raw: "); Serial.print(raw);
      Serial.print(" | Scaled: "); Serial.print(scaled, 2);
      Serial.print(" | ScaleMin: "); Serial.print(scaleMin[i], 2);
      Serial.print(" | ScaleMax: "); Serial.print(scaleMax[i], 2);
      Serial.print(" | ADC Min: "); Serial.print(adcMin[i]);
      Serial.print(" | ADC Max: "); Serial.println(adcMax[i]);
    }
    Serial.println();
  }
}

void handleWebServer() {
  EthernetClient client = server.available();
  if (client) {
    String req = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        req += c;
        if (req.endsWith("\r\n\r\n")) break;
      }
    }

    if (req.indexOf("GET /value") >= 0) {
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/plain");
      client.println("Connection: close\n");
      for (int i = 0; i < 6; i++) {
        client.println(lastScaled[i], 2);
      }
    } else {
      if (req.indexOf("GET /reset") >= 0) {
        resetEEPROM();
        loadConfig();
      }
      else if (req.indexOf("GET /?ch=") >= 0) {
        int ch = req.substring(req.indexOf("ch=") + 3, req.indexOf("&")).toInt() - 1;
        float smin = req.substring(req.indexOf("min=") + 4, req.indexOf("&max=")).toFloat();
        float smax = req.substring(req.indexOf("max=") + 4, req.indexOf("&adcmin=")).toFloat();
        int amin = req.substring(req.indexOf("adcmin=") + 7, req.indexOf("&adcmax=")).toInt();
        int amax = req.substring(req.indexOf("adcmax=") + 7, req.indexOf(" ", req.indexOf("adcmax="))).toInt();

        if (ch >= 0 && ch < 6) {
          scaleMin[ch] = smin;
          scaleMax[ch] = smax;
          adcMin[ch] = amin;
          adcMax[ch] = amax;
          saveConfig();

           // Kirim redirect ke halaman utama
          client.println("HTTP/1.1 302 Found");
          client.println("Location: /");
          client.println("Connection: close");
          client.println();
          client.stop();
          return;
        }
      }

      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/html");
      client.println("Connection: close\n");
      client.println(F("<!DOCTYPE html><html><head><title>Analog Scaling Config</title>"));
      client.println(F("<meta name='viewport' content='width=device-width, initial-scale=1.0'>"));
      client.println(F("<style>body{font-family:Arial;margin:20px;background:#e9ecef;}h1{color:#333;}"));
      client.println(F("form{background:#fff;padding:15px;border-radius:5px;box-shadow:0 0 10px #ccc;width:max-content;}"));
      client.println(F("input,select{padding:8px;margin:5px;}input[type=submit]{background:#007bff;color:white;border:none;border-radius:4px;cursor:pointer;}"));
      client.println(F("input[type=submit]:hover{background:#0056b3;}li{margin-bottom:5px;}button{padding:8px;margin-top:10px;background:#dc3545;color:#fff;border:none;border-radius:4px;cursor:pointer;}button:hover{background:#c82333;}</style>"));
      client.println(F("<script>function updateValue(){fetch('/value').then(res=>res.text()).then(data=>{let lines=data.trim().split('\\n');for(let i=0;i<6;i++){document.getElementById('ch'+(i+1)).innerText=lines[i];}});}setInterval(updateValue,1000);window.onload=updateValue;</script>"));
      client.println(F("</head><body><h1>Configure Scaling Per Channel</h1>"));
      client.println(F("<form method='GET'>"));
      client.println(F("<label>Channel: <select name='ch'>"));
      for (int i = 0; i < 6; i++) {
        client.print(F("<option value='")); client.print(i + 1); client.print(F("'>CH ")); client.print(i + 1); client.println(F("</option>"));
      }
      client.println(F("</select></label><br>"));
      client.println(F("Scale Min: <input name='min' type='number' step='any'><br>"));
      client.println(F("Scale Max: <input name='max' type='number' step='any'><br>"));
      client.println(F("ADC Min: <input name='adcmin' type='number'><br>"));
      client.println(F("ADC Max: <input name='adcmax' type='number'><br>"));
      client.println(F("<input type='submit' value='Save Config'></form>"));

      client.println(F("<form method='GET' action='/reset'><button type='submit'>Reset EEPROM</button></form>"));

      client.println(F("<h2>Realtime Values:</h2><ul>"));
      for (int i = 0; i < 6; i++) {
        client.print(F("<li>CH ")); client.print(i + 1); client.print(F(": <span id='ch")); client.print(i + 1); client.println(F("'>...</span></li>"));
      }
      client.println(F("</ul></body></html>"));
    }

    delay(1);
    client.stop();
  }
}
