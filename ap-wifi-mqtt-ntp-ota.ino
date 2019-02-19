#include <WiFi.h>//or ESP8266WiFi.h
#include <WiFiUdp.h>
#include <WiFiClient.h>
#include <EEPROM.h>
#include <SSD1306.h>//or SH1106.h
#include <Ethernet.h>
#include <PubSubClient.h> // PubSubClient by Nick O'Leary
#include <stdlib.h>
IPAddress mystaticip(192, 168, 1, 120);
IPAddress mydns(8, 8, 8, 8);
IPAddress mygateway(192, 168, 1, 1);
IPAddress mysubnet(255, 255, 255, 0);

IPAddress timeServer(129, 6, 15, 28);
const char* ntpServerName = "time.nist.gov";
unsigned int localPort = 2390;
WiFiUDP udp;

const int ARR_VARS_SZ = 6; // Nr of settings in eeprom
unsigned int lengths[ARR_VARS_SZ] = {0};
char vars[ARR_VARS_SZ][33];//0=wifi ssid,1=wifi pass,2=mqtt user,3=mqtt pass,4=mqtt domain,5=mqtt port
//A-447907a4ae30
const char* WIFI_PASSWORD = "Passw0rt";
//192.168.4.1
WiFiServer espServer(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned int modeWifi = 0;
SSD1306 display(0x3c, 5, 4);

const byte numChars = 255;
char receivedChars[numChars]; // an array to store the received data
char ChipId[15] = {0};
boolean newData = false;

bool testWifi() {
    int i = 0;
    int j = 0;
    int offset[ARR_VARS_SZ] = {0};
    //global lengths

    // Read all lengths from EEPROM into memory
    for (j = 0; j < ARR_VARS_SZ; j++) {
      lengths[j] = EEPROM.read(j);
      offset[j] = 0; for (i = 0; i < j; i++) { if (i < j) { offset[j] += lengths[i]; } }
    }
  
    // Read all vars from EEPROM into memory
    for (j = 0; j < ARR_VARS_SZ; j++) {
      if (lengths[j] == 0 || lengths[j] > 32) { lengths[j] = 32; }
      for (i = 0; i <= lengths[j]; i++) { vars[j][i] = EEPROM.read(i + ARR_VARS_SZ + offset[j]); } vars[j][lengths[j]] = '\0';
    }
  
    display.drawString(0, 0, "v0.2");
    display.display();
    //WiFi.encryptionType(WIFI_AUTH_WPA2_PSK);
    //WiFi.config(mystaticip, mydns, mygateway, mysubnet);
    WiFi.begin(vars[0], vars[1]);
    for (i = 0; i < 18; i++) {
      if (i == 0) {
        display.drawString(0, 10, "Ssid: " + String(vars[0])); display.display();
      }
      else if (i == 1) {
        display.drawString(0, 20, "Pswd: ********"); display.display();
      }
      else if (WiFi.status() == WL_CONNECTED) {
        display.drawString(0, 40, "OK " + WiFi.localIP().toString()); display.display();
        return true;
      }
      else {
        display.drawString((i*8)-16, 30, "."); display.display();
      }
      delay(1200);
    }
    display.drawString(0, 40, "Failed"); display.display();
    return false;
  }

//#include <ArduinoOTA.h>
//#include "helpers/ota.c"
//platformio device monitor (after editing platformio.ini)

//PROGMEM const int l=D4,r=D3,f=D2,b=D1,h=D0;

//Function is called when, a message is received in the MQTT server.
void callback(char* topic, byte* payload, unsigned int length) {
  String receivedLine = "";
  for (int i=0;i<length;i++) {
    char c = (char)payload[i];
    receivedLine += c;
  }
  display.drawString(0, 30, String(topic) + "> " + receivedLine);
}

void setup() {
  Serial.begin(115200);
  // https://github.com/esp8266/Arduino/issues/1809
  //pinMode(f, OUTPUT); pinMode(b, OUTPUT); pinMode(l, OUTPUT); pinMode(r, OUTPUT); pinMode(h, OUTPUT);
  //digitalWrite(f, LOW); digitalWrite(b, LOW); digitalWrite(l, LOW); digitalWrite(r, LOW); digitalWrite(h, LOW);
  delay(10);
  EEPROM.begin(512);
  delay(10);
  WiFi.persistent(false);
  uint64_t cid;
  cid = ESP.getEfuseMac();
  sprintf(ChipId, "A-%04x%08x", (uint16_t)(cid>>32), (uint32_t)cid);

  // Initialising the UI will init the display too.
  display.init(); display.flipScreenVertically(); display.setFont(ArialMT_Plain_10);
  
  delay(10);
  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  if (testWifi()) {
    //setupOTA();
    mystaticip = WiFi.localIP();
    mygateway = WiFi.gatewayIP();
    mysubnet = WiFi.subnetMask();
    udp.begin(localPort);
    modeWifi = 0;
    //ArduinoOTA.begin();
  }
  else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ChipId, WIFI_PASSWORD);
    espServer.begin();
    modeWifi = 1;
  }
  display.clear();
}

