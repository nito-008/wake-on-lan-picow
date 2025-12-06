#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <FreeRTOS.h>
#include <queue.h>
#include <WiFi.h>
#include "secrets.h"
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <stdarg.h>
#include <stdio.h>
#include "certs.h"

WiFiClientSecure esp_client;
PubSubClient mqtt_client(esp_client);
const int mqtt_port = 8883;

// MQTT topics
const char *MQTT_DEBUG_TOPIC = "/debug";
const char *MQTT_WAKEUP_TOPIC = "/wakeup";
const char *MQTT_STATE_TOPIC_SUFFIX = "/state";

const char *MQTT_ACTIVE_MESSAGE = "active";
const char *MQTT_INACTIVE_MESSAGE = "inactive";

const bool MQTT_RETAINED = true;

// DB keys
const char *DB_DEVICE_NAME_KEY = "DeviceName";
const char *DB_MAC_ADDRESS_KEY = "MacAddress";
const char *DB_IP_ADDRESS_KEY = "IPAddress";

JsonDocument doc;
JsonArray device_list;

unsigned long lastCheckTime = 0;
const unsigned long CHECK_INTERVAL = 5000; // MillSeconds
const unsigned long PING_TIMEOUT = 1000;
unsigned long pingCount = 0;

WiFiUDP udp;
IPAddress broadcastIp = IPAddress(255, 255, 255, 255);

struct PingMessage
{
  char deviceName[64];
  int resultMs;
};

QueueHandle_t pingQueue;

void mqttCallback(char *topic, byte *payload, unsigned int length);

void connectToWifi()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Wifi connecting...");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected");
}

void connectToMQTT()
{
  esp_client.setCACert(ca_cert);

  mqtt_client.setServer(MQTT_BROKER, mqtt_port);
  mqtt_client.setKeepAlive(60);
  mqtt_client.setCallback(mqttCallback);

  while (!mqtt_client.connected())
  {
    String client_id = "raspberry_pi_picow_client_" + String(WiFi.macAddress());
    Serial.printf("Connecting to MQTT Broker as %s...\n", client_id.c_str());
    if (!mqtt_client.connect(client_id.c_str(), MQTT_USERNAME, MQTT_PASS))
    {
      Serial.print("Failed to connect to MQTT broker, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" Retrying in 5 seconds.");
      delay(5000);
    }
  }

  Serial.println("Connected to MQTT broker");
  mqtt_client.subscribe(MQTT_WAKEUP_TOPIC);
  mqtt_client.publish(MQTT_DEBUG_TOPIC, "Connected to MQTT broker");
}

void setUpOTA()
{
  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {  // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type); });
  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    } });
  ArduinoOTA.begin();
}

void updateDeviceList()
{
  Serial.println("Getting device list from Cloudflare D1");

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("No wifi connection");
    return;
  }

  if (!mqtt_client.connected())
  {
    Serial.println("No mqtt connection");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;

  if (!https.begin(client, DB_API_URL))
  {
    Serial.println("Failed to begin https connection");
    return;
  }

  https.addHeader("x-api-key", DB_API_KEY);
  https.addHeader("Content-Type", "application/json");

  int httpCode = https.GET();

  if (httpCode > 0)
  {
    if (httpCode == HTTP_CODE_OK)
    {
      String payload = https.getString();
      Serial.println(payload);
      Serial.print("Deserializing json...");

      DeserializationError error = deserializeJson(doc, payload);

      if (doc.is<JsonArray>())
      {
        device_list = doc.as<JsonArray>();
      }

      if (error)
      {
        Serial.println("Failed to deserialize json");
        Serial.println(error.c_str());
        Serial.println(error.f_str());
      }
      else
      {
        Serial.println("Successfully deserialized json");
        mqtt_client.publish(MQTT_DEBUG_TOPIC, "Successfully got device list");
      }
    }
  }
  else
  {
    Serial.printf("Error: %s\n", https.errorToString(httpCode).c_str());
  }

  https.end();
}

