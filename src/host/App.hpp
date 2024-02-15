#pragma once

#include <ESPAsyncWebServer.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

#include <memory>

#include "ConfStorage.hpp"
#include "EspNowPairingManager.hpp"
#include "EspNowServer.hpp"
#include "LedIndicator.hpp"
#include "ReadingsStorage.hpp"
#include "Timer.hpp"
#include "WebPageMain.hpp"
#include "WebWifiConfig.hpp"
#include "adapters/esp32/IWifi32Adp.hpp"
#include "adapters/esp32/IArduino32Adp.hpp"

class App
{
    enum class State
    {
        OK,
        FAIL,
        WIFI_CONFIGURATION_NEEDED
    };

    enum class Mode
    {
        SENSOR_HOST,
        WIFI_SETTINGS
    };

    struct WebViewComponentTypes
    {
        using AsyncWebSvrType = AsyncWebServer;
        using AsyncEventSrcType = AsyncEventSource;
        using AsyncWebSrvReq = AsyncWebServerRequest;
        using AsyncEventSrcClient = AsyncEventSourceClient;
        using AsyncWebParam = AsyncWebParameter;

        enum ReqMethod
        {
            GET = HTTP_GET,
            POST = HTTP_POST
        };
    };

    using WebWifiConfigType = WebWifiConfig<ConfStorage, AsyncWebServer>;

public:
    App() = default;
    void init();
    void update();

private:
    State systemInit();
    State initConfig();
    State connectWiFi();
    void wifiSettingsMode();
    void setupButtons();
    bool isWifiButtonPressed();
    bool isPairButtonPressed();

    constexpr static auto m_ledIndicatorPin = 23;
    constexpr static auto wifiButton = 14;
    constexpr static auto pairButton = 18;
    constexpr static auto wifiConfigServerTimeoutMillis = 1000 * 60 * 10;  // 10 minutes
    constexpr static auto espNowPairingTimeout = 1000 * 60 * 2;            // 2 minutes

    Mode m_mode = Mode::SENSOR_HOST;

    std::shared_ptr<LedIndicator> m_ledIndicator{};
    std::shared_ptr<ConfStorage> m_confStorage{};
    std::unique_ptr<WebPageMain> m_webPageMain{};
    std::unique_ptr<WebWifiConfigType> m_webPageMainWifiConfig{};
    WiFiUDP m_ntpUDP{};
    std::shared_ptr<NTPClient> m_timeClient{};
    std::shared_ptr<EspNowPairingManager> m_pairingManager{};
    std::unique_ptr<EspNowServer> m_espNow{};
    std::shared_ptr<IWifi32Adp> m_wifiAdp{};
    std::shared_ptr<IArduino32Adp> m_arduinoAdp;
    ReadingsStorage m_readingsStorage{};
};
