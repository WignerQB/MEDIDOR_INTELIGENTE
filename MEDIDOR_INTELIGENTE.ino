/*
 * Versao 4 otimizada  
 * Dei uma pequena organizada no código
*/
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

/*
   tarHP: de 18 hrs as 21 hrs
   tarHI: de 17 hrs as 18 hrs e de 21 hrs a 22 hrs
   tarHFP: O restante
*/

/*
   custo_ESPERADO: consumo esperado a ser pago pelo consumidor
   custoPLAN_DIA: consumo em R$ planejado por dia
   consumido_total: consumo total da energia em R$
   consumido_DIA: O quanto foi consumido durante um dia
*/
float custo_ESPERADO = 10.50, custoPLAN_DIA, consumido_DIA = 0, consumido_total = 0, tar;//Todos valores simulados
int dias = 1, marc_dia, flag_setup = 0;

int blue = 33, green = 32, LED_ERROR = 2, PLAN_ERROR = 0;
/*
   green -> indica se conseguiu enviar para web
   blue -> indica se conseguiu gravar no SD card
*/


int sumrec_consumido_DIA[500], sumrec_kWh[500], sumrec_marc_dia[500], agrupar_marc_dia = 0, agrupar_consumido_DIA = 0, agrupar_kWh = 0;
int sumrec_consumido_total[500], agrupar_consumido_total = 0;
int i = 0 , j, caseTR = 0;

enum ENUM {
  f_medicao,
  incrementar,
  Enviar_TS,
  backupSD,
  printar
};

ENUM estado = f_medicao, estado_antigo;

EnergyMonitor emon;
WiFiServer server(80);
WiFiClient client;

//-----------------Funcoes----------------------------------------
void SD_config() {
  //Verifica se o cartão está apto
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


  //Ler os arquivos a fim de pegar os valores armazenados
  readFile(SD);


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
  //-----------------------------------------------------------
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
  //-----------------------------------------------------------
  // Create a file on the SD card and write consumido_DIA
  File file_consumido_DIA = SD.open("/consumido_DIA.txt");
  if (!file_consumido_DIA) {
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/consumido_DIA.txt", " ");
    consumido_DIA = 0;
  }
  else {
    Serial.println("File already exists");
  }
  file_consumido_DIA.close();
  //-----------------------------------------------------------
  // Create a file on the SD card and write consumido_total
  File file_consumido_total = SD.open("/consumido_total.txt");
  if (!file_consumido_total) {
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/consumido_total.txt", " ");
    consumido_total = 0;
  }
  else {
    Serial.println("File already exists");
  }
  file_consumido_total.close();
  //-----------------------------------------------------------
  // Create a file on the SD card and write marc_dia
  File file_marc_dia = SD.open("/marc_dia.txt");
  if (!file_marc_dia) {
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/marc_dia.txt", " ");
    flag_setup = 0;
  }
  else {
    Serial.println("File already exists");
    flag_setup = 1;//Mudar para 1
  }
  file_marc_dia.close();
}

float tarifa(int hora) {
  if (hora >= 18 && hora < 21) {
    Serial.println("\nHorário em vigor: Horário  de Ponta");
    return 0.4;
  }
  else if (hora == 17 || hora == 21) {
    Serial.println("\nHorário em vigor: Horário Intermediario");
    return 0.3;
  }
  else {
    Serial.println("\nHorário em vigor: Horário Fora de Ponta");
    return 0.2;
  }
}