void checkDevicesStatus()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("No wifi connection");
    return;
  }

  Serial.println("Sending ping to all devices");

  for (JsonObject device : device_list)
  {
    const char *ip_address = device[DB_IP_ADDRESS_KEY];
    if (ip_address == nullptr)
    {
      Serial.println("no ip address");
      continue;
    }
    Serial.printf("IP Address: %s ", ip_address);

    const char *device_name = device[DB_DEVICE_NAME_KEY];
    if (device_name == nullptr)
    {
      Serial.println("no device name");
      continue;
    }
    Serial.printf("Device Name: %s ...", device_name);

    int result = -1;
    result = WiFi.ping(ip_address);
    if (result >= 0)
    {

      Serial.printf("Success, Time: %d ms\n", ip_address, result);
    }
    else
    {
      Serial.printf("Failed\n", ip_address);
    }

    PingMessage msg;
    sprintf(msg.deviceName, device_name);
    msg.resultMs = result;
    xQueueSend(pingQueue, &msg, portMAX_DELAY);
  }
}

void parseMacAddress(const char *str, uint8_t *mac)
{
  int values[6];
  if (sscanf(str, "%x:%x:%x:%x:%x:%x",
             &values[0], &values[1], &values[2],
             &values[3], &values[4], &values[5]) == 6)
  {
    for (int i = 0; i < 6; i++)
    {
      mac[i] = (uint8_t)values[i];
    }
  }
}

void sendMagicPacket(const char *mac_address)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("No wifi connection");
    return;
  }

  uint8_t packet[102];

  for (int i = 0; i < 6; i++)
  {
    packet[i] = 0xFF;
  }

  uint8_t targetMac[6];
  parseMacAddress(mac_address, targetMac);

  for (int i = 0; i < 16; i++)
  {
    memcpy(&packet[(i + 1) * 6], targetMac, 6);
  }

  Serial.print("Sending Magic Packet to ");
  Serial.println(broadcastIp);

  udp.beginPacket(broadcastIp, 9);
  udp.write(packet, sizeof(packet));
  udp.endPacket();

  Serial.println("Magic Packet Sent!");
}

void mqttCallback(char *topic, uint8_t *payload, unsigned int length)
{
  Serial.print("Message received on topic: ");
  Serial.println(topic);
  Serial.print("Message: ");

  char message[128];
  if (length >= sizeof(message))
  {
    length = sizeof(message) - 1;
  }
  memcpy(message, payload, length);
  message[length] = '\0';
  Serial.print(message);
  Serial.println("\n-----------------------");

  if (strcmp(topic, MQTT_WAKEUP_TOPIC) == 0)
  {
    for (JsonObject device : device_list)
    {
      const char *device_name = device[DB_DEVICE_NAME_KEY];
      if (device_name != nullptr && strcmp(device_name, message) == 0)
      {
        const char *mac_address = device[DB_MAC_ADDRESS_KEY];
        if (mac_address == nullptr)
        {
          Serial.println("no device name");
          continue;
        }
        Serial.printf("Mac Address: %s ...", mac_address);

        sendMagicPacket(mac_address);
      }
      else
      {
        Serial.println("Device name no match");
      }
    }
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Serial OK.");

  connectToWifi();
  connectToMQTT();
  setUpOTA();
  updateDeviceList();

  WiFi.setTimeout(PING_TIMEOUT);

  pingQueue = xQueueCreate(10, sizeof(PingMessage));
  if (pingQueue == NULL)
  {
    Serial.println("Error creating the queue");
  }
}

// MQTT & Arduino OTA
void loop()
{
  ArduinoOTA.handle();

  if (!mqtt_client.connected())
  {
    connectToMQTT();
  }
  mqtt_client.loop();

  PingMessage msg;
  if (xQueueReceive(pingQueue, &msg, 0) == pdTRUE)
  {
    char state_topic[64];
    char payload[64];

    sprintf(state_topic, "/%s%s", msg.deviceName, MQTT_STATE_TOPIC_SUFFIX);

    if (msg.resultMs >= 0)
    {
      mqtt_client.publish(state_topic, MQTT_ACTIVE_MESSAGE, MQTT_RETAINED);
    }
    else
    {
      mqtt_client.publish(state_topic, MQTT_INACTIVE_MESSAGE, MQTT_RETAINED);
    }

    char message[64];
    sprintf(message, "Ping count...%d", pingCount++);
    mqtt_client.publish(MQTT_DEBUG_TOPIC, message);
  }
}

void setup1()
{
}

// Ping
void loop1()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    return;
  }

  if (millis() - lastCheckTime > CHECK_INTERVAL)
  {
    lastCheckTime = millis();
    checkDevicesStatus();
  }
}