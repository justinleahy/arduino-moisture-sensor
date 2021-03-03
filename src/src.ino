#define VERSIONNUMBER "1.0.0"
#define BUILDNUMBER "000015"
#define SENSORNAME "moisture-001"

// ESP8266 Libraries
#include <WiFiEsp.h>
#include <WiFiEspUdp.h>

#include <ESP8266_FTPClient.h>

#include <SoftwareSerial.h>

#include <time.h>

#define WIFI_SSID ""
#define WIFI_PASS ""

char ftp_server[] = "";
char ftp_user[] = "";
char ftp_pass[] = "";

// Number of seconds difference you are from UTC
// EST is plus five hours so 18000 seconds
const int timeZoneConversion = 18000;

// Number of observations to record
const int amount = 3;

ESP8266_FTPClient ftp(ftp_server, ftp_user, ftp_pass);

int status = WL_IDLE_STATUS;

SoftwareSerial SoftSerial(3, 4);

// National Institutite of Standards and Technology NTP server
// https://www.nist.gov/pml/time-and-frequency-division/time-distribution/internet-time-service-its
// https://tf.nist.gov/tf-cgi/servers.cgi
char timeServer[] = "time.nist.gov";
unsigned int localPort = 2390;

const int NTP_PACKET_SIZE = 48;
const int UDP_TIMEOUT = 2000;

byte packetBuffer[NTP_PACKET_SIZE];

// UDP instance to send and receive packets over UDP
WiFiEspUDP udp;

// Update these with your respective pin outs.
int greenLED = 10;
int redLED = 11;

int buttonPower = 17;
int buttonPin = 12;
int buttonState = 0;

int sensorPin = A2;
int sensorValue = 0;

time_t unixTime = 0;
struct tm dateTime;

void initWiFiConnection()
{
  status = WL_IDLE_STATUS;
  // Initialize the ESP module
  WiFi.init(&SoftSerial);

  delay(3000);

  // Attempt to connenct to the WiFi network
  while(status != WL_CONNECTED)
  {
    WiFi.reset();

    // Connect to WPA/WPA2 network
    Serial.print(F("SSID = "));
    Serial.println(WIFI_SSID);

    status = WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial.print(F("Local IP : "));
    Serial.println(WiFi.localIP());
  }
}

void sendNTPRequest(char *ntpServer)
{
  // Set all bytes in the packet buffer to zero
  memset(packetBuffer, 0, NTP_PACKET_SIZE);

  // Initialize values needed for the NTP request packet
  // https://labs.apnic.net/?p=462
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;            // Stratum
  packetBuffer[2] = 6;            // Polling Interval
  packetBuffer[3] = 0xEC;         // Peer Clock Precision
  // Eight bytes of zero for Root Delay and Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;

  udp.beginPacket(ntpServer, 123);

  udp.write(packetBuffer, NTP_PACKET_SIZE);

  udp.endPacket();
}

void setTime()
{
  sendNTPRequest(timeServer);
  
  unsigned long startMs = millis();
  while (!udp.available() && (millis() - startMs) < UDP_TIMEOUT) {}
  
  if(udp.parsePacket())
  {
    Serial.println(F("Packet received"));

    udp.read(packetBuffer, NTP_PACKET_SIZE);

    // The timestamp starts at byte 40 of the packet and is four bytes long.
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);

    // Combine the four bytes into a long integer
    // This is in NTP format (seconds since Jan 1 1900)
    unsigned long ntpTime = highWord << 16 | lowWord;
    ntpTime -= timeZoneConversion;

    Serial.print(F("Seconds since Jan 1 1900 : "));
    Serial.println(ntpTime);

    // Unix time starts at Jan 1 1970. 1970 - 1900 = 70
    // 70 years in seconds is 220,898,800 seconds.
    const unsigned long seventyYears = 2208988800UL;
    unixTime = ntpTime - seventyYears;
    
    Serial.print("Unix time : ");
    Serial.println(unixTime);

    // Convert ntp time to date time (0 <-> Jan 1 1900 00:00)
    dateTime = *localtime(&ntpTime);
    dateTime.tm_year += 1800;
  }
}

