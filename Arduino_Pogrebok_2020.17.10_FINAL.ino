// Доработка передачи данных о состоянии включено выключено отопление
// Финальная версия программы погребок 2020.10.07
//Доработал подсчет включений
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <OneWire.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <BME280I2C.h>
#include <SD.h>
#include <SPI.h>
#include <RtcDS1307.h>

#define DS18B20_PIN 2      // Контакт GPIO2 DS18B20(d4)
#define DS18B20_COUNT 2    // Количество датчиков на шине Wire
#define SERIAL_BAUD 250000 // Скорость передачи com порта
#define countof(a) (sizeof(a) / sizeof(a[0])) //Вычисляют количество элементов в статически выделенном массиве.

WiFiClient client;
ESP8266WebServer server (80);
WiFiServer server2(85);                //*17.11.2019 управление подсветкой
LiquidCrystal_I2C lcd(0x27, 20, 4); // set the LCD address to 0x27
BME280I2C bme;                      // BMP280 датчик
OneWire ds(DS18B20_PIN);            // ds18b20 Pin
RtcDS1307<TwoWire> Rtc(Wire);
File myFile;

struct meteo_home
{
  float temp_street_max; //Темпреатура улица максимальная
  float temp_street_min; //Темпреатура улица
  float pressure;        //Атмосферное давление
  float temp_cellar;     //Темпреатура погреб      29.11.2019 убрал инициализацию  
  float temp_street;     //Темпреатура улица
  float temp_home;       //Темпреатура дом
  float kwt_full;        //Расход электроэнергии
  float time_power;      //Время работы нагревателя
  int count_tarn;        //Количество включений
};

struct dot_wifi
{
 const char *ssid; // Точка доступа
 const char *password; //Пароль
};

dot_wifi dot[] = {"teremok","19781978","TP-LINK_8357","64781570"};

byte degree[8] =         // Битовая маска символа градуса
{
  B00111,
  B00101,
  B00111,
  B00000,
  B00000,
  B00000,
  B00000,
};

const char *monthName[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun",
  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};


//const char *ssid = "teremok"; // Точка доступа
//const char *password = "19781978"; // Пароль

//const char *ssid = "TP-LINK_8357"; // Точка доступа
//const char *password = "64781570";


char server_php_raspberry[] = "192.168.88.10"; // PHP server // Raspberry PI3
char server_php_debian[] = "37.193.0.199"; // PHP server Debian
const int SSR40DA_pin = 0; // GPIO 0 (D3) Контакт управления реле SSR 40DA

bool flag = false; // Управление реле
float timeIn = 0;
float timeOUT = 0;
//const int led_record = 16; // Светодиод GPIO 16 (d0) Работа с SD
//const int led_send = 13;   // Светодиод GPIO 13 (d7) Отправка данных на сервер
byte my_addr[DS18B20_COUNT][8];//={{0x28,0x81,0xC4,0xBA,2,0,0,0x3B},// адреса устройста на шине
int tek_sensor = 0;

String sfilename;
String dir_temp;
char filename[20];
String sTemp;
String record;
struct meteo_home meteo;


unsigned int get_write_file = millis(); // Частота отправки данных на сервер
unsigned get_send_php = millis();           // Частота отправки данных на сервер
unsigned get_lcd = millis();

const int interval_write_file = 1800000; // 30 минут
const int interval_send_php = 1200000; // 20 минут
const int interval_lcd = 10000; // 10 сек
int interval_count = 0; // Порядок обновления экрана


