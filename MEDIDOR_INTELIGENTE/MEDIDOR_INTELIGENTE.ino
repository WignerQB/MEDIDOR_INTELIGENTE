/*
 * Versao 9
 * Renomeação das variáveis 
 * Parei de ficar enviando informação para o campo 8
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
float kWh, TensaoAlimentacao, FatorPotencia, PotenciaAparente, PotenciaReal;
double Irms;

/*
   tarHP: de 18 hrs as 21 hrs
   tarHI: de 17 hrs as 18 hrs e de 21 hrs a 22 hrs
   tarHFP: O restante
   ConsumoEsperado: consumo esperado a ser pago pelo consumidor
   MetaDiaria: consumo em R$ planejado por dia
   ConsumoTotal: consumo total da energia em R$
   ConsumoDiario: O quanto foi consumido durante um dia
*/

float ConsumoEsperado = 10.50, MetaDiaria, ConsumoDiario = 0, ConsumoTotal = 0, ValorDokWh;//Todos valores simulados
int ContadorDeDias = 30, DiaAtual, flag_setup = 0;

int blue = 33, green = 32, LedDeErro = 2, ErroNoPlanejamento = 0;
/*
   green -> indica se conseguiu enviar para web
   blue -> indica se conseguiu gravar no SD card
*/
//aqui
int sumrec_ConsumoDiario[500], sumrec_kWh[500], sumrec_DiaAtual[500], Agrupar_DiaAtual = 0, Agrupar_ConsumoDiario = 0, Agrupar_kWh = 0;
int sumrec_ConsumoTotal[500], Agrupar_ConsumoTotal = 0;
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
  //Verifica se o cartão está PotenciaAparenteto
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
  // Create a file on the SD card and write ConsumoDiario
  File file_ConsumoDiario = SD.open("/ConsumoDiario.txt");
  if (!file_ConsumoDiario) {
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/ConsumoDiario.txt", " ");
    ConsumoDiario = 0;
  }
  else {
    Serial.println("File already exists");
  }
  file_ConsumoDiario.close();
  //-----------------------------------------------------------
  // Create a file on the SD card and write ConsumoTotal
  File file_ConsumoTotal = SD.open("/ConsumoTotal.txt");
  if (!file_ConsumoTotal) {
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/ConsumoTotal.txt", " ");
    ConsumoTotal = 0;
  }
  else {
    Serial.println("File already exists");
  }
  file_ConsumoTotal.close();
  //-----------------------------------------------------------
  // Create a file on the SD card and write DiaAtual
  File file_DiaAtual = SD.open("/DiaAtual.txt");
  if (!file_DiaAtual) {
    Serial.println("File doens't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/DiaAtual.txt", " ");
    flag_setup = 0;
  }
  else {
    Serial.println("File already exists");
    flag_setup = 1;//Mudar para 1
  }
  file_DiaAtual.close();
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
    dataread = "Vrms: " + String(TensaoAlimentacao) + " V; " + "Irms: " + String(Irms) + " A; " + "Fator de Potência: " + String(FatorPotencia) + "; Potência Real: " + String(PotenciaReal) + " W; " + "Apparent Power: " + String(PotenciaAparente) + " VA; " + "Energia consumida: " + String(kWh);

    appendFile(SD, "/data.txt", dateRegister + "\t" + dataread + "\r\n");
    writeFile(SD, "/kWh.txt", String((int)(kWh * 1000000)));
    writeFile(SD, "/ConsumoDiario.txt", String((int)(ConsumoDiario * 1000000)));
    writeFile(SD, "/ConsumoTotal.txt", String((int)(ConsumoTotal * 1000000)));
    return;
  }

  
  switch (caseTR) {
    case 0: //Todas as tarefas que só são necessárias serem feitas 1 vez, salvo alguma exceção
      DiaAtual = timeinfo.tm_mday;
      writeFile(SD, "/DiaAtual.txt", String(DiaAtual));
      //Verifica o mês que está em vigor para definir o consumo médio diário
      MetaDiaria = ConsumoEsperado / 30;
      caseTR = 1;
      break;

    case 1://Salva as informações no SD
      Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
      //Mes/dia/ano - H:M:S
      dateRegister = String(timeinfo.tm_mday) + "/" + String(timeinfo.tm_mon + 1)   + "/" + String(timeinfo.tm_year + 1900) + " - " + String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec);
      Serial.println(dateRegister);
      dataread = "Vrms: " + String(TensaoAlimentacao) + " V; " + "Irms: " + String(Irms) + " A; " + "Fator de Potência: " + String(FatorPotencia) + "; Potência Real: " + String(PotenciaReal) + " W; " + "Apparent Power: " + String(PotenciaAparente) + " VA; " + "Energia consumida: " + String(kWh);

      appendFile(SD, "/data.txt", dateRegister + "\t" + dataread + "\r\n");
      writeFile(SD, "/kWh.txt", String((int)(kWh * 1000000)));
      writeFile(SD, "/ConsumoDiario.txt", String((int)(ConsumoDiario * 1000000)));
      writeFile(SD, "/ConsumoTotal.txt", String((int)(ConsumoTotal * 1000000)));
      caseTR = 2;
      break;

    case 2: //Gerenciamento do consumo de energia
      if (DiaAtual != timeinfo.tm_mday) { //Verifica se houve a mudança de dia e atualiza a variável que faz o controle e o arquivo txt
        DiaAtual = timeinfo.tm_mday;
        ContadorDeDias --; 
        writeFile(SD, "/DiaAtual.txt", String(DiaAtual));
        ConsumoTotal = ConsumoTotal + ConsumoDiario;//Incrementa na variável que contabiliza o consumo geral, em R$, a quantidade 
        //consumida no mês
    
        if (ConsumoDiario > MetaDiaria) {//Sinaliza ao consumidor que este consumiu em um mais que o ideal e refaz os cálculos necessários
          //Falta lógica ainda-------------------------------------------------------------------------------------------------------------------
          digitalWrite(LedDeErro, HIGH);
          Serial.println("\n\n\n\n\n\nConsumiu além do planejado!!!\n\n\n\n\n\n");   
          ContadorDeDias --;
          if(ConsumoTotal <= ConsumoEsperado){//Verifica se o consumo total ainda está menor que o esperado pelo consumidor
            MetaDiaria = (ConsumoEsperado - ConsumoTotal)/ContadorDeDias;
          }
          else{
            Serial.println("\n\n\n\n\n\nInfelizmente é impossível cumprir com o consumo esperado ao final do mês!");
            Serial.println("Já foi consumido o que era esperado em um mês\n\n\n\n\n\n");
          }
        }
        ConsumoDiario = 0;
      }
      if(ContadorDeDias == 0){//Testa se o período de um mês de monitoramento já foi atingido, para reinicia-lo
        //Zera todas as variáveis...
        ContadorDeDias = 30;
        caseTR = 0;
        ConsumoDiario = 0; 
        ConsumoTotal = 0;
      }
      else{//Se por acaso não atingiu o período de um mês de consumo, continua o código normalmente
        caseTR = 1; 
      }
      break;
  }

  //Faz a verificação do horário para determinar qual o valor do horário em vigor
  ValorDokWh = tarifa(timeinfo.tm_hour);//Futuramente será definido de forma online
  Serial.print("Valor do kWh: "); Serial.println(ValorDokWh);
  Serial.print("\nConsumo médio por dia: "); Serial.println(MetaDiaria);
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
    Agrupar_kWh = Agrupar_kWh + (sumrec_kWh[j] - 48) * pow(10, i - j - 1);
  }
  kWh = float(Agrupar_kWh) / 1000000;
  i = 0;
  //Backup do consumo em R$---------------------------------------------------------------------------------------
  Serial.printf("Reading file: %s\n",  "/ConsumoDiario.txt");

  File file_ConsumoDiario = fs.open("/ConsumoDiario.txt");
  if (!file_ConsumoDiario || file_ConsumoDiario.isDirectory()) {
    Serial.println("Failed to open file_ConsumoDiario for reading");
    return;
  }

  Serial.print("\nRead from file_ConsumoDiario: ");
  while (file_ConsumoDiario.available()) {
    sumrec_ConsumoDiario[i] = file_ConsumoDiario.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++)
  {
    Agrupar_ConsumoDiario = Agrupar_ConsumoDiario + (sumrec_ConsumoDiario[j] - 48) * pow(10, i - j - 1);
  }
  ConsumoDiario = float(Agrupar_ConsumoDiario) / 1000000;
  i = 0;
  //Backup do consumo total---------------------------------------------------------------------------------------
  Serial.printf("Reading file: %s\n",  "/ConsumoTotal.txt");

  File file_ConsumoTotal = fs.open("/ConsumoTotal.txt");
  if (!file_ConsumoTotal || file_ConsumoTotal.isDirectory()) {
    Serial.println("Failed to open file_ConsumoTotal for reading");
    return;
  }

  Serial.print("\nRead from file_ConsumoTotal: ");
  while (file_ConsumoTotal.available()) {
    sumrec_ConsumoTotal[i] = file_ConsumoTotal.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++) {
    Agrupar_ConsumoTotal = Agrupar_ConsumoTotal + (sumrec_ConsumoTotal[j] - 48) * pow(10, i - j - 1);
  }
  ConsumoTotal = float(Agrupar_ConsumoTotal) / 1000000;
  i = 0;
  //Backup do dia(marcador)---------------------------------------------------------------------------------------
  Serial.printf("Reading file: %s\n",  "/DiaAtual.txt");

  File file_DiaAtual = fs.open("/DiaAtual.txt");
  if (!file_DiaAtual || file_DiaAtual.isDirectory()) {
    Serial.println("Failed to open file_DiaAtual for reading");
    return;
  }

  Serial.print("\nRead from file_DiaAtual: ");
  while (file_DiaAtual.available()) {
    sumrec_DiaAtual[i] = file_DiaAtual.read();
    i = i + 1;
  }
  for (j = 0; j < i; j++)
  {
    Agrupar_DiaAtual = Agrupar_DiaAtual + (sumrec_DiaAtual[j] - 48) * pow(10, i - j - 1);
  }
  DiaAtual = Agrupar_DiaAtual;
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
    ThingSpeak.setField(2, TensaoAlimentacao);          //Envia o valor da tensão
    ThingSpeak.setField(3, ConsumoTotal);          //Envia o valor do consumo total em R$
    ThingSpeak.setField(4, ConsumoDiario);         //Envia o valor do consumo em R$
    ThingSpeak.setField(5, kWh);      //Envia o valor do consumo em kWh
    ThingSpeak.setField(6, FatorPotencia);         //Envia o valor do fator de potência
    ThingSpeak.setField(7, MetaDiaria); //Envia a meta diaria de consumo para o ThingSpeak

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
  pinMode(LedDeErro, OUTPUT);
  digitalWrite(LedDeErro, LOW);
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
    case f_medicao://Estado aonde é feita a mensuração da corrente, tensão, as potências ativas e reativas e o fator de potência
    //Nesse estado também é realizado a verificação de possíveis erros dado a imprecisão do sensor de corrente.
      //Serial.println("Estado f_medicao");
      emon.calcVI(20, 2000);
      TensaoAlimentacao   = emon.Vrms;
      Irms = emon.calcIrms(1480);  // Calculate Irms only
      PotenciaReal = emon.realPower;
      PotenciaAparente = emon.apparentPower;
      FatorPotencia = emon.powerFactor;
      if (PotenciaReal >= 0) {
        estado = incrementar;
      }
      else {
        digitalWrite(LedDeErro, HIGH);
        digitalWrite(green, LOW);
        digitalWrite(blue, LOW);
      }
      break;

    case incrementar://Nesse estado é feito o cálculo do consumo em R$ e em kWh
      //Serial.println("Estado incrementar");
      if ((millis() - lastTime3) >= timerDelay3) {
        kWh = kWh + (PotenciaReal / 3600) / 1000;
        ConsumoDiario = ConsumoDiario + ((PotenciaReal / 3600) / 1000) * ValorDokWh;
        lastTime3 = millis();
      }
      estado = Enviar_TS;
      break;

    case Enviar_TS://Envia para o ThingSpeak os dados de cada ciclo de leitura
      //Serial.println("Estado Enviar_TS");
      ThingSpeakPost();
      estado = printar;
      break;

    case printar://Printa as informações lidas e calculadas
      if ((millis() - lastTime2) >= timerDelay2)
      {
        //Serial.println("Estado printar");
        Serial.print(" Vrms: ");
        Serial.print(TensaoAlimentacao);
        Serial.print(" V     ");
        Serial.print("Irms: ");
        Serial.print(Irms);
        Serial.print(" A     ");
        Serial.print("Potência Real: ");
        Serial.print(PotenciaReal);
        Serial.print(" W     ");
        Serial.print("Potência Aparente: ");
        Serial.print(PotenciaAparente);
        Serial.print(" VA");
        Serial.print("Fator de Potência: ");
        Serial.println(FatorPotencia);
        Serial.print("kWh*1000000: ");
        Serial.println(kWh * 1000000);
        Serial.print("ConsumoDiario*1000000: ");
        Serial.println(ConsumoDiario * 1000000);
        Serial.println("PotenciaReal , PotenciaAparente , Vrms , Irms , FatorPotencia");
        estado = backupSD;
        //delay(400);
        lastTime2 = millis();
      }
      break;

    case backupSD: //Faz o backup de todos os dados em um cartão SD
      //Serial.println("Estado backupSD");
      TIMERegister();
      estado = f_medicao;
      break;

    default :
      //Serial.println("Estado default");
      break;
  }
}
