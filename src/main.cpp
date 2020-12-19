#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/queue.h>
#include <AsyncMqttClient.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <credentials.h>

#define SOP '<'
#define EOP '>'
#define DEL ';'

AsyncMqttClient mqttClient;
TimerHandle_t mqttReconnectTimer;
TimerHandle_t wifiReconnectTimer;

QueueHandle_t serialData;
QueueHandle_t tempData;
QueueHandle_t mqttData;

UBaseType_t uxHighWaterMark;

#define SENSORS_NUMBER 16
float tempSet[SENSORS_NUMBER];
int relayState[SENSORS_NUMBER];
bool boot= true;

bool started = false;
bool ended = false;

char inData[80];
int i=0;

String incomingByte;
struct data
{
  uint8_t id;
  float temp;
  char mac[18];
  float voltage;
};
struct mqttdata{
  char topic[100];
  char sentData[20];
};

void sendData(data data){
  const char *testing="lalala/%s/lalala";
  char tempTopic[100];
  char batTopic[100];
  char idTopic[100];
  char temp[10];
  char bat[10];
  char id[10];
  char test[100];
  dtostrf(data.temp,5, 2, temp);
  dtostrf(data.voltage,4, 2, bat);
  sprintf(id,"%d",data.id);
  sprintf(tempTopic,"floor2/%s/temp/current",id);
  sprintf(batTopic,"floor2/%s/bat/state",id);
  sprintf(idTopic,"floor2/%s/id",data.mac);
  sprintf(test,testing,id);
  Serial.printf("\n\nIa aici: %s\n\n",test);
  mqttClient.publish(tempTopic, 0, true, temp);
  mqttClient.publish(batTopic, 0, true, bat);
  mqttClient.publish(idTopic, 0, true, id);
}

void checkTemp(void *some){
  // Serial.printf("On task start: %d", uxTaskGetStackHighWaterMark( NULL ));
  while(1){
    data sensorData;
    int id;
    if( xQueueReceive( tempData, &( sensorData ), ( TickType_t ) 100 ) )
    {
      Serial.printf("ID: %d\tSet temp: %f \t current temp: %f\n",sensorData.id,tempSet[sensorData.id],sensorData.temp);
      id = sensorData.id;
      if (sensorData.temp <= tempSet[id] ) {
        if(relayState[id]==0){
          char actionTopic[100];
          sprintf(actionTopic,"floor2/%d/temp/action",id);
          mqttClient.publish(actionTopic, 0, true, "heating");
          relayState[id]=1;
        }
      }
      if (sensorData.temp > tempSet[id] + 0.5){
        if(relayState[id]==1){
          char actionTopic[100];
          sprintf(actionTopic,"floor2/%d/temp/action",id);
          mqttClient.publish(actionTopic, 0, true, "idle");
          relayState[id]=0;
        }
      }
    }
    vTaskDelay(1000/portTICK_PERIOD_MS);
  }
}

void extractData(void *some){
  while(1){
  char in[80];
  data lala;
  char * strtokIndx;
  if( xQueueReceive( serialData, &( in ), ( TickType_t ) 100 ) )
  {
    Serial.println("got data");
    Serial.println(in);
  strtokIndx = strtok(in,";");
  lala.id = atoi(strtokIndx);
  strtokIndx = strtok(NULL,";");
  strcpy(lala.mac,strtokIndx);
  strtokIndx = strtok(NULL,";");
  lala.temp=atof(strtokIndx);
  strtokIndx = strtok(NULL,";");
  lala.voltage=atof(strtokIndx);
  Serial.printf("ID:%d\nMAC:%s\nTEMP:%f\nVoltage:%f\n",lala.id,lala.mac,lala.temp,lala.voltage);
  sendData(lala);
  xQueueSend(tempData,(void *) &lala, ( TickType_t ) 1000);
  // checkTemp(lala.id,lala.temp);
  }
  vTaskDelay(1000/portTICK_PERIOD_MS);
  }
}

