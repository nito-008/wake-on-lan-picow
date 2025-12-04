#include <Arduino.h>
#include <WiFi.h>
#include "secrets.h"
#include <PubSubClient.h>

WiFiClientSecure esp_client;
PubSubClient mqtt_client(esp_client);
const int mqtt_port = 8883;
const char *mqtt_topic = "/hello";

// Root CA Certificate
// Load DigiCert Global Root G2, which is used by EMQX Public Broker: broker.emqx.io
const char *ca_cert = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)EOF";

void connectToWifi()
{
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.println("connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.println(".");
  }
  Serial.println("connected");
}

void connectToMQTT()
{
  while (!mqtt_client.connected())
  {
    String client_id = "raspberry-pi-picow-client-" + String(WiFi.macAddress());
    Serial.printf("Connecting to MQTT Broker as %s...\n", client_id.c_str());
    if (mqtt_client.connect(client_id.c_str(), MQTT_USERNAME, MQTT_PASS))
    {
      Serial.println("Connected to MQTT broker");
      mqtt_client.subscribe(mqtt_topic);
      mqtt_client.publish(mqtt_topic, "Hi EMQX I'm ESP32 ^^");
    }
    else
    {
      Serial.print("Failed to connect to MQTT broker, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" Retrying in 5 seconds.");
      delay(5000);
    }
  }
}

void setup()
{
  Serial.begin(115200);

  connectToWifi();
  connectToMQTT();
}

void loop()
{
}