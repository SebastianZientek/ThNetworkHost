#pragma once

#include <NTPClient.h>
#include <WiFiUdp.h>

#include "Config.hpp"
#include "EspNow.hpp"
#include "ReadingsStorage.hpp"
#include "WebView.hpp"
#include "boardsettings.hpp"

class App
{
    enum class Status
    {
        OK,
        FAIL
    };

public:
    App();
    void init();
    void update();

private:
    Status systemInit();
    Status initSD();
    Status readConfig();
    Status saveExampleConfig();
    Status connectWiFi();
    void sendEvent(float temp, float hum, MacAddr mac, unsigned long epochTime);

    WebView m_web{boardsettings::serverPort};
    WiFiUDP m_ntpUDP{};
    NTPClient m_timeClient{m_ntpUDP};
    EspNow m_espNow{m_timeClient};
    Config m_config{};
    ReadingsStorage m_readings{};
};