///////////////////////////////////////////////////////////////////////////////////////////
//Функция возвращает имя файла
//////////////////////////////////////////////////////////////////////////////////////////
String get_file_name(const RtcDateTime & dt, int day_find = 0) {
  char filename_string[13];
  snprintf_P(filename_string,
             countof(filename_string),
             PSTR("%02u-%02u-%02u.txt"),
             //PSTR("DS18B20/%s/%04u_%02u.txt"),
             dt.Day() - day_find,// Применяем для поиска файлов
             dt.Month(),
             dt.Year() - 2000);
  return filename_string;
}
///////////////////////////////////////////////////////////////////////////////////////////
//Функция возвращает имя папки
//////////////////////////////////////////////////////////////////////////////////////////
String get_dir_name(const RtcDateTime & dt, int month_find = 1) {
  char dir_string[4];
  snprintf_P(dir_string,
             countof(dir_string),
             PSTR("%s"),
             monthName[dt.Month() - month_find]); //Применяем для поиска подпапки
  return dir_string;
}
void setup() {
  //pinMode(0, OUTPUT); Приводит к зависанию программы
  //pinMode(16, 1); Приводит к зависанию программы
  lcd.init();
  lcd.clear();
  lcd.backlight();
  Serial.begin(SERIAL_BAUD);
  Wire.begin();
  Find_DS18b20();
  WiFi.begin ( dot[0].ssid, dot[0].password );
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  server2.begin();                                                                                 // *17.10.2020 отключил управлением подсветкой*
  Serial.println("Server started");
  if ( MDNS.begin ( "esp8266" ) ) {
    Serial.println ( "MDNS responder started" );
  }
  server.on ( "/", handleRoot );
  //server.on ( "/test.svg", drawGraph );
  server.on ( "/inline", []() {
    server.send ( 200, "text/plain", "this works as well" );
  } );
  server.onNotFound ( handleNotFound );
  server.begin();
  Serial.println ( "HTTP server started" );
  if (!SD.begin(15)) // SD GPIO 15  Пин SD Card
  {
    Serial.println("Initialization SD failed!");
    lcd.setCursor(0, 0);
    lcd.print("Initialization SD");
    lcd.setCursor(0, 1);
    lcd.print("Failed!");
    delay(2000);
    //return;
  }
  else
  {
    lcd.clear();
    Serial.println("Initialization SD done.");
    lcd.setCursor(0, 0);
    lcd.print("Find SD");
    lcd.setCursor(0, 1);
    lcd.print("Done");
    delay(1000);
  }

  if (!bme.begin())
  {
    Serial.println("Could not find BME280 sensor!");
    lcd.setCursor(0, 2);
    lcd.print("Not find BME280");
  }
  else
  {
    Serial.println("Find BME280 sensor done!");
    lcd.setCursor(0, 2);
    lcd.print("Find BME280");
    lcd.setCursor(0, 3);
    lcd.print("Done");
  }
  pinMode(SSR40DA_pin, OUTPUT);
  digitalWrite(SSR40DA_pin, 0);
  delay(1000);
  // Экран приветствия при первом запуске
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Hello, Alex");
  lcd.setCursor(0, 1);
  lcd.print("Webserver Start");
  lcd.setCursor(0, 2);
  lcd.print("Arduino NodeMcu V3");
  lcd.setCursor(0, 3);
  lcd.print("Meteorological Home");
  delay (2000);
  initializing_after_reset(); //Восстанавливаем накопленные значения, инициализируем переменные после сбоя питания
}