void readFromSerial(void *some){
  int pos=0;
  char buff[30];
   for( ;; ){
    while (Serial2.available() > 0){
      char inChar = Serial2.read();
      if(inChar == SOP)
      {
        i = 0;
        inData[i] = '\0';
        started = true;
        ended = false;
      }
      else if(inChar == EOP)
      {
        ended = true;
        break;
      }
      else
      {
        if(i < 79)
        {
          inData[i] = inChar;
          i++;
          inData[i] = '\0';
        }
      }
    }
    if(started && ended)
    {
      Serial.println(inData);
      // extractData(inData);
      xQueueSend( serialData, ( void * ) &inData, ( TickType_t ) 1000 );

      // The end of packet marker arrived. Process the packet
      // Reset for the next packet
      started = false;
      ended = false;
      i = 0;
      inData[i] = '\0';
    }
    // Serial.println("Delay");
    vTaskDelay(100/portTICK_PERIOD_MS);
  }
  Serial.println("Aici");
  vTaskDelete(NULL);
}

void connectToWifi() {
  Serial.println("Connecting to Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void connectToMqtt() {
  Serial.println("Connecting to MQTT...");
  mqttClient.connect();

}

void WiFiEvent(WiFiEvent_t event) {
    Serial.printf("[WiFi-event] event: %d\n", event);
    switch(event) {
    case SYSTEM_EVENT_STA_GOT_IP:
        Serial.println("WiFi connected");
        Serial.println("IP address: ");
        Serial.println(WiFi.localIP());
        connectToMqtt();
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        Serial.println("WiFi lost connection");
        xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
		xTimerStart(wifiReconnectTimer, 0);
        break;
    }
}

void onMqttConnect(bool sessionPresent) {
  Serial.println("Connected to MQTT.");
  Serial.print("Session present: ");
  Serial.println(sessionPresent);
  for(int a=0; a < SENSORS_NUMBER; a++){
    char tempSetTopic[100];
    sprintf(tempSetTopic,"floor2/%d/temp/set",a);
    if(boot){
      char modeTopic[100];
      sprintf(modeTopic,"floor2/%d/temp/mode",a);
      mqttClient.publish(modeTopic, 0, true, "heat");
    }
    Serial.println(tempSetTopic);
    mqttClient.subscribe(tempSetTopic,2);
  }
  if(boot){
    boot=false;
  }
}



void onMqttDisconnect(AsyncMqttClientDisconnectReason reason) {
  Serial.println("Disconnected from MQTT.");

  if (WiFi.isConnected()) {
    xTimerStart(mqttReconnectTimer, 0);
  }
}

void onMqttSubscribe(uint16_t packetId, uint8_t qos) {
  Serial.println("Subscribe acknowledged.");
  Serial.print("  packetId: ");
  Serial.println(packetId);
  Serial.print("  qos: ");
  Serial.println(qos);
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total){
  Serial.printf("Topic: %s\t\t message: %s\n",topic,payload);

  // int id = atoi(getId(topic));
  // setTemperature(id,payload);
}

void set_defaults(void * some){
  for(uint8_t a=0; a< SENSORS_NUMBER; a++){
    tempSet[a]=20;
    relayState[a]=0;
  }
  vTaskDelete(NULL);
}

void setup() {
  serialData = xQueueCreate(10,sizeof(inData));
  tempData = xQueueCreate(10,sizeof(data));
  mqttData = xQueueCreate(20,sizeof(mqttdata));
  Serial.begin(9600);
  Serial2.begin(9600);
  xTaskCreate(set_defaults, "set defaults", 1000, NULL, 1, NULL);
  xTaskCreate(readFromSerial,"Read from Serial2", 2048, NULL, 1, NULL);
  xTaskCreate(extractData,"Parse Serial", 4096, NULL, 1, NULL);
  xTaskCreate(checkTemp,"Check temp",2048,NULL,1,NULL);
  mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
  wifiReconnectTimer = xTimerCreate("wifiTimer", pdMS_TO_TICKS(2000), pdFALSE, (void*)0, reinterpret_cast<TimerCallbackFunction_t>(connectToWifi));

  WiFi.onEvent(WiFiEvent);

  mqttClient.onConnect(onMqttConnect);
  mqttClient.onDisconnect(onMqttDisconnect);
  // mqttClient.onSubscribe(onMqttSubscribe);
  // mqttClient.onUnsubscribe(onMqttUnsubscribe);
  mqttClient.onMessage(onMqttMessage);
  // mqttClient.onPublish(onMqttPublish);
  // mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setClientId(MQTT_ID);
  mqttClient.setCredentials(MQTT_USER, MQTT_PSWD);
  mqttClient.setServer(MQTT_HOST,MQTT_PORT);

  connectToWifi();
}

void loop() {
  // vTaskDelay(10);
}