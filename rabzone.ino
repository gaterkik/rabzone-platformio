#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <Wire.h>                                               // Для работы с шиной I2C
#include <APDS9930.h>                                           // Для работы с датчиком APDS-9930 
#include <Ethernet.h>

APDS9930 apds = APDS9930();                                     // Определяем объект apds, экземпляр класса APDS9930


//--------------------------------------------------------------НАСТРОЙКИ
const char* ssid = "gwfap";
const char* password = "06081987";
#define proximityIntHigh 40                                     // Определяем переменную для хранения верхнего порога приближения, ниже которого прерывания выводиться не будут
#define proximityIntLow 0                                       // Определяем переменную для хранения нижнего  порога приближения, выше которого прерывания выводиться не будут
#define timeOutRange 1000                                       // интервал для игнора повторного поднесения милисекунд
#define timeOutBounce 60000                                      // интервал для сброса счетчика ложных прерываний милисекунд
#define maxBounce 4                                            // Количество прерываний меньше которого считаем что ложняк
#define relayPin 13                                             // Пин для реле
#define pinINT 2                                                // Определяем № вывода Arduino к которому подключен вывод INT датчика
const char* host = "http://admin:accesscode@192.168.88.77/dev/sps/io/rabzonekey/on";                           // строка для отправки в локсон при срабатывании датчика

//----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

WiFiServer server(80);                                          // Создаем сервер
WiFiClient client;

String header;                                                  // Переменная для хранения http запроса
String debugonline;
float    lightAmbient = 0;                                      // Определяем переменную для хранения освещённости общей    в люксах
char lightAmbientString[6];

byte flgCounter = 0;

// Объявляем выводы, флаги и функции для прерываний:            //
uint8_t  numINT;                                                // Объявляем переменную для хранения № внешнего прерывания для вывода pinINT
bool     flgINT;                                                // Объявляем флаг указывающий на то, что сработало прерывание
void     funINT(void) {
  flgINT = 1; // Определяем функцию, которая будет устанавливать флаг flgINT при каждом её вызове
  flgCounter ++;
}

// Объявляем переменные:                                        //
uint16_t proximityData    = 0;                                  // Определяем переменную для хранения значения приближения

void ICACHE_RAM_ATTR funINT ();                                 //Для работы прерывания

unsigned long timerRange;                                       // таймер для игнора повторного поднесения
unsigned long timerBounce;                                      // таймер для игнора ложняков


uint8_t relayState = LOW;                                       // этой переменной устанавливаем состояние реле