void writeFile()
{
  digitalWrite(greenLED, LOW);
  
  // Makes the time as accurate as it can be to the obtaining of data
  setTime();

  digitalWrite(greenLED, HIGH);
  digitalWrite(greenLED, LOW);
  
  String messageString = "";
  String fileString = "";

  String sensorValues = "";
  String sensorLevels = "";
  
  ftp.ChangeWorkDir("~/data/");

  // Create the file name based on the current date and time
  fileString.concat(dateTime.tm_year);
  fileString.concat("-");
  fileString.concat(dateTime.tm_mon + 1);
  fileString.concat("-");
  fileString.concat(dateTime.tm_mday + 1);
  fileString.concat("-");
  fileString.concat(dateTime.tm_hour);
  fileString.concat("-");
  fileString.concat(dateTime.tm_min);
  fileString.concat("-");
  fileString.concat(dateTime.tm_sec);
  fileString.concat("_data.json");

  sensorValues.concat("[");
  sensorLevels.concat("[");
  
  for(int i = 0; i < amount; i++)
  {
    digitalWrite(greenLED, HIGH);
    
    sensorValue = analogRead(sensorPin);

    sensorLevels.concat(sensorValue);
    sensorLevels.concat(",");
    
    if(sensorValue > 430)
    {
      sensorValues.concat("\"dry\",");
    }else if(sensorValue > 350)
    {
      sensorValues.concat("\"wet\",");
    }else if(sensorValue >= 260){
      sensorValues.concat("\"water\",");
    }else{
      sensorValues.concat("\"error\",");
    }
    
    digitalWrite(greenLED, LOW);
    delay(5000);
  }

  sensorValues.remove(sensorValues.length() - 1, 1);
  sensorLevels.remove(sensorLevels.length() - 1, 1);
  sensorValues.concat("]");
  sensorLevels.concat("]");
  
  messageString.concat("{\"sensor\":\"");
  messageString.concat(SENSORNAME);
  messageString.concat("\",\"time\":");
  messageString.concat(unixTime);
  messageString.concat(",\"values\":");
  messageString.concat(sensorValues);
  messageString.concat(",\"levels\":");
  messageString.concat(sensorLevels);
  messageString.concat("}");
  
  digitalWrite(greenLED, HIGH);
  digitalWrite(greenLED, LOW);

  const char fileName[fileString.length() + 1];
  fileString.toCharArray(fileName, fileString.length() + 1);

  const char message[messageString.length() + 1];
  messageString.toCharArray(message, messageString.length() + 1);

  Serial.println(fileName);
  Serial.println(message);
  ftp.InitFile("Type A");
  digitalWrite(greenLED, HIGH);
  digitalWrite(greenLED, LOW);
  ftp.NewFile(fileName);
  ftp.Write(message);
  ftp.CloseFile();
}

void setup()
{
  pinMode(redLED, OUTPUT);
  digitalWrite(redLED, HIGH);
  pinMode(greenLED, OUTPUT);

  pinMode(buttonPin, INPUT);
  
  Serial.begin(115200);

  // Start up message
  Serial.println(F("------------------------------"));
  Serial.println(F("Moisture Sensor IOT"));
  Serial.println(F("Justin Leahy"));
  Serial.print(F("Version "));
  Serial.println(VERSIONNUMBER);
  Serial.print(F("Build "));
  Serial.println(BUILDNUMBER);
  Serial.println(F("------------------------------"));
  Serial.println(F("Initializing"));

  // Setup WiFi
  // Initialize serial for ESP module
  SoftSerial.begin(115200);

  SoftSerial.println("AT+RST");
  char value;
  while(SoftSerial.available())
  {
    value = SoftSerial.read();
    Serial.println(value);
  }

  // Baud rates above ~38,000 do not work reliably on the ATmega328P (Pro Mini)
  SoftSerial.println(F("AT+UART_DEF=19200,8,1,0,0"));
  delay(1000);

  // Restart SoftwareSerial for the slower baud rate for the WiFi
  SoftSerial.end();
  SoftSerial.begin(19200);

  initWiFiConnection();

  udp.begin(localPort);

  ftp.OpenConnection();
}

void loop()
{
  digitalWrite(greenLED, HIGH);
  
  buttonState = digitalRead(buttonPin);

  if(buttonState == HIGH)
  {
    writeFile();
  }
  
  delay(150);
}