void recvWithEndMarker() {
  static byte ndx = 0;
  char rc;
  while (Serial.available() > 0 && newData == false) {
    delay(1); // delay to allow byte to arrive in input buffer
    rc = Serial.read();
    if (rc != '\n' && rc != '\r') {
      receivedChars[ndx] = rc;
      ndx++;
      if (ndx >= numChars) { ndx = numChars - 1; }
    }
    else {
      receivedChars[ndx] = '\0'; // terminate the string
      ndx = 0;
      newData = true;
    }
  }
}

void parseNewData() {
  int i = 0;
  int j = 0;
  int offset[ARR_VARS_SZ] = {0};

  if (newData == true) {
    String first2 = String(receivedChars);
    if (first2.length() > 2) { first2.remove(2); }

    if (first2.equals("rd")) {
      int offset[ARR_VARS_SZ] = {0};
      // Read all the lengths
      for (j = 0; j < ARR_VARS_SZ; j++) {
        lengths[j] = EEPROM.read(j);
        offset[j] = 0; for (i = 0; i < j; i++) { if (i < j) { offset[j] += lengths[i]; } }
      }
      // Read all the strings
      for (j = 0; j < ARR_VARS_SZ; j++) {
        if (lengths[j] == 0 || lengths[j] > 32) { lengths[j] = 32; }
        for (i = 0; i < lengths[j]; i++) { vars[j][i] = EEPROM.read(i + ARR_VARS_SZ + offset[j]); }
        vars[j][lengths[j]] = '\0';
        Serial.print(vars[j]);
        Serial.print(",");
      }
      Serial.println();
    }
    else if (first2.equals("wr") && strlen(receivedChars) > 3) {
      String swriteArgs = String(receivedChars).substring(3);
      int swriteSpace = swriteArgs.indexOf(' ');
      if (swriteSpace < 0) { newData = false; return; }
      String swriteArg1 = swriteArgs.substring(0, swriteSpace);
      int arg1 = swriteArg1.toInt();
      if (arg1 >= ARR_VARS_SZ) { arg1 = 0; }
      String swriteArg2 = swriteArgs.substring(swriteSpace + 1);
      // Get current lengths
      for (i = 0; i < ARR_VARS_SZ; i++) { lengths[i] = EEPROM.read(i); }
      // Get new length for this particular variable
      lengths[arg1] = swriteArg2.length();
      // Calculate all other variable lengths to see where we have to write
      int offset = 0; for (i = 0; i < ARR_VARS_SZ; i++) { if (lengths[i] > 32) { lengths[i] = 32; } if (i < arg1) { offset += lengths[i]; } Serial.println(lengths[i], DEC); }
      // Write the length
      EEPROM.write(arg1, lengths[arg1]);
      // Write the string
      for (i = 0; i < lengths[arg1]; i++) { EEPROM.write(i + ARR_VARS_SZ + offset, swriteArg2.charAt(i)); }
      EEPROM.commit();
      Serial.print("Write var"); Serial.print(arg1, DEC); Serial.print(" "); Serial.println(swriteArg2); Serial.print(" ["); Serial.print(lengths[arg1], DEC); Serial.println("]");
    }
    newData = false;
  }
}