void TIMERegister() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Falha ao obter a hora");
    dateRegister = String(timeinfo.tm_mday) + "/" + String(timeinfo.tm_mon + 1)   + "/" + String(timeinfo.tm_year + 1900) + " - " + String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec);
    Serial.println(dateRegister);
    dataread = "Vrms: " + String(sV) + " V; " + "Irms: " + String(Irms) + " A; " + "Fator de Potência: " + String(pF) + "; Potência Real: " + String(rP) + " W; " + "Apparent Power: " + String(aP) + " VA; " + "Energia consumida: " + String(kWh);

    appendFile(SD, "/data.txt", dateRegister + "\t" + dataread + "\r\n");
    writeFile(SD, "/kWh.txt", String((int)(kWh * 1000000)));
    writeFile(SD, "/consumido_DIA.txt", String((int)(consumido_DIA * 1000000)));
    writeFile(SD, "/consumido_total.txt", String((int)(consumido_total * 1000000)));
    return;
  }

  
  switch (caseTR) {
    case 0: //Todas as tarefas que só são necessárias serem feitas 1 vez, salvo alguma exceção
      marc_dia = timeinfo.tm_mday;
      writeFile(SD, "/marc_dia.txt", String(marc_dia));
      //Verifica o mês que está em vigor para definir o consumo médio diário
      if ((timeinfo.tm_mon + 1) == 1 || (timeinfo.tm_mon + 1) == 3 || (timeinfo.tm_mon + 1) == 5 || (timeinfo.tm_mon + 1) == 7 || (timeinfo.tm_mon + 1) == 8 || (timeinfo.tm_mon + 1) == 10 || (timeinfo.tm_mon + 1) == 12) {
        custoPLAN_DIA = custo_ESPERADO / 31;
      }
      else if ((timeinfo.tm_mon + 1) == 4 || (timeinfo.tm_mon + 1) == 6 || (timeinfo.tm_mon + 1) == 9 || (timeinfo.tm_mon + 1) == 11) {
        custoPLAN_DIA = custo_ESPERADO / 30;
      }
      else {
        custoPLAN_DIA = custo_ESPERADO / 28;
      }
      caseTR = 1;
      break;

    case 1://Salva as informações no SD
      Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
      //Mes/dia/ano - H:M:S
      dateRegister = String(timeinfo.tm_mday) + "/" + String(timeinfo.tm_mon + 1)   + "/" + String(timeinfo.tm_year + 1900) + " - " + String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec);
      Serial.println(dateRegister);
      dataread = "Vrms: " + String(sV) + " V; " + "Irms: " + String(Irms) + " A; " + "Fator de Potência: " + String(pF) + "; Potência Real: " + String(rP) + " W; " + "Apparent Power: " + String(aP) + " VA; " + "Energia consumida: " + String(kWh);

      appendFile(SD, "/data.txt", dateRegister + "\t" + dataread + "\r\n");
      writeFile(SD, "/kWh.txt", String((int)(kWh * 1000000)));
      writeFile(SD, "/consumido_DIA.txt", String((int)(consumido_DIA * 1000000)));
      writeFile(SD, "/consumido_total.txt", String((int)(consumido_total * 1000000)));
      caseTR = 2;
      break;

    case 2: //Gerenciamento do consumo de energia
      if (marc_dia != timeinfo.tm_mday) { //Atualiza a variável marc_dia e atualiza o arquivo txt
        marc_dia = timeinfo.tm_mday;
        writeFile(SD, "/marc_dia.txt", String(marc_dia));
        //Verificar se o consumo do dia está dentro do planejado
        consumido_total = consumido_total + consumido_DIA;
    
        //Falta lógica ainda-------------------------------------------------------------------------------------------------------------------
        if (consumido_DIA > custoPLAN_DIA) {
          digitalWrite(LED_ERROR, HIGH);
          Serial.println("\n\n\n\n\n\nConsumiu além do planejado!!!\n\n\n\n\n\n");          
        }
        else {

        }
        consumido_DIA = 0;
      }
      caseTR = 1;
      break;
  }

  
  //Faz a verificação do horário para determinar qual o valor do horário em vigor
  tar = tarifa(timeinfo.tm_hour);//Futuramente será definido de forma online
  Serial.print("Valor do kWh: "); Serial.println(tar);
  Serial.print("\nConsumo médio por dia: "); Serial.println(custoPLAN_DIA);
}

void readFile(fs::FS &fs) {
  //Backup do consumo em kWh-------------------------------------------------------------------------------------
  Serial.printf("Reading file: %s\n",  "/kWh.txt");

  File file_kWh = fs.open("/kWh.txt");
  if (!file_kWh || file_kWh.isDirectory()) {
    Serial.println("Failed to open file_kWh for reading");
    return;
  }

  Serial.print("\nRead from file_kWh: ");
  while (file_kWh.available()) {
    sumrec_kWh[i] = file_kWh.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++)
  {
    agrupar_kWh = agrupar_kWh + (sumrec_kWh[j] - 48) * pow(10, i - j - 1);
  }
  kWh = float(agrupar_kWh) / 1000000;
  i = 0;
  //Backup do consumo em R$---------------------------------------------------------------------------------------
  Serial.printf("Reading file: %s\n",  "/consumido_DIA.txt");

  File file_consumido_DIA = fs.open("/consumido_DIA.txt");
  if (!file_consumido_DIA || file_consumido_DIA.isDirectory()) {
    Serial.println("Failed to open file_consumido_DIA for reading");
    return;
  }

  Serial.print("\nRead from file_consumido_DIA: ");
  while (file_consumido_DIA.available()) {
    sumrec_consumido_DIA[i] = file_consumido_DIA.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++)
  {
    agrupar_consumido_DIA = agrupar_consumido_DIA + (sumrec_consumido_DIA[j] - 48) * pow(10, i - j - 1);
  }
  consumido_DIA = float(agrupar_consumido_DIA) / 1000000;
  i = 0;
  //Backup do consumo total---------------------------------------------------------------------------------------
  Serial.printf("Reading file: %s\n",  "/consumido_total.txt");

  File file_consumido_total = fs.open("/consumido_total.txt");
  if (!file_consumido_total || file_consumido_total.isDirectory()) {
    Serial.println("Failed to open file_consumido_total for reading");
    return;
  }

  Serial.print("\nRead from file_consumido_total: ");
  while (file_consumido_total.available()) {
    sumrec_consumido_total[i] = file_consumido_total.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++) {
    agrupar_consumido_total = agrupar_consumido_total + (sumrec_consumido_total[j] - 48) * pow(10, i - j - 1);
  }
  consumido_total = float(agrupar_consumido_total) / 1000000;
  i = 0;
  //Backup do dia(marcador)---------------------------------------------------------------------------------------
  Serial.printf("Reading file: %s\n",  "/marc_dia.txt");

  File file_marc_dia = fs.open("/marc_dia.txt");
  if (!file_marc_dia || file_marc_dia.isDirectory()) {
    Serial.println("Failed to open file_marc_dia for reading");
    return;
  }

  Serial.print("\nRead from file_marc_dia: ");
  while (file_marc_dia.available()) {
    sumrec_marc_dia[i] = file_marc_dia.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++)
  {
    agrupar_marc_dia = agrupar_marc_dia + (sumrec_marc_dia[j] - 48) * pow(10, i - j - 1);
  }
  marc_dia = agrupar_marc_dia;
  i = 0;

  Serial.print("\n");
}

