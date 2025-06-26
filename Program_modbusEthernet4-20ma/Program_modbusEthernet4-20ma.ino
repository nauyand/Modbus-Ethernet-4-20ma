#include <SPI.h>
#include <Ethernet2.h>
#include <Modbus.h>
#include <ModbusIP.h>
#include <EEPROM.h>
#include <avr/wdt.h>

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress staticIP(192, 168, 0, 155);  // Default IP

ModbusIP mb;
EthernetServer server(80);

float scaleMin[6], scaleMax[6];
int adcMin[6], adcMax[6];
float lastScaled[6];
int modbusAddr[6];  // NEW: Modbus address mapping

void saveConfig() {
  int addr = 0;
  for (int i = 0; i < 6; i++) {
    EEPROM.put(addr, scaleMin[i]); addr += sizeof(float);
    EEPROM.put(addr, scaleMax[i]); addr += sizeof(float);
    EEPROM.put(addr, adcMin[i]);   addr += sizeof(int);
    EEPROM.put(addr, adcMax[i]);   addr += sizeof(int);
  }
  EEPROM.put(addr, staticIP); addr += sizeof(IPAddress);
  for (int i = 0; i < 6; i++) {
    EEPROM.put(addr, modbusAddr[i]); addr += sizeof(int);
  }
}

void resetEEPROM() {
  for (int i = 0; i < EEPROM.length(); i++) {
    EEPROM.write(i, 0xFF);
  }
  Serial.println("EEPROM reset.");
}

void loadConfig() {
  int addr = 0;
  for (int i = 0; i < 6; i++) {
    EEPROM.get(addr, scaleMin[i]); addr += sizeof(float);
    EEPROM.get(addr, scaleMax[i]); addr += sizeof(float);
    EEPROM.get(addr, adcMin[i]);   addr += sizeof(int);
    EEPROM.get(addr, adcMax[i]);   addr += sizeof(int);

    if (scaleMin[i] < -10000 || scaleMin[i] > 10000) {
      scaleMin[i] = 0; scaleMax[i] = 100;
      adcMin[i] = 205; adcMax[i] = 1023;
    }
  }

  EEPROM.get(addr, staticIP); addr += sizeof(IPAddress);
  if (staticIP[0] == 255 || staticIP[0] == 0) {
    staticIP = IPAddress(192, 168, 0, 155);
  }

  for (int i = 0; i < 6; i++) {
    EEPROM.get(addr, modbusAddr[i]); addr += sizeof(int);
    if (modbusAddr[i] < 1 || modbusAddr[i] > 9999) modbusAddr[i] = i + 1;
  }
}

void softwareReset() {
  wdt_enable(WDTO_15MS); // Trigger WDT reset dalam 15ms
  while (1) {}           // Tunggu sampai reset terjadi
}

void setup() {
  Serial.begin(9600);
  loadConfig();
  Ethernet.begin(mac, staticIP);
  mb.config(mac, staticIP);
  server.begin();

  mb.addHreg(0);
  int regsToAdd[6];    // Array untuk simpan address yang akan didaftarkan ke Modbus (maksimal 6 CH)
  int regCount = 0;    // Hitung berapa banyak register unik

  for (int i = 0; i < 6; i++) {
    int addr = modbusAddr[i];  // Modbus pakai address 1-based, sedangkan mb.Hreg() pakai 0-based

    // Skip kalau address negatif (invalid)
    if (addr < 0) continue;

    // Cek apakah addr sudah pernah disimpan
    bool exists = false;
    for (int j = 0; j < regCount; j++) {
      if (regsToAdd[j] == addr) {
        exists = true;
        break;
      }
    }

    // Kalau belum ada dan masih muat, simpan
    if (!exists && regCount < 6) {
      regsToAdd[regCount++] = addr;
    }
  }

  // Tambahkan hanya register yang unik ke Modbus
  for (int i = 0; i < regCount; i++) {
    mb.addHreg(regsToAdd[i]);
    Serial.print("Modbus Register Added: ");
    Serial.println(regsToAdd[i]);
  }

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
      mb.Hreg(modbusAddr[i], (int)scaled);

      Serial.print("CH "); Serial.print(i + 1);
      Serial.print(" | Raw: "); Serial.print(raw);
      Serial.print(" | Scaled: "); Serial.print(scaled, 2);
      Serial.print(" | ScaleMin: "); Serial.print(scaleMin[i], 2);
      Serial.print(" | ScaleMax: "); Serial.print(scaleMax[i], 2);
      Serial.print(" | ADC Min: "); Serial.print(adcMin[i]);
      Serial.print(" | ADC Max: "); Serial.print(adcMax[i]);
      Serial.print(" | Modbus Addr: "); Serial.println(modbusAddr[i] + 1);
    }
    Serial.print(staticIP);
    Serial.println();
  }
}

