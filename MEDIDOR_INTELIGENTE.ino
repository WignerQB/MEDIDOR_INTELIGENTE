#include <ThingSpeak.h>
#include "EmonLib.h"
#include <driver/adc.h>
#include <WiFi.h>
#include "time.h"
#include "FS.h"
#include "SD.h"
#include <SPI.h>


/*Pinagem do módulo SD na ESP32
   CS: D5
   SCK:D18
   MOSI:D23
   MISO:D19
*/

// Force EmonLib to use 10bit ADC resolution
#define ADC_BITS    10
#define ADC_COUNTS  (1<<ADC_BITS)
#define vCalibration 600
#define currCalibration 7.7
#define phase_shift 1.7
#define SD_CS 5

// Essas informações são de um canal publico que eu criei, pode usar se precisar:
// (Acesso: https://thingspeak.com/channels/1076260)
unsigned long myChannelNumber = 1076260; //ID DO CANAL
const char * myWriteAPIKey = "UICL5S86IO3X0NQF";

const char* ssid       = "WIGNER";
const char* password   = "wigner123";

//const char* ssid       = "VIRUS";
//const char* password   = "987654321";


const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -3;
//nao esqueca de ajustar o fuso
const int   daylightOffset_sec = -3600 * 3;

unsigned long timerDelay = 10000, timerDelay2 = 1000, timerDelay3 = 1000;
unsigned long lastTime = 0, lastTime2 = 0, lastTime3 = 0;

String dataread, dateRegister;
float kWh, sV, pF, aP, rP;
double Irms;

int blue = 33, green = 32, error_rP = 2;
/*
   green -> indica se conseguiu enviar para web
   blue -> indica se conseguiu gravar no SD card
*/


int sumrec[500],i=0,j, agrupar= 0;
EnergyMonitor emon;
WiFiServer server(80);
WiFiClient client;

//-----------------Funcoes----------------------------------------
void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Falha ao obter a hora");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  //Mes/dia/ano - H:M:S
  dateRegister = String(timeinfo.tm_mday) + "/" + String(timeinfo.tm_mon + 1)   + "/" + String(timeinfo.tm_year + 1900) + " - " + String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec);
  Serial.println(dateRegister);
  dataread = "Vrms: " + String(sV) + " V; " + "Irms: " + String(Irms) + " A; " + "Fator de Potência: " + String(pF) + "; Potência Real: " + String(rP) + " W; " + "Apparent Power: " + String(aP) + " VA; " + "Energia consumida: " + String(kWh);
 
  appendFile(SD, "/data.txt", dateRegister + "\t" + dataread + "\r\n");
  writeFile(SD, "/kWh.txt", String((int)(kWh*1000000)));
  Serial.println((int)(kWh*1000000));
}

void readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\n", path);

    File file = fs.open(path);
    if(!file || file.isDirectory()){
        Serial.println("Failed to open file for reading");
        return;
    }

    Serial.print("\nRead from file: ");
    while(file.available()){
        sumrec[i] = file.read();
        i=i+1;
    }
    for(j = 0; j < i; j++)
    {
      Serial.println(sumrec[j]);
      agrupar = agrupar + (sumrec[j]-48)*pow(10,i-j-1);
    }
    Serial.println(agrupar);
    kWh = float(agrupar)/1000000;
    i=0;
    Serial.print("\n");
}