void writeFile(fs::FS &fs, const char * path, const String message) {
  Serial.printf("Writing file: %s\n", path);

  /* cria uma variável "file" do tipo "File", então chama a função
    open do parâmetro fs recebido. Para abrir, a função open recebe
    os parâmetros "path" e o modo em que o arquivo deve ser aberto
    (nesse caso, em modo de escrita com FILE_WRITE)
  */
  File file = fs.open(path, FILE_WRITE);
  //verifica se foi possivel criar o arquivo
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  /*grava o parâmetro "message" no arquivo. Como a função print
    tem um retorno, ela foi executada dentro de uma condicional para
    saber se houve erro durante o processo.*/
  if (file.print(message)) {
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

    Serial.println("\n\n\n\n\n\n");

    Serial.printf("Conectando em %s ", ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println(" Feito");

    // Set o campo do canal com o valor
    ThingSpeak.setField(1, (float)Irms); //Envia o valor da corrente
    ThingSpeak.setField(2, sV);          //Envia o valor da tensão
    ThingSpeak.setField(3, consumido_total);          //Envia o valor do consumo total em R$
    ThingSpeak.setField(4, consumido_DIA);         //Envia o valor do consumo em R$
    ThingSpeak.setField(5, kWh);      //Envia o valor do consumo em kWh
    ThingSpeak.setField(6, tar);         //Envia o valor do kWh do horário em questão

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
  pinMode(LED_ERROR, OUTPUT);
  digitalWrite(LED_ERROR, LOW);
  digitalWrite(green, LOW);
  digitalWrite(blue, LOW);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
  analogReadResolution(10);
  Serial.begin(9600);
  emon.voltage(35, vCalibration, phase_shift); // Voltage: input pin, calibration, phase_shift
  emon.current(34, currCalibration); // Current: input pin, calibration.

  //  //connect to WiFi--------------------------------------------------
  Serial.printf("Conectando em %s ", ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Feito");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  SD_config();
  TIMERegister();
  ThingSpeak.begin(client);  // Initialize ThingSpeak
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  lastTime3 = millis();
  lastTime2 = millis();
  lastTime = millis();
}


void loop() {
  switch (estado) {//Funcionalidades
    case f_medicao:

      emon.calcVI(20, 2000);
      sV   = emon.Vrms;
      Irms = emon.calcIrms(1480);  // Calculate Irms only
      rP = emon.realPower;
      aP = emon.apparentPower;
      pF = emon.powerFactor;
      if (rP >= 0) {
        estado = incrementar;
      }
      else {
        digitalWrite(LED_ERROR, HIGH);
        digitalWrite(green, LOW);
        digitalWrite(blue, LOW);
      }
      break;

    case incrementar:
      if ((millis() - lastTime3) >= timerDelay3) {
        kWh = kWh + (rP / 3600) / 1000;
        consumido_DIA = consumido_DIA + ((rP / 3600) / 1000) * tar;
        lastTime3 = millis();
      }
      estado = Enviar_TS;
      break;

    case Enviar_TS:
      ThingSpeakPost();
      estado = printar;
      
      break;

    case printar:
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
        Serial.print("consumido_DIA*1000000: ");
        Serial.println(consumido_DIA * 1000000);
        Serial.println("rP , aP , Vrms , Irms , pF");
        estado = backupSD;
        delay(400);
        lastTime2 = millis();
      }
      break;

    case backupSD:
      TIMERegister();
      estado = f_medicao;
      break;

    default :
      break;
  }
}