void handleWebServer() {
  EthernetClient client = server.available();
  if (!client) return;

  String req = "";
  unsigned long timeout = millis();
  while (client.connected() && millis() - timeout < 1000) {
    if (client.available()) {
      char c = client.read();
      req += c;
      if (req.endsWith("\r\n\r\n") || req.length() > 300) break;
    }
  }
  if (req.length() == 0) {
    client.stop(); return;
  }

if (req.indexOf("GET /restart") >= 0) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html\n");
  client.println("<html><body><h2>Restarting Controller...</h2></body></html>");
  client.stop();
  delay(100);  // beri waktu sebelum reset
  softwareReset();
  return;
}

  // IP Config Page
  if (req.indexOf("GET /ipconfig") >= 0) {
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html\n");
    client.println(F("<html><body><h2>Set IP Address</h2><form method='GET' action='/setip'>"));
    client.println(F("IP: <input name='newip' placeholder='192.168.0.200'><br>"));
    client.println(F("<input type='submit' value='Save'></form>"));
    client.print(F("<p>Current IP: ")); client.print(staticIP); client.println(F("</p>"));
    client.println(F("<a href='/'><button>Kembali</button></a></body></html>"));
    client.stop(); 
    return;
  }

  // Save New IP
  if (req.indexOf("GET /setip") >= 0) {
    int idx = req.indexOf("newip=");
    if (idx > 0) {
      String ipStr = req.substring(idx + 6, req.indexOf(" ", idx));
      int ip1, ip2, ip3, ip4;
      if (sscanf(ipStr.c_str(), "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4) == 4) {
        if (ip1 <= 255 && ip2 <= 255 && ip3 <= 255 && ip4 <= 255) {
          staticIP = IPAddress(ip1, ip2, ip3, ip4);
          saveConfig();
          client.println("HTTP/1.1 200 OK\nContent-Type: text/html\n");
          client.println(F("<html><body><h2>IP Address Saved</h2>"));
          client.print(F("<p>New IP: ")); client.print(staticIP); client.println(F("</p>"));
          client.println(F("<p><b>Restart Controller to apply new IP.</b></p>"));
          client.println(F("<a href='/'><button>Kembali</button></a></body></html>"));
          client.stop(); 
          delay(300);  // beri waktu sebelum reset
          softwareReset();
          return;
        }
      }
    }
    client.println("HTTP/1.1 400 Bad Request\nContent-Type: text/html\n");
    client.println(F("<html><body><h2>Invalid IP Format</h2>"));
    client.println(F("<a href='/ipconfig'><button>Kembali</button></a></body></html>"));
    client.stop(); return;
  }

  // Save Modbus Address
if (req.indexOf("GET /setmb") >= 0) {
  for (int i = 0; i < 6; i++) {
    String key = "mb" + String(i) + "=";
    int idx = req.indexOf(key);
    if (idx > 0) {
      int endIdx = req.indexOf("&", idx + key.length());
      if (endIdx == -1) endIdx = req.indexOf(" ", idx + key.length());  // <- FIXED
      int val = req.substring(idx + key.length(), endIdx).toInt();
      if (val >= 0) {
        modbusAddr[i] = val - 1;
      }
    }
  }
  saveConfig();
  client.println("HTTP/1.1 302 Found");
  client.println("Location: /");
  client.println("Connection: close\n");
  client.stop();
  
  delay(100);                 // Delay sebentar biar respon terkirim
  softwareReset();            // Restart otomatis biar config baru aktif
  return;
  }


  if (req.indexOf("GET /value") >= 0) {
    client.println("HTTP/1.1 200 OK\nContent-Type: text/plain\nConnection: close\n");
    for (int i = 0; i < 6; i++) {
      client.println(lastScaled[i], 2);
    }
    client.stop(); return;
  }

  if (req.indexOf("GET /reset") >= 0) {
    resetEEPROM();
    loadConfig();
  } else if (req.indexOf("GET /?ch=") >= 0) {
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
      client.println("HTTP/1.1 302 Found");
      client.println("Location: /");
      client.println("Connection: close\n");
      client.stop();
      return;
    }
  }

  // === Halaman Web Utama ===
  client.println("HTTP/1.1 200 OK\nContent-Type: text/html\nConnection: close\n");
  client.println(F("<html><head><title>Scaling & Modbus Config</title>"));
  client.println(F("<style>body{font-family:Arial;}form,input,select,button{margin:5px;padding:8px;}</style>"));
  client.println(F("<script>function updateValue(){fetch('/value').then(r=>r.text()).then(d=>{let l=d.trim().split('\\n');for(let i=0;i<6;i++){document.getElementById('ch'+(i+1)).innerText=l[i];}});}setInterval(updateValue,1000);window.onload=updateValue;</script>"));
  client.println(F("</head><body><h2>Scaling Config</h2><form method='GET'>"));
  client.println(F("Channel: <select name='ch'>"));
  for (int i = 0; i < 6; i++) {
    client.print("<option value='"); client.print(i + 1); client.print("'>CH "); client.print(i + 1); client.println("</option>");
  }
  client.println(F("</select><br>Scale Min: <input name='min' type='number' step='any'><br>Scale Max: <input name='max' type='number' step='any'><br>ADC Min: <input name='adcmin' type='number'><br>ADC Max: <input name='adcmax' type='number'><br><input type='submit' value='Save'></form>"));
  client.println(F("<form method='GET' action='/reset'><button style='background:#dc3545;color:#fff;'>Reset EEPROM</button></form>"));
  client.println(F("<a href='/ipconfig'><button>IP Config</button></a>"));

  client.println(F("<h3>Modbus Address Setting</h3><form method='GET' action='/setmb'>"));
  for (int i = 0; i < 6; i++) {
    client.print("CH "); client.print(i + 1); client.print(": <input name='mb");
    client.print(i); client.print("' value='"); client.print(modbusAddr[i]+1);
    client.println("' type='number' min='0' max='9999'><br>");
  }
  client.println(F("<input type='submit' value='Save Modbus Address'></form><hr>"));

  client.println(F("<h3>Realtime Values</h3><ul>"));
  for (int i = 0; i < 6; i++) {
    client.print("<li>CH "); client.print(i + 1); client.print(": <span id='ch"); client.print(i + 1); client.println("'>...</span></li>");
  }
  client.println(F("</ul></body></html>"));
  client.println(F("<form method='GET' action='/restart'><button style='background:#ffc107;'>Restart Controller</button></form>"));

  client.stop();
}