int wifi_level(int lvl) {
  if (-90 <= lvl && lvl < -80) { return 1; }
  else if (-80 <= lvl && lvl < -70) { return 2; }
  else if (-70 <= lvl && lvl < -56) { return 3; }
  else if (-56 <= lvl && lvl < -30) { return 4; }
  else if (-30 <= lvl) { return 5; }
  return 0;
}

void writeResponse(WiFiClient client) {
  // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
  // and a content-type so the client knows what's coming, then a blank line:
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type: text/html");
  client.println("Connection: close");
  client.println();

  client.print("<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'><title>WiFi</title><style>.content{display:block;margin:0 auto;}</style></head><body>");
  client.print("<div class='content'><form method='post' action=''>");
  client.print("<span>" + String(ChipId) + "</span><br/>");
  client.print("<label for='net'>WIFI SSID: </label><input type='text' name='net' value='" + String(vars[0]) + "' /><br/>");
  client.print("<label for='psw'>WIFI PASSWORD: </label><input type='password' name='psw' value='" + String(vars[1]) + "'/><br/>");
  client.print("<label for='mod'>WIFI MODE: </label><select name='mod'><option value='0'>Open</option><option value='1'>Wep</option><option value='2'>Wpa Psk</option><option value='3'>Wpa2 Psk</option></select><br/>");
  client.print("<label for='mqu'>MQTT USER: </label><input type='text' name='mqu' value='" + String(vars[2]) + "' /><br/>");
  client.print("<label for='mqp'>MQTT PASSWORD: </label><input type='text' name='mqp' value='" + String(vars[3]) + "' /><br/>");
  client.print("<label for='mqd'>MQTT DOMAIN: </label><input type='text' name='mqd' value='" + String(vars[4]) + "' /><br/>");
  client.print("<label for='mqt'>MQTT PORT: </label><input type='text' name='mqt' value='" + String(vars[5]) + "' /><br/>");
  client.print("<input type='submit' name='sbm' value='Submit'/>");
  client.print("</form></div>");
  client.println("</body></html>");
  // The HTTP response ends with another blank line:
  client.println();
}