void setup() {
  Serial.begin(115200);
  Serial.println("Booting");  //  "Загрузка"
  //-----------------------------------------------------------------------------WiFi connect
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    //  "Соединиться не удалось! Перезагрузка..."
    delay(5000);
    ESP.restart();
  }
  //-----------------------------------------------------------------------------OTA
  // строчка для номера порта по умолчанию
  // можно вписать «8266»:
  // ArduinoOTA.setPort(8266);

  // строчка для названия хоста по умолчанию;
  // можно вписать «esp8266-[ID чипа]»:
  ArduinoOTA.setHostname("rabzone");

  // строчка для аутентификации
  // (по умолчанию никакой аутентификации не будет):
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]() {
    Serial.println("Start");  //  "Начало OTA-апдейта"
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");  //  "Завершение OTA-апдейта"
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    //  "Ошибка при аутентификации"
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    //  "Ошибка при начале OTA-апдейта"
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    //  "Ошибка при подключении"
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    //  "Ошибка при получении данных"
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    //  "Ошибка при завершении OTA-апдейта"
  });
  ArduinoOTA.begin();
  Serial.println("Ready");  //  "Готово"
  Serial.print("IP address: ");  //  "IP-адрес: "
  Serial.println(WiFi.localIP());

  //---------------------------------------------------------------------------------------- I2C
  //  Подготавливаем переменные и функции для прерываний:         //
  pinMode(pinINT, INPUT_PULLUP);                                     // Переводим вывод pinINT в режим входа
  numINT = digitalPinToInterrupt(pinINT);                     // Определяем № внешнего прерывания для вывода pinINT
  attachInterrupt(numINT, funINT, FALLING);                   // Задаём функцию funINT для обработки прерывания numINT. FALLING значит, что функция funINT будет вызываться при каждом спаде уровня сигнала на выводе pinINT с «1» в «0».
  if (numINT >= 0) {                                          // Если у вывода pinINT есть внешнее прерывание, то ...
    Serial.println("Pin interrupt OK!");                  // Выводим сообщение об успешном выборе вывода прерывания
  } else {
    Serial.println("Pin interrupt ERROR!"); // Иначе, выводим сообщение об ошибке выбранного вывода прерывания
  }
  //
  //  Инициируем работу датчика:                                  //
  if (apds.init()) {                                          // Если инициализация прошла успешно, то ...
    Serial.println("apds Initialization OK!");                 // Выводим сообщение об успешной инициализации датчика
  } else {
    Serial.println("apds Initialization ERROR!"); // Иначе, выводим сообщение об ошибке инициализации датчика
  }

  //  Устанавливаем коэффициент усиления приёмника:               // Доступные значения: 1х, 2х, 4х, 8х (PGAIN_1X, PGAIN_2X, PGAIN_4X, PGAIN_8X). Чем выше коэффициент тем выше чувствительность
  if (apds.setProximityGain(PGAIN_1X)) {                      // Если установлен коэффициент усиления приёмника в режиме определения расстояния, то ...
    Serial.println("Set gain OK!");                       // Выводим сообщение об успешной установке коэффициента усиления приёмника
  } else {
    Serial.println("Set gain ERROR!"); // Иначе, выводим сообщение об ошибке при установке коэффициента усиления приёмника
  }
  // Прочитать установленный коэффициент усиления приёмника можно так: uint8_t i = apds.getProximityGain(); // в переменную i сохранится значение: PGAIN_1X, или PGAIN_2X, или PGAIN_4X, или PGAIN_8X
  //  Устанавливаем силу тока драйвера ИК-светодиода:             // Доступные значения: 100мА, 50мА, 25мА, 12.5мА (LED_DRIVE_100MA, LED_DRIVE_50MA, LED_DRIVE_25MA, LED_DRIVE_12_5MA). Чем выше сила тока, тем выше чувствительность.
  if (apds.setProximityDiode(LED_DRIVE_25MA)) {               // Если установлена сила тока драйвера (яркость) ИК-светодиода для обнаружения приближения, то ...
    Serial.println("Set LED drive OK!");                  // Выводим сообщение об успешной установке силы тока драйвера
  } else {
    Serial.println("Set LED drive ERROR!"); // Иначе, выводим сообщение об ошибке при установке силы тока драйвера
  }
  // Прочитать установленную силу тока можно так: uint8_t i = apds.getProximityDiode(); // в переменную i сохранится значение: LED_DRIVE_100MA, или LED_DRIVE_50MA, или LED_DRIVE_25MA, или LED_DRIVE_12_5MA
  //
  //  Разрешаем режим определения освещённости:                   //
  if (apds.enableLightSensor(false)) {                        // Если режим определения освещённости запущен (false - без прерываний на выходе INT), то ...
    Serial.println("Start light sensor OK!");             // Выводим сообщение об успешном запуске режима определения освещённости
  } else {
    Serial.println("Start light sensor ERROR!"); // Иначе, выводим сообщение об ошибке запуска режима определения освещённости
  }

  //  Устанавливаем нижний порог определения приближения:         // Значения приближения выше данного порога не будут приводить к возникновению прерываний на выводе INT
  if (apds.setProximityIntLowThreshold(proximityIntLow)) {    // Если установлен нижний порог прерываний, то ...
    Serial.println("Set proximity low OK!");              // Выводим сообщение об успешной установке нижнего порога
  } else {
    Serial.println("Set proximity low ERROR!"); // Иначе, выводим сообщение об ошибке при установке нижнего порога
  }
  // Прочитать нижний установленный порог можно так: int i; bool j = apds.getProximityIntLowThreshold(i); // в переменную i запишется порог, а в переменную j результат выполнения чтения (true/false)
  //  Устанавливаем верхний порог определения приближения:        // Значения приближения ниже данного порога не будут приводить к возникновению прерываний на выводе INT
  if (apds.setProximityIntHighThreshold(proximityIntHigh)) {  // Если установлен верхний порог прерываний, то ...
    Serial.println("Set proximity high OK!");             // Выводим сообщение об успешной установке верхнего порога
  } else {
    Serial.println("Set proximity high ERROR!"); // Иначе, выводим сообщение об ошибке при установке верхнего порога
  }
  // Прочитать верхний установленный порог можно так: int i; bool j = apds.getProximityIntHighThreshold(i); // в переменную i запишется порог, а в переменную j результат выполнения чтения (true/false)
  // Запретить режим определения освещённости можно так: bool j = apds.disableLightSensor(); // в переменную j сохранится результат выполнения функции (true/false)
  //  Разрешаем режим определения приближения:                    //
  if (apds.enableProximitySensor(false)) {                    // Если режим определения близости запущен (false - без прерываний на выходе INT), то ...
    Serial.println("Start Proximity sensor OK!");         // Выводим сообщение об успешном запуске режима определения близости
  } else {
    Serial.println("Start Proximityght sensor ERROR!"); // Иначе, выводим сообщение об ошибке запуска режима определения близости
  }

  //  Запрет или разрешение прерываний при определения приближения//
  //              apds.setProximityIntEnable(false);              // Запрет     разрешённых ранее прерываний от механизма определения приближения. Данная функция, как и представленные выше, так же возвращает true при успехе и false при неудаче
  apds.setProximityIntEnable(true);                 // Разрешение запрещённых ранее прерываний от механизма определения приближения. Данная функция, как и представленные выше, так же возвращает true при успехе и false при неудаче
  //  uint8_t i = apds.getProximityIntEnable();                   // Чтение     разрешены ли прерывания от механизма определения приближения. В переменную i запишется значение 0 или 1
  //

  timerRange = millis();                                        // старт таймера на периодическое считывание дистанции
  timerBounce = millis();                                        // старт таймера на антидребезг
  pinMode(relayPin, OUTPUT);                                    // Переводим pinOUT в рижим выхода


  //  Ждём завершение инициализации и калибровки:                 //
  delay(500);                                                 //

  //---------------------------------------------------------------------------------------- Start the server
  server.begin();
  Serial.println("Server started");
}