void loop()
{
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  initializing_PHP();  //*17.10.2020отключил
  RtcDateTime now = Rtc.GetDateTime(); // 2020/17/10 включил
  if (check_time_print())
  {
    printBME280Data(&Serial); // Давление и температура
    record_file(); // Пишем данные в файл
  }
  for (int i = 1; i <= DS18B20_COUNT; i++)
    get_temp(i);
  relay_control();// Управление реле
  lcd_print_date();// Экран 0 Дата
  lcd_print_street ();// Экран 1 Дата
  lcd_print_cellar(); // Экран 2 Погреб
  server.handleClient();
}
///////////////////////////////////////////////////////////////////////////////////////////
//Вывод на LСD дисплей Экран 1
///////////////////////////////////////////////////////////////////////////////////////////
void lcd_print_date () {
  RtcDateTime now = Rtc.GetDateTime();
  if (millis() - get_lcd > interval_lcd && interval_count == 0) // Выдерживаем интервал
  {
    if (millis() < get_lcd) { // Проверка на обнуление millis()
      get_lcd = millis();
      return;
      Serial.print("Error cicl");
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Date:" + printDate(now));
    lcd.setCursor(0, 1);
    lcd.print("Time:" + printTime(now));
    lcd.setCursor(0, 2);
    lcd.print("Temp Home:");
    lcd.setCursor(10, 2);
    lcd.print(meteo.temp_home);
    lcd.createChar(2, degree);
    lcd.setCursor(15, 2);
    lcd.print("\2");
    lcd.setCursor(16, 2);
    lcd.print("C");
    lcd.setCursor(0, 3);
    lcd.print("Pressure:");
    lcd.setCursor(9, 3);
    lcd.print(meteo.pressure);
    lcd.setCursor(15, 3);
    lcd.print("mm");
    get_lcd = millis();
    interval_count++;
  }

}
///////////////////////////////////////////////////////////////////////////////////////////
//Вывод на LСD дисплей Экран 2 Улица
///////////////////////////////////////////////////////////////////////////////////////////
void lcd_print_street () {
  RtcDateTime now = Rtc.GetDateTime();
  if (millis() - get_lcd > interval_lcd && interval_count == 1) // Выдерживаем интервал
  {
    if (millis() < get_lcd) { // Проверка на обнуление millis()
      get_lcd = millis();
      return;
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Temp Street:");
    lcd.setCursor(12, 0);
    lcd.print(meteo.temp_street);
    lcd.createChar(3, degree);
    lcd.setCursor(18, 0);
    lcd.print("\3");
    lcd.setCursor(19, 0);
    lcd.print("C");

    lcd.setCursor(0, 3);
    lcd.print("Pressure:");
    lcd.setCursor(9, 3);
    lcd.print(meteo.pressure);
    lcd.setCursor(15, 3);
    lcd.print("mm");

    lcd.setCursor(0, 2 );
    lcd.print("Temp MAX:");
    lcd.setCursor(9, 2);
    lcd.print(meteo.temp_street_max);
    lcd.setCursor(15, 2);
    //lcd.createChar(1, degree);
    lcd.print("\3");
    lcd.setCursor(16, 2);
    lcd.print("C");

    lcd.setCursor(0, 1 );
    lcd.print("Temp MIN:");
    lcd.setCursor(9, 1);
    lcd.print(meteo.temp_street_min);
    lcd.setCursor(15, 1);
    //lcd.createChar(1, degree);
    lcd.print("\3");
    lcd.setCursor(16, 1);
    lcd.print("C");
    get_lcd = millis();
    interval_count++;
  }
}
///////////////////////////////////////////////////////////////////////////////////////////
//Вывод на LСD дисплей Экран 3 Погреб
///////////////////////////////////////////////////////////////////////////////////////////
void lcd_print_cellar () {
  RtcDateTime now = Rtc.GetDateTime();

  if (millis() - get_lcd > interval_lcd && interval_count == 2) // Выдерживаем интервал
  {
    if (millis() < get_lcd) { // Проверка на обнуление millis()
      get_lcd = millis();
      return;
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Temp Cellar:");
    lcd.setCursor(12, 0);
    lcd.print(meteo.temp_cellar);
    lcd.createChar(4, degree);
    lcd.setCursor(18, 0);
    lcd.print("\4");
    lcd.setCursor(19, 0);
    lcd.print("C");

    lcd.setCursor(0, 1);
    lcd.print("Time Pow:");
    lcd.setCursor(9, 1);
    lcd.print(meteo.time_power);

    lcd.setCursor(0, 2 );
    lcd.print("Electric:");
    lcd.setCursor(9, 2);
    lcd.print(meteo.kwt_full);

    lcd.setCursor(0, 3 );
    lcd.print("Heater:");
    lcd.setCursor(7, 3);
    if ((digitalRead(SSR40DA_pin) == true))
      lcd.print("On");
    else
      lcd.print("Off");

    get_lcd = millis();
    interval_count = 0;
  }
}
///////////////////////////////////////////////////////////////////////////////////////////
//Блок управления реле
///////////////////////////////////////////////////////////////////////////////////////////
void relay_control() {
  if (meteo.temp_cellar < 3)
    digitalWrite(SSR40DA_pin, HIGH);
  if (meteo.temp_cellar > 4)
    digitalWrite(SSR40DA_pin, LOW);

  if ((timeIn == 0) && (digitalRead(SSR40DA_pin) == true) && (flag == false)) { //
    timeIn = millis();
    flag = true;
  }
  if ((timeIn != 0) && (digitalRead(SSR40DA_pin)) == false && (flag == true) ) { //>3
    timeOUT = millis();
    if (timeOUT < timeIn) { // Если milis обнулится
      timeIn = millis();
      return;
    }
    timeOUT = timeOUT - timeIn;
    meteo.kwt_full += (0.5 * (timeOUT / 1000.00 / 60.00 / 60.00));
    meteo.time_power += (timeOUT / 1000.00 / 60.00);
    if (timeOUT > 10000) // Если обогрев работал больше 10 секунд увелиичиваем счетчик количества включений
      meteo.count_tarn++;
    flag = false;
    timeIn = 0;
  }
  if (((millis() - get_send_php) > interval_send_php  || get_send_php > millis()) && WiFi.status() == WL_CONNECTED) { // отправляем данн на сервер каждые 10 минут... даже если обнулиться millis и если нет подключчения пробуем подключиться
    sendData(meteo.temp_street_max, meteo.temp_street_min, meteo.pressure, meteo.temp_cellar, meteo.temp_street, meteo.temp_home, meteo.kwt_full, meteo.time_power, meteo.count_tarn, digitalRead(SSR40DA_pin));
    get_send_php = millis();
    Serial.println("SEND DATA SERVER: PHP");
  }
  
   if(WiFi.status() != WL_CONNECTED){
//    Serial.println("No Connected");
//    Serial.print("IP address: ");
//    Serial.println(WiFi.localIP());
//    
//    Serial.println("Reconnected");  
//    Serial.println(dot[0].ssid);  
//    Serial.println(dot[0].password);  
//    WiFi.begin ( dot[0].ssid, dot[0].password );

    for(int i =0;i<1;i++)  //i =1;i<2 2-я точка доступа
    {
    Serial.println("No Connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    
    Serial.println("Reconnected");  
    Serial.println(dot[i].ssid);  
    Serial.println(dot[i].password);  
    WiFi.begin ( dot[i].ssid, dot[i].password );
    for (int i = 0; i < 5; i++ ) // Wait for connection
      Serial.print ( "." );
    delay(5000);
      Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    if(WiFi.status() == WL_CONNECTED)
    return;
  
    
    }
    

   }

  
}
////////////////////////////////////////////////////////////////////////////////////////////
//Отправляем данные на web сервер
////////////////////////////////////////////////////////////////////////////////////////////
void sendData(float temp_street_max, float temp_street_min, float pressure, float temp_cellar, float temp_street, float temp_home, float kwt_full, float time_power, int count_tarn, int heating) {
  for (int i = 0; i < 2 ; i++) {                            // i=1 на малинку не отправляем
      if (i == 0)
       client.connect(server_php_raspberry, 80);//*17.10.2020*****
     else
  client.connect(server_php_debian, 1010);
  client.print( "GET /home_meteo.php?");
  client.print("json="); client.print("{"); 
  client.print("\"contents\":");
  //client.print("{\"temp_street_max\":\""); client.print(temp_street_max); client.print("\","); //2019.12.10 отключил передачу этих параметров
  //client.print("\"temp_street_min\":\""); client.print(temp_street_min); client.print("\",");
  client.print("{\"pressure\":\""); client.print(pressure); client.print("\",");
  client.print("\"temp_cellar\":\""); client.print(temp_cellar); client.print("\",");
  client.print("\"temp_street\":\""); client.print(temp_street); client.print("\",");
  client.print("\"temp_home\":\""); client.print(temp_home); client.print("\",");
  client.print("\"kwt_full\":\""); client.print(kwt_full); client.print("\",");
  client.print("\"time_power\":\""); client.print(time_power); client.print("\",");
  client.print("\"count_tarn\":\""); client.print(count_tarn); client.print("\",");
  client.print("\"heating\":\""); client.print(heating); client.print("\"}}"); //Подогрев включен...выключен
  client.println(" HTTP/1.1");
  client.print( "Host: " );
//  client.connect(server_php_debian, 1010);
//  client.print( "GET /home_meteo.php?");
//  client.print("json="); client.print("{"); client.print("\"contents\":");
//  client.print("{\"temp_street_max\":\""); client.print(temp_street_max); client.print("\",");
//  client.print("\"temp_street_min\":\""); client.print(temp_street_min); client.print("\",");
//  client.print("\"pressure\":\""); client.print(pressure); client.print("\",");
//  client.print("\"temp_cellar\":\""); client.print(temp_cellar); client.print("\",");
//  client.print("\"temp_street\":\""); client.print(temp_street); client.print("\",");
//  client.print("\"temp_home\":\""); client.print(temp_home); client.print("\",");
//  client.print("\"kwt_full\":\""); client.print(kwt_full); client.print("\",");
//  client.print("\"time_power\":\""); client.print(time_power); client.print("\",");
//  client.print("\"count_tarn\":\""); client.print(count_tarn); client.print("\"}}");
//  client.println(" HTTP/1.1");
//  client.print( "Host: " );
 if (i == 0)
 client.println(server_php_raspberry);
 else
  client.println(server_php_debian);
  client.println( "Connection: close" );
  client.println();//Два символа конца строки
  client.println();//Окончание передачи
  client.stop();
  client.flush();
  }
}
////////////////////////////////////////////////////////////////////////////////////////////
//ESP8266WebServer
////////////////////////////////////////////////////////////////////////////////////////////
void handleRoot() {
  RtcDateTime now = Rtc.GetDateTime();
  unsigned long sec = millis() / 1000;
  unsigned long min = sec / 60;
  unsigned long hr = min / 60;
  unsigned long den = hr / 24;
  const char *s;
  String string_html;
  string_html = "<html>\
  <head>\
    <title>Погребок V2.0</title>\
    <meta charset=\"utf-8\">\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
   <h2><font color=\"black\">Программа контроля температуры на базе Arduino ESP8266 Web Server V2019.12.10<br />";
  string_html += "Время работы программы: ";
  if (hr > 24 || den > 0) {
    string_html += " Дней: ";
    string_html += hr / 24; string_html += " "; hr = hr % 24;
  }
  if (hr > 9)
    string_html += hr;
  else {
    string_html += "0";
    string_html += hr;
  }
  string_html += ":";
  if (min % 60 > 9)
    string_html += min % 60;
  else {
    string_html += "0";
    string_html += min % 60;
  }
  string_html += ":";
  if (sec % 60 > 9)
    string_html += sec % 60;
  else {
    string_html += "0";
    string_html += sec % 60;
  }
  string_html += "</font></h2><hr/>";

  string_html += "<p><h3>Дата: "; string_html += printDate(now); string_html += " "; string_html += "<br />";
  string_html += "Время: "; string_html += printTime(now); string_html += "<br />";
  string_html += "Температура в квартире: "; string_html += meteo.temp_home; string_html += " C</h3></p>";

  string_html += "<p><h3>Температура на улице: "; string_html += meteo.temp_street; string_html += " C<br />";
  string_html += "Атм. давление: "; string_html += meteo.pressure; string_html += " мм<br />";
  string_html += "Температура на улице max: "; string_html += meteo.temp_street_max; string_html += " C<br />";
  string_html += "Температура на улице min: "; string_html += meteo.temp_street_min; string_html += " C</h3></p>";

  string_html += "<p><h3>Температура в погребке: "; string_html += meteo.temp_cellar; string_html += " C<br />";
  if (digitalRead(SSR40DA_pin)) 
  string_html += "Подогрев: <i><font color=\"green\">Вкл.</font></i><br />";
  else string_html += "Подогрев: <i><font color=\"red\">Выкл.</font></i><br />";
  string_html += "Время работы отопления: <i><font color=\"red\">"; string_html += meteo.time_power; string_html += " Мин</font></i><br />";
  string_html += "Расход эл. энергии: <i><font color=\"red\">"; string_html += meteo.kwt_full; string_html += " кВт.ч</font></i><br />";
  string_html += "Стоимость эл. энергии: <i><font color=\"red\">"; string_html += (meteo.kwt_full * 2.49); string_html += " руб</font></i></h3></p>";
  string_html += "<p><h3>Количество включений нагревателя: <i><font color=\"purple\">"; string_html += meteo.count_tarn;
  string_html += "</font></i></h3></p><hr/></body></html>";
  s = string_html.c_str();
  server.send (500, "text/html", s);
  delay(1000);
}

void handleNotFound() {
  //digitalWrite ( led_send, 1 );
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }
  server.send ( 404, "text/plain", message );
  digitalWrite ( 0, 0 );
  //digitalWrite ( led_send, 0 );
}

///////////////////////////////////////////////////////////////////////////////////////////
//Проверка состояния батарейки часов
//////////////////////////////////////////////////////////////////////////////////////////
bool check_time_print() {
  RtcDateTime now = Rtc.GetDateTime();
  if (!Rtc.IsDateTimeValid()) // Батарея на устройстве разряжена или даже отсутствует, а линия электропитания отключена
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Warning the battery");
    lcd.setCursor(0, 1);
    lcd.print("is depleted");
    lcd.setCursor(0, 2);
    lcd.print("there is no power");
    lcd.setCursor(0, 3);
    lcd.print("Meteorological Home");
    delay(5000); // Можно добавить вывод строки на сайт
    return false;
  }
  else
  {
    return true;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////
//Работа c SD Card
///////////////////////////////////////////////////////////////////////////////////////////
void record_file() {
  if (millis() - get_write_file > interval_write_file  || get_write_file > millis())

  {
    if (!SD.open("/"))
      return;
    get_write_file = millis();
    RtcDateTime now = Rtc.GetDateTime();
    dir_temp = "";
    record = "";
    //digitalWrite (led_record, 1); //Индикатор работы с SD картой
    lcd.clear();
    if (!SD.exists("DS")) //Если нет корневой папки создаем ее
      SD.mkdir("DS");
    if (SD.exists("DS"))// Если папка создана формируем путь к папке
      dir_temp = "DS/";
    dir_temp += get_dir_name(now) ; // Имя папки название месяца добавляем к пути файла
    if (!SD.exists(dir_temp))
    {
      SD.mkdir(dir_temp);
      lcd.setCursor(0, 1);
      lcd.print(dir_temp);// Выводим путь к файлу
      delay(2000);
    }
    if (SD.exists(dir_temp)) //Если путь есть пишем показания
    {
      sfilename = dir_temp + "/";
      lcd.setCursor(0, 0);
      lcd.print("File write");// Выводим путь к файлу
      delay(2000);
      sfilename += get_file_name(now, 0);
      sfilename.toCharArray(filename, 25);
      if (SD.exists(filename))
      {
        lcd.setCursor(0, 2);
        lcd.print("File exists");
      }
      else
      {
        lcd.setCursor(0, 2);
        lcd.print("File not exists.");
      }
      myFile = SD.open(filename, FILE_WRITE);
      lcd.setCursor(0, 3);
      if (!myFile)
        lcd.print("File open = False");
      else lcd.print("File open = True");
      delay(2000);
      for (int i = 0; i < DS18B20_COUNT; i++)
      {
        float Temp = get_temp(tek_sensor + 1);
        sTemp = String(Temp);
        if (i == 0)lcd.clear();
        lcd.setCursor(0, i);
        lcd.print(tek_sensor + 1);
        lcd.setCursor(2, i);
        lcd.print("DS18B20: ");
        lcd.setCursor(11, i);
        lcd.print(sTemp);
        record += printTime(now);
        record += String(" DS ");
        record += String(tek_sensor + 1);
        record += ": ";
        record += sTemp;
        record += "\n";
        tek_sensor = (tek_sensor + 1) % DS18B20_COUNT; // Выбор текущего датчика
      }
      record += "("; //эксперемнтальная
      record += meteo.time_power; // Время работы нагревателя минуты
      record += ",";
      record += meteo.kwt_full; // Расход электроэнергии
      record += ",";
      record += meteo.temp_street_max; // Время работы нагревателя
      record += ",";
      record += meteo.temp_street_min; // Время работы нагревателя
      record += ")"; //эксперемнтальная строка
      myFile.println(record);// запись данных в файл
      delay(4000);
      myFile.close();
      lcd.setCursor(0, 2);
      lcd.print("File Record TRUE");
      lcd.setCursor(0, 3);
      lcd.print("File Close");
      delay(2000);
      //digitalWrite (led_record, 0);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////////////////
//Работа с датчиком температуры и давления BMP 280
///////////////////////////////////////////////////////////////////////////////////////////
void printBME280Data(Stream * client) {
  float temp(NAN), hum(NAN), pres(NAN);
  BME280::TempUnit tempUnit(BME280::TempUnit_Celcius);
  BME280::PresUnit presUnit(BME280::PresUnit_Pa);
  bme.read(pres, temp, hum, tempUnit, presUnit);
  meteo.temp_home = temp - 2;
  meteo.pressure = pres * 0.00750063755419211;
  client->print("Temp: ");
  client->print(temp);
  client->println(" " + String(tempUnit == BME280::TempUnit_Celcius ? 'C' : 'F'));
  client->print("Pressure: ");
  client->print(pres * 0.00750063755419211);
  client->println(" mm");
}
///////////////////////////////////////////////////////////////////////////////////////////
//Функция выводит текущую дату
///////////////////////////////////////////////////////////////////////////////////////////
String printDate(const RtcDateTime & dt) {
  char datestring[12];
  snprintf_P(datestring,
             countof(datestring),
             PSTR("%02u/%02u/%04u"),
             dt.Day(),
             dt.Month(),
             dt.Year());
  return datestring;
}
///////////////////////////////////////////////////////////////////////////////////////////
//Функция выводит текущее время
///////////////////////////////////////////////////////////////////////////////////////////
String printTime(const RtcDateTime & dt) {
  char timestring[12];
  snprintf_P(timestring,
             countof(timestring),
             PSTR("%02u:%02u:%02u"),
             dt.Hour(),
             dt.Minute(),
             dt.Second() );
  return timestring;
}
///////////////////////////////////////////////////////////////////////////////////////////
//Функция выводит показания с датчика DS18B20
///////////////////////////////////////////////////////////////////////////////////////////
float get_temp(int n) {
  byte i;
  byte present = 0;
  byte data[12];
  byte addr[8];
  float temp_ds;
  ds.reset();
  ds.select(my_addr[n - 1]);
  //  ds.write(0x44, 1);       // start conversion, with parasite power on at the end
  ds.write(0x44);      // start conversion, with parasite power on at the end // Убрали 1 скорее всего это было причиной зависания так как единица при паразитивном питании
  delay(750);     // maybe 750ms is enough, maybe not
  present = ds.reset();
  ds.select(my_addr[n - 1]);
  ds.write(0xBE);          // Читаем данные с датчика
  for ( i = 0; i < 9; i++) {         // 9 байт
    data[i] = ds.read();
  }
  int16_t raw = (data[1] << 8) | data[0];
  byte cfg = (data[4] & 0x60);
  // at lower res, the low bits are undefined, so let's zero them
  if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
  else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
  else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
  temp_ds = (float)raw / 16.0;


  if (n == 2) //Датчик в погребе индекс для переопределения датчика поменять с 2 на 1
    meteo.temp_cellar = temp_ds;
  else {
    meteo.temp_street = temp_ds;
    if (meteo.temp_street_max < meteo.temp_street)
      meteo.temp_street_max = meteo.temp_street;
    if (meteo.temp_street_min > meteo.temp_street)
      meteo.temp_street_min = meteo.temp_street;
  }
  return temp_ds;
}
///////////////////////////////////////////////////////////////////////////////////////////
//Функция выполняет поиск датчиков DS18B20 ROM пишем в массив
//////////////////////////////////////////////////////////////////////////////////////////
void Find_DS18b20() {
  lcd.clear();
  for (int i = 0; i < DS18B20_COUNT; i++)
  {
    if (!ds.search(my_addr[i]))
    {
      Serial.println("ErrInit1w");//инициализация не выполнена:DS18B20 не найдены
      lcd.setCursor(0, i);
      lcd.print("Err Init DS18B20");
      delay(5000);
      return;
    }
    else //датчики найдены
    {
      lcd.setCursor(0, 0);
      lcd.print(i + 1);
      lcd.setCursor(2, 0);
      lcd.print("DS18B20");
      lcd.setCursor(10, 0);
      lcd.print("Init Done");
      if (OneWire::crc8(my_addr[i], 7) != my_addr[i][7])
      {
        Serial.println("ErrCRC");//CRC Failed!
        return;
      }
      if (my_addr[i][0] != 0x28)
      {
        Serial.println("notDS18B20");//"Устройство не DS18B20!"
      }
      else
      {
        lcd.setCursor(0, i + 1);
        lcd.print("ROM = ");
        for (int j = 0; j < 8; j++)
        {
          lcd.setCursor(6 + j, i + 1);
          lcd.print(my_addr[i][j]);
        }
      }
    }
    delay(2000);
  }
}
////////////////////////////////////////////////////////////////////////////////////////////
// Инитциализация переменных с SD карты
///////////////////////////////////////////////////////////////////////////////////////////
void initializing_after_reset() {
  if (!SD.open("/")) // Если нет SD Card выходим из функции
    return;
  char find_str[250]; // Строка с параметрами для инициализации
  RtcDateTime now = Rtc.GetDateTime();
  dir_temp = "";
  String temp_sfilename = "";
  float x;
  char myStr_temp[20];
  //String str;
  int index_in_str;
  int index_out_str;
  //digitalWrite (led_record, 1); //Индикатор работы с SD картой
  lcd.clear();

  if (!SD.exists("DS")) { //Проверяем наличие корневой папки
    lcd.setCursor(0, 0);
    lcd.print("No find dir DS");
    delay(2000);
    return; // Нет корневой паки выходим из функции
  }
  // Если папка создана формируем путь к папке
  // Начинаем поиск с текущего месяца
  for (int i = 1 ; i <= now.Month() ; i++) {
    dir_temp = "DS/";
    dir_temp += get_dir_name(now, i) ; // Имя папки название месяца добавляем к пути файла
    if (!SD.exists(dir_temp)) {
      lcd.setCursor(0, 0);
      lcd.print("Find Dir...");
    }
    else {
      sfilename = dir_temp + "/";
      lcd.setCursor(0, 0);
      lcd.print("Find Dir True");
      break; // Нашли папку выходим из цикла
    }
  }
  if (!SD.exists(dir_temp)) { // Не нашли подпаку выходим из функции
    lcd.setCursor(0, 0);
    lcd.print("No find Dir...");
    delay(2000);
    return;
  }
  lcd.setCursor(0, 1);
  lcd.print(sfilename);// Выводим путь к файлу если нашли папку

  for (int i = 0 ; i < now.Day(); i++) { // Начинаем поиск файла
    temp_sfilename = sfilename;
    temp_sfilename += get_file_name(now, i);
    temp_sfilename.toCharArray(filename, 25);
    if (SD.exists(filename)) {
      lcd.setCursor(0, 2);
      lcd.print(filename); // Выводим на экран имя файла
      myFile = SD.open(filename, FILE_READ);
      lcd.setCursor(0, 3);
      if (myFile) { //Если файл открыт
        lcd.print("File open = True");
        int n = myFile.size();
        if (n < 200) continue; // Если файл меньше 200 байт продолжаем поиск файла
        for (int j = 1; j <= n / 200; j++) { // Читаем файл блоками по 200 байт
          Serial.println(n);
          Serial.println(myFile.seek(n - (200 * j)));
          for (int k = 0; k < n; k++) {
            if (myFile.available()) {
              find_str[k] = myFile.read();
            }
          }// For читаем файл блоками
          Serial.println("Read Massive true");
          Serial.println(n);
          Serial.println("________________________________");
          String str(find_str); // Преобразуем символьный массив в строку
          Serial.println("Stroka IN");
          int len_s = str.length(); // Длинна строки без символа окончания строки
          Serial.print("Lenght = ");
          Serial.println(len_s);//
          index_in_str = str.lastIndexOf('(');// Поиск символа с конца строки
          index_out_str = str.lastIndexOf(')');// Поиск символа с конца строки
          if (index_in_str == -1 || index_out_str == -1 ) continue; // Если индекс не найден продолжаем читать файл
          Serial.print("Index string in '(' = ");
          Serial.println(index_in_str);
          Serial.print("Index string out ')' = ");
          Serial.println(index_out_str);
          if (index_in_str < index_out_str) break; // Если все верно покидаем цикл
          Serial.println("Find str");

        } //Нашли все что нужно
        // Считаем что получили подстроку и все данные
        Serial.println ("Substring");
        String str(find_str);
        String temp_str = str.substring(index_in_str, index_out_str + 1); // выводим подстроку без скобок
        Serial.print("Lenght podstoki = ");
        Serial.println(temp_str.length()); //Длинна строки без символа кокончания строки
        Serial.print("Is Substroka = ");
        Serial.println(temp_str); // Выводим подстроку

        char myStr[temp_str.length() + 1];
        temp_str.toCharArray(myStr, temp_str.length() + 1); // Пишем в символьный массив субстроку
        Serial.println(temp_str.length() + 1);

        // Разбираем подстроку
        delay(2000);
        lcd.clear();
        for (int count = 0, m = 1, h = 0; m <= temp_str.length(); m++) {
          if (myStr[m] != ',' && myStr[m] != ')')
            Serial.println(myStr_temp[h++] = myStr[m]);
          else {
            count++;
            switch (count) {
              case 1: // Время работы нагревателя минуты
                meteo.time_power = atof(myStr_temp);  //  преобразование в float
                Serial.println(x);

                lcd.setCursor(0, 0);
                lcd.print("Time power:");

                lcd.setCursor(11, 0);
                lcd.print(meteo.time_power);// Выводим данные на экран
                memset(myStr_temp, ' ', 12); // Очищаем 12 элементов массива
                h = 0;

                break;
              case 2: // Расход электроэнергии
                meteo.kwt_full = atof(myStr_temp);  //  преобразование в float
                Serial.println(meteo.kwt_full);
                lcd.setCursor(0, 1);
                lcd.print("Kw-hr:");

                lcd.setCursor(6, 1);
                lcd.print(meteo.kwt_full);// Выводим данные на экран
                memset(myStr_temp, ' ', 12);
                h = 0;

                break;
              case 3:
                meteo.temp_street_max = atof(myStr_temp);  //  преобразование в float
                Serial.println(meteo.temp_street_max);
                lcd.setCursor(0, 2);
                lcd.print("Temp max:");

                lcd.setCursor(9, 2);
                lcd.print(meteo.temp_street_max);// Выводим данные на экран
                memset(myStr_temp, ' ', 12);
                h = 0;

                break;
              case 4:
                meteo.temp_street_min = atof(myStr_temp);  //  преобразование в float
                Serial.println(meteo.temp_street_min);
                lcd.setCursor(0, 3);
                lcd.print("Temp min:");

                lcd.setCursor(9, 3);
                lcd.print(meteo.temp_street_min);// Выводим данные на экран
                memset(myStr_temp, ' ', 12);
                h = 0;
                break;
              default  :
                break;
            }
          }
        }
        delay(5000);
        Serial.println("Stroka END");
        Serial.println("________________________________");
        myFile.close();
        return; // Выход из функции
      }   // Если файл открыт
    } //Если файл существует
  }// For поиск файла
  Serial.println(meteo.time_power);
  Serial.println(meteo.kwt_full);
  Serial.println(meteo.temp_street_max);
  Serial.println(meteo.temp_street_min);
  Serial.println("End function initializing_after_reset");
}
//////////////////////////////////////////////////////////////////////////////////////////////
//// Инитциализация переменных с PHP Server
/////////////////////////////////////////////////////////////////////////////////////////////
void initializing_PHP() {
  // проверяем, подключен ли клиент:
  WiFiClient client = server2.available();
  if (!client) {
    return;
  }

  // ждем, когда клиент отправит какие-нибудь данные:
  Serial.println("new client");  //  "новый клиент"
  while (!client.available()) {
    delay(1);
  }

  // считываем первую строчку запроса:
  String request = client.readStringUntil('\r');
  Serial.println(request);
  client.flush();

  // обрабатываем запрос:
  int value = LOW;
  if (request.indexOf("/LED=ON") != -1) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("GPIO ON");
    delay(1000);
    //digitalWrite(0, 1);
    value = HIGH;
    lcd.backlight();
  }
  if (request.indexOf("/LED=OFF") != -1) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("GPIO OFF");
    delay(1000);
    digitalWrite(0, 0);
    value = LOW;
    lcd.noBacklight();
  }

  // выставляем значение на ledPin в соответствии с запросом:
  //digitalWrite(ledPin, value);

  // возвращаем ответ:
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");  //  "Тип контента:
  //  text/html "
  client.println("");  //  не забываем это...
  client.println("<!DOCTYPE HTML>");
  client.println("<html>");
client.println("<head><meta charset=\"utf-8\"></head>");
  client.print("Подсветка LCD Дисплея: ");  //  "Контакт светодиода теперь
  //  в состоянии: "

  if (value == HIGH) {
    client.print("Вкл");   //  "Вкл"
  } else {
    client.print("Выкл");  //  "Выкл"
  }
  client.println("<br><br>");
  client.println("Нажмите <a href=\"/LED=ON\">здесь</a> чтобы включить подсветку<br>");  //  "Кликните тут, чтобы включить светодиод
  //  на контакте 2"
  client.println("Нажмите <a href=\"/LED=OFF\">здесь</a> чтобы выключить подсветку дисплея<br>");     //  "Кликните тут, чтобы выключить светодиод
  //  на контакте 2"
  client.println("</html>");
  delay(1);
  Serial.println("Client disconnected");  //  "Клиент отключен"
  Serial.println("");

} // В разработке
