#include <WiFi.h>
#include <Arduino.h>
#include "secrets.h"
#include "net.h"

enum NetState
{
    NET_DISCONNECTED,
    NET_CONNECTING,
    NET_CONNECTED
};

static NetState netstate = NET_DISCONNECTED;

static const int TIMEOUT_MS = 15000;
static const int RETRY_MS = 5000;
static unsigned long start_connect = 0;
static unsigned long retry_timer = 0;

void networkInit()
{
    retry_timer = millis() - RETRY_MS;
    WiFi.mode(WIFI_STA);
}
bool isOnline(){
    return (WiFi.status() == WL_CONNECTED);
}

void updateNetwork()
{
    switch (netstate)
    {
    case NET_DISCONNECTED:
        if (millis() - retry_timer >= RETRY_MS)
        {
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            start_connect = millis();
            netstate = NET_CONNECTING;
        }
        break;
    case NET_CONNECTING:
        if (isOnline())
        {
            Serial.print("Connected IP: ");
            Serial.println(WiFi.localIP());
            netstate = NET_CONNECTED;
            
        }
        else if (millis() - start_connect >= TIMEOUT_MS)
        {
            WiFi.disconnect();
            retry_timer = millis();
            Serial.println("Wi-Fi timed out, retrying");
            netstate = NET_DISCONNECTED;
            
        }
        break;
    case NET_CONNECTED:
        if (!isOnline())
        {
            retry_timer = millis();
            Serial.println("Wi-Fi lost");
            netstate = NET_DISCONNECTED;
        }
        break;
    }
}