void getAmbient() {                                             //  Читаем значения освещённости в переменные:
  if (apds.readAmbientLightLux (lightAmbient)                   // Если прочитано значение общей освещённости в люксах
      && apds.readProximity       (proximityData)   ) {           // И    прочитано значение близости
    Serial.println((String) "Ambient=" + lightAmbient + " lx,proximity" + proximityData + ", "); // Выводим все прочитанные значения
  }
  else {
    Serial.println("Read light ERROR!");
  }                 // Иначе, выводим сообщение об ошибке чтения освещённости

  dtostrf(lightAmbient, 2, 2, lightAmbientString);

}

void loop() {
  ArduinoOTA.handle();

  MDNS.update();

  WiFiClient client = server.available();

  if (client) {
    Serial.println("New client");                                 //  "Новый клиент"
    boolean blank_line = true;                                    // создаем переменную типа «boolean», чтобы определить конец HTTP-запроса:
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        header += c;

        if (c == '\n' && blank_line) {
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");
          client.println();

          if (header.indexOf("GET /relay/on") >= 0) {
            relayState = HIGH;
            digitalWrite(relayPin, relayState);
          }
          else if (header.indexOf("GET /relay/off") >= 0) {
            relayState = LOW;
            digitalWrite(relayPin, relayState);
          }
          else if (header.indexOf("GET /getambient") >= 0) {
            getAmbient();
            client.print(lightAmbientString);
          }
          break;
        }
        if (c == '\n') {                                              // если обнаружен переход на новую строку:
          blank_line = true;
        }
        else if (c != '\r') {                                         // если в текущей строчке найден символ:
          blank_line = false;
        }
      }
    }
  }
  header = "";                                                        //Стираем переменную
  delay(1);
  client.stop();                                                      // закрываем соединение с клиентом:

  if ( (millis() - timerBounce) > timeOutBounce) {                    // если прошло время с прошлого переключения
    flgCounter = 0;                                                   // счётчик прерываний сбрасываем
    timerBounce = millis();                                           // сброс таймера интервала антидребезга
  }

  if (flgINT) {                                                       // Если установлен флаг flgINT (указывающий о том, что сработало прерывание),
    flgINT = 0;                                                       // то сбрасываем его и ...

    //  Читаем определённое датчиком значение приближения:            //
    if (apds.readProximity(proximityData)) {                          // Если значение приближения корректно прочитано в переменную proximityData, то ..
      Serial.println((String) "Proximity=" + proximityData);          // Выводим значение приближения
      if ( (millis() - timerRange) > timeOutRange) {                  // если прошло время с прошлого переключения
        Serial.println("RELAY");                                      // Делаем переключение
        if (flgCounter > maxBounce) {                                 // Если счетчик срабатывания прерываний превысил пороговое значение
          flgCounter = 0;                                             //  счётчик прерываний сбрасываем и далее переключаем реле
          if (relayState == LOW) {
            relayState = HIGH;
          }
          else {
            relayState = LOW;
          }
          digitalWrite(relayPin, relayState);                            // устанавливаем состояния выхода, чтобы включить или выключить реле

          if (client.connect(host, 80)) {                              //  " подключено"


            client.print(String("GET /dev/sps/io/rabzonekey/on") + " HTTP/1.1\r\n" + //  "Отправка запроса"
                         "Host: " + host + "\r\n" + //  "Хост: "
                         "Connection: close\r\n" + //  "Соединение: закрыто"
                         "\r\n"
                        );
            //  "Ответ:"
            while (client.connected()) {
              if (client.available()) {
                Serial.println("line");
              }
            }
            client.stop();
            Serial.println("\n[Disconnected]"); //  "Отключено"
          }
          else {
            Serial.println("connection failed!]");
            //  "подключиться не удалось!"
            client.stop();
          }
          timerRange = millis();

        }
      }
    }
    else {
      Serial.println("Reading proximity value ERROR!");                // Иначе, выводим сообщение об ошибке чтения приближения
    }

    //      Сообщаем модулю, сбросить прерывание с выхода INT:         //
    if (!apds.clearProximityInt()) {                                   // Если модуль НЕ сбросил прерывание с выхода INT после его установки как реакцию на приближение, то ...
      Serial.println("Сlearing interrupt ERROR!");                     // Выводим сообщение о том, что прерывание не сброшено
    }
  }
}