void getCredentials() {
  int i = 0;
  int n = 0;
  int offset = 0;
  int netIndex;
  int pswIndex;
  int sbmIndex;
  int modIndex;
  int mquIndex;
  int mqpIndex;
  int mqdIndex;
  int mqtIndex;

  String netssid;
  String netpswd;
  String netmode;
  String mqtuser;
  String mqtpass;
  String mqtdomn;
  String mqtport;
  
  String currentLine = "";
  mystaticip = WiFi.softAPIP();
  display.drawString(0, 0, "Host: " + String(mystaticip[0]) + "." + String(mystaticip[1]) + "." + String(mystaticip[2]) + "." + String(mystaticip[3])); display.display();
  WiFiClient client = espServer.available();
  if (client) {
    display.drawString(0, 10, "Client connected"); display.display();
    display.drawString(0, 20, "Waiting input.."); display.display();
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    String currentLine = "";
    int data_length = -1;
    boolean skip = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
//Serial.write(c);
        currentLine += c;
//if(currentLineIsBlank) {Serial.println("~");Serial.println(currentLine);}
        if (c == '\n' && currentLineIsBlank && currentLine.startsWith("GET")) {
          writeResponse(client);
          break;
        }
        else if (c == '\n' && currentLineIsBlank && currentLine.startsWith("POST") && !skip) {
          writeResponse(client);
          break;
        }
        else if (c == '\n' && currentLineIsBlank && currentLine.startsWith("POST") && skip) {
          skip = false;
          String temp = currentLine.substring(currentLine.indexOf("Content-Length:") + 15);
          temp.trim();
//Serial.print("Content-Length=");
          data_length = temp.toInt();
//Serial.println(data_length);
          while(data_length-- > 0) { c = client.read(); currentLine += c; }
          writeResponse(client);
          break;
        }
        // you're starting a new line
        if (c == '\n') {
          currentLineIsBlank = true;
        }
        // you've gotten a character on the current line
        else if (c != '\r') {
          currentLineIsBlank = false;
        }
      }
    } /* end while client.connected() */

    if (currentLine.indexOf("net=") != -1) {
      netIndex = currentLine.indexOf("net=");
    }

    if (currentLine.indexOf("&psw=") != -1) {
      pswIndex = currentLine.indexOf("&psw=");
    }

    if (currentLine.indexOf("&mod=") != -1) {
      modIndex = currentLine.indexOf("&mod=");
    }

    if (currentLine.indexOf("&mqu=") != -1) {
      mquIndex = currentLine.indexOf("&mqu=");
    }

    if (currentLine.indexOf("&mqp=") != -1) {
      mqpIndex = currentLine.indexOf("&mqp=");
    }

    if (currentLine.indexOf("&mqd=") != -1) {
      mqdIndex = currentLine.indexOf("&mqd=");
    }

    if (currentLine.indexOf("&mqt=") != -1) {
      mqtIndex = currentLine.indexOf("&mqt=");
    }

    if (currentLine.indexOf("&sbm=") != -1) {
      sbmIndex = currentLine.indexOf("&sbm=");
    }

    if (netIndex > 0 && pswIndex > 0 && sbmIndex > 0) {
      display.clear();
      // currentLine GET /?net=abcd&psw=1234&sbm=Submit
      // currentLine POST ... net=abcd&psw=1234&sbm=Submit
      netssid = currentLine.substring(netIndex+4, pswIndex);
      netpswd = currentLine.substring(pswIndex+5, modIndex);
      netmode = currentLine.substring(modIndex+5, mquIndex);
      mqtuser = currentLine.substring(mquIndex+5, mqpIndex);
      mqtpass = currentLine.substring(mqpIndex+5, mqdIndex);
      mqtdomn = currentLine.substring(mqdIndex+5, mqtIndex);
      mqtport = currentLine.substring(mqtIndex+5, sbmIndex);

      netssid.toCharArray(vars[0], 33);
      netpswd.toCharArray(vars[1], 33);
      mqtuser.toCharArray(vars[2], 33);
      mqtpass.toCharArray(vars[3], 33);
      mqtdomn.toCharArray(vars[4], 33);
      mqtport.toCharArray(vars[5], 33);
      // Write eeprom
      lengths[0] = netssid.length();
      lengths[1] = netpswd.length();
      lengths[2] = mqtuser.length();
      lengths[3] = mqtpass.length();
      lengths[4] = mqtdomn.length();
      lengths[5] = mqtport.length();
      EEPROM.write(0, lengths[0]);
      EEPROM.write(1, lengths[1]);
      EEPROM.write(2, lengths[2]);
      EEPROM.write(3, lengths[3]);
      EEPROM.write(4, lengths[4]);
      EEPROM.write(5, lengths[5]);
      offset = ARR_VARS_SZ; for (i = 0; i < lengths[0]; i++) { EEPROM.write(i + offset, netssid.charAt(i)); }
      offset += lengths[0]; for (i = 0; i < lengths[1]; i++) { EEPROM.write(i + offset, netpswd.charAt(i)); }
      offset += lengths[1]; for (i = 0; i < lengths[2]; i++) { EEPROM.write(i + offset, mqtuser.charAt(i)); }
      offset += lengths[2]; for (i = 0; i < lengths[3]; i++) { EEPROM.write(i + offset, mqtpass.charAt(i)); }
      offset += lengths[3]; for (i = 0; i < lengths[4]; i++) { EEPROM.write(i + offset, mqtdomn.charAt(i)); }
      offset += lengths[4]; for (i = 0; i < lengths[5]; i++) { EEPROM.write(i + offset, mqtport.charAt(i)); }
      EEPROM.commit();

      display.drawString(0, 10, String(vars[0]));
      display.drawString(0, 20, String(vars[1]));
      display.drawString(0, 30, String(vars[2]));
      display.drawString(0, 40, String(vars[3]));
      display.drawString(0, 50, String(vars[4]));
      display.display();
      
      // WiFi.disconnect();
      // WiFi.mode(WIFI_STA);
      // delay(100);
      // modeWifi = 0;
    }