void writeFile(fs::FS &fs, const char * path, const String message){
    Serial.printf("Writing file: %s\n", path);

    /* cria uma variável "file" do tipo "File", então chama a função 
    open do parâmetro fs recebido. Para abrir, a função open recebe
    os parâmetros "path" e o modo em que o arquivo deve ser aberto 
    (nesse caso, em modo de escrita com FILE_WRITE) 
    */
    File file = fs.open(path, FILE_WRITE);
    //verifica se foi possivel criar o arquivo
    if(!file){
        Serial.println("Failed to open file for writing");
        return;
    }
    /*grava o parâmetro "message" no arquivo. Como a função print
    tem um retorno, ela foi executada dentro de uma condicional para
    saber se houve erro durante o processo.*/
    if(file.print(message)){
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
}

// Append to the SD card (DON'T MODIFY THIS FUNCTION)
void appendFile(fs::FS &fs, const char * path, const String message) {
  if (path[0] != '/') {
    Serial.println("File path needs start with /. Change it.");
  }

  Serial.printf("Appending to file: %s\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file) {
    Serial.println("Failed to open file for appending");
    digitalWrite(blue, LOW);
    return;
  }

  if (file.print(message)) {
    Serial.println("Message appended");
    digitalWrite(blue, HIGH);
  } else {
    Serial.println("Append failed");
    digitalWrite(blue, LOW);
  }
  file.close();
}


void ThingSpeakPost() {
  if ((millis() - lastTime) > timerDelay) {

    Serial.println("\n\n\n\n\n\n\n\n\n\n\n\n");

    Serial.printf("Conectando em %s ", ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println(" Feito");

    ThingSpeak.setField(1, (float)Irms);     // Set o campo do canal com o valor
    ThingSpeak.setField(2, sV);     // Set o campo do canal com o valor
    ThingSpeak.setField(3, kWh);

    int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);   // Escrever no canal do ThingSpeak
    if (x == 200) {
      Serial.println("Channel update successful.");
      digitalWrite(green, HIGH);
      delay(200);
      digitalWrite(green, LOW);
    }
    else {
      Serial.println("Problem updating channel. HTTP error code " + String(x));
      digitalWrite(green, LOW);
    }
    Serial.println("");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    lastTime = millis();
  }
}

//----------------------------Setup---------------------------

void setup() {
  pinMode(green, OUTPUT);
  pinMode(blue, OUTPUT);
  pinMode(error_rP, OUTPUT);
  digitalWrite(error_rP, LOW);
  digitalWrite(green, LOW);
  digitalWrite(blue, LOW);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
  analogReadResolution(10);
  Serial.begin(9600);
  emon.voltage(35, vCalibration, phase_shift); // Voltage: input pin, calibration, phase_shift
  emon.current(34, currCalibration); // Current: input pin, calibration.

  // Initialize SD card------------------------------------------------
  SD.begin(SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("Card Mount Failed");
    //return;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    //return;
  }
  Serial.println("Initializing SD card...");
  if (!SD.begin(SD_CS)) {
    Serial.println("ERROR - SD card initialization failed!");
    //return;    // init failed
  }

  readFile(SD, "/kWh.txt");
  
  // If the data.txt file doesn't exist
  // Create a file on the SD card and write the data labels
  File file = SD.open("/data.txt");
  if (!file) {
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    appendFile(SD, "/data.txt", "Date| Hour| Mensage \r\n");
  }
  else {
    Serial.println("File already exists");
  }
  file.close();

  // Create a file on the SD card and write kWh
  File file_kWh = SD.open("/kWh.txt");
  if (!file_kWh) {
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/kWh.txt", " ");
    kWh = 0;
  }
  else {
    Serial.println("File already exists");
  }
  file_kWh.close();

  //  //connect to WiFi--------------------------------------------------
  Serial.printf("Conectando em %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Feito");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  printLocalTime();
  ThingSpeak.begin(client);  // Initialize ThingSpeak
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  lastTime3 = millis();
  lastTime2 = millis();
  lastTime = millis();
}

void loop() {

  emon.calcVI(20, 2000);
  sV   = emon.Vrms;
  Irms = emon.calcIrms(1480);  // Calculate Irms only
  rP = emon.realPower;
  aP = emon.apparentPower;
  pF = emon.powerFactor;


  if (rP < 0) { //Caso a potência seja negativa, inverter o CT sensor
    digitalWrite(error_rP, HIGH);
    digitalWrite(green, LOW);
    digitalWrite(blue, LOW);
  }
  else {
    digitalWrite(error_rP, LOW);
    if ((millis() - lastTime3) >= timerDelay3) {
      kWh = kWh + (rP / 3600) / 1000;
      lastTime3 = millis();
    }


    ThingSpeakPost(); // Parâmetros: (Valor, campo)

    if ((millis() - lastTime2) >= timerDelay2)
    {
      Serial.print(" Vrms: ");
      Serial.print(sV);
      Serial.print(" V     ");
      Serial.print("Irms: ");
      Serial.print(Irms);
      Serial.print(" A     ");
      Serial.print("Potência Real: ");
      Serial.print(rP);
      Serial.print(" W     ");
      Serial.print("Potência Aparente: ");
      Serial.print(aP);
      Serial.print(" VA");
      Serial.print("Fator de Potência: ");
      Serial.println(pF);
      Serial.print("kWh*1000000: ");
      Serial.println(kWh * 1000000);
      Serial.println("rP , aP , Vrms , Irms , pF");
      //emon.serialprint();

      printLocalTime();
      lastTime2 = millis();
    }
  }
}