//Serial.println(currentLine);
    // give the web browser time to receive the data
    delay(1);
    // close the connection:
    client.stop();
  }
}

void loop() {
  //ArduinoOTA.handle();
  int i = 0;
  int n = 0;

  recvWithEndMarker();
  parseNewData();
  
  display.clear();

  if (modeWifi == 1) {
    getCredentials();
  }
  else {
    if (WiFi.status() == WL_CONNECTED) {
      WiFi.hostByName(ntpServerName, timeServer);
      mystaticip = WiFi.localIP();
      mygateway = WiFi.gatewayIP();
      mysubnet = WiFi.subnetMask();
      display.drawString(0, 0, "Ip: " + String(mystaticip[0]) + "." + String(mystaticip[1]) + "." + String(mystaticip[2]) + "." + String(mystaticip[3]));
      display.drawString(0, 10, "Ntp: " + String(timeServer[0]) + "." + String(timeServer[1]) + "." + String(timeServer[2]) + "." + String(timeServer[3]));
      sendNTPpacket(timeServer);
      delay(1000);
      int cb = udp.parsePacket();
      if (!cb) {
        display.drawString(0, 20, "No time packet");
      }
      else {
        unsigned long epoch = readNTPpacket();
        display.drawString(0, 20, "Unixts: " + String(epoch));
      }
      
      if (!mqttClient.connected()) {
        mqttClient.setServer(vars[4], (unsigned int)atoi(vars[5]));
        mqttClient.setCallback(callback);
        if (mqttClient.connect(ChipId, vars[2], vars[3])) {
          display.drawString(0, 30, "Connected " + String(ChipId));
          mqttClient.subscribe("testtopic");
        }
        else {
          display.drawString(0, 30, "Mqtt failed " + String(mqttClient.state()));
        }
      }
    }
    else {
      // WiFi.scanNetworks will return the number of networks found
      n = WiFi.scanNetworks();
      display.drawString(0, 0, String(n) + "Networks found");
    
      if (n > 0) {
        int k = 0;
        for (i = 0; i < n; ++i) {
          display.drawString(0, 10+k, String(WiFi.SSID(i)));
          display.drawString(110, 10+k, String(wifi_level(WiFi.RSSI(i))));
          k=k+10;
          delay(10);
        }
      }
    }

    if (mqttClient.connected()) {
      mqttClient.loop();
    }
  }

  // write the buffer to the display
  display.display();
  // Wait a bit before scanning again
  delay(4000);
}

// read NTP request
unsigned long readNTPpacket() {
  int NTP_PACKET_SIZE = 48;
  byte packetBuffer[NTP_PACKET_SIZE] = {0};
  udp.read(packetBuffer, NTP_PACKET_SIZE);
  unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
  unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
  unsigned long secsSince1900 = highWord << 16 | lowWord;
  // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
  const unsigned long seventyYears = 2208988800UL;
  // subtract seventy years:
  return secsSince1900 - seventyYears;
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address) {
  int NTP_PACKET_SIZE = 48;
  byte packetBuffer[NTP_PACKET_SIZE] = {0};
  //memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011; packetBuffer[1] = 0; packetBuffer[2] = 6; packetBuffer[3] = 0xEC;
  packetBuffer[12] = 49; packetBuffer[13] = 0x4E; packetBuffer[14] = 49; packetBuffer[15] = 52;
  udp.beginPacket(address, 123); udp.write(packetBuffer, NTP_PACKET_SIZE); udp.endPacket();
}
