#include "App.hpp"

#include <Arduino.h>
#include <SPIFFS.h>

#include <algorithm>
#include <memory>
#include <numeric>

#include "ConfStorage.hpp"
#include "EspNowPairingManager.hpp"
#include "LedIndicator.hpp"
#include "RaiiFile.hpp"
#include "Resources.hpp"
#include "WebPageMain.hpp"
#include "adapters/esp32/Arduino32Adp.hpp"
#include "adapters/esp32/EspNow32Adp.hpp"
#include "adapters/esp32/Wifi32Adp.hpp"
#include "common/MacAddr.hpp"
#include "common/logger.hpp"
#include "common/types.hpp"
#include "webserver/WebServer.hpp"

void App::init()
{
    m_wifiAdp = std::make_shared<Wifi32Adp>();
    m_arduinoAdp = std::make_shared<Arduino32Adp>();

    if (auto initState = systemInit(); initState == State::FAIL)
    {
        constexpr auto msInSecond = 1000;
        constexpr auto waitBeforeRebootSec = 5;

        logger::logErr("System will be rebooted in %ds", waitBeforeRebootSec);
        delay(waitBeforeRebootSec * msInSecond);
        ESP.restart();
    }
    else if (initState == State::WIFI_CONFIGURATION_NEEDED)
    {
        wifiSettingsMode();
    }
    else
    {
        auto newReadingCallback = [this](float temp, float hum, IDType identifier)
        {
            m_readingsStorage.addReading(identifier, temp, hum, m_timeClient->getEpochTime());

            auto reading = m_readingsStorage.getLastReadingAsJsonStr(identifier);
            m_webPageMain->sendEvent(reading.c_str(), "newReading", millis());
        };

        m_espNow->init(newReadingCallback, m_confStorage->getSensorUpdatePeriodMins());

        auto getSensorData = [this](const std::size_t &identifier)
        {
            auto data = m_readingsStorage.getReadingsAsJsonStr(identifier);
            return data;
        };

        m_webPageMain->startServer(getSensorData);

        // TODO: STUB, remove after implementation ready
        m_confStorage->addSensor(2506682365, "Some sensor name");
    }
}

void App::update()
{
    static decltype(millis()) wifiModeStartTime = 0;
    if (m_mode != Mode::WIFI_SETTINGS && isWifiButtonPressed())
    {
        wifiModeStartTime = millis();
        wifiSettingsMode();
    }

    if (m_mode == Mode::WIFI_SETTINGS
        && millis() > wifiConfigServerTimeoutMillis + wifiModeStartTime)
    {
        logger::logInf("Wifi configuration timeout. Reboot...");
        ESP.restart();
    }

    if (isPairButtonPressed())
    {
        m_pairingManager->enablePairingForPeriod();
    }

    m_pairingManager->update();
    m_ledIndicator->update();
}

App::State App::systemInit()
{
    // Let the board be electrically ready before initialization
    constexpr auto waitBeforeInitializationMs = 1000;
    delay(waitBeforeInitializationMs);
    setupButtons();

    m_ledIndicator = std::make_shared<LedIndicator>(m_arduinoAdp, m_ledIndicatorPin);
    m_ledIndicator->switchOn(false);

    logger::init();
    if (auto state = initConfig(); state != State::OK)
    {
        return state;
    }
    if (auto state = connectWiFi(); state != State::OK)
    {
        return state;
    }

    m_timeClient = std::make_shared<NTPClient>(m_ntpUDP);
    m_timeClient->begin();
    m_timeClient->update();

    auto espNowAdp = std::make_unique<EspNow32Adp>();

    m_pairingManager = std::make_unique<EspNowPairingManager>(m_confStorage, m_arduinoAdp, m_ledIndicator);
    m_espNow = std::make_unique<EspNowServer>(std::move(espNowAdp), m_pairingManager, m_wifiAdp);
    m_webPageMain = std::make_unique<WebPageMain>(std::make_unique<WebServer>(),
                                                  std::make_unique<Resources>(), m_confStorage);

    return State::OK;
}

App::State App::initConfig()
{
    SPIFFS.begin(true);
    RaiiFile configFile(SPIFFS.open("/config.json"));

    m_confStorage = std::make_shared<ConfStorage>();
    auto state = m_confStorage->load(configFile);

    if (state == ConfStorage::State::FAIL)
    {
        logger::logWrn("File not exists, setting and saving defaults");
        m_confStorage->setDefault();
        if (m_confStorage->save(configFile) == ConfStorage::State::FAIL)
        {
            logger::logErr("Can't save settings");
            return State::FAIL;
        }
    }

    return State::OK;
}

App::State App::connectWiFi()
{
    logger::logInf("Connecting to WiFi");

    constexpr auto connectionRetriesBeforeRebootMs = 10;
    constexpr auto delayBetweenConnectionTiresMs = 1000;
    constexpr auto waitBeforeRebootMs = 1000;

    m_wifiAdp->setMode(Wifi32Adp::Mode::AP_STA);

    auto wifiConfig = m_confStorage->getWifiConfig();
    if (!wifiConfig)
    {
        logger::logWrn("No wifi configuration!");
        return State::WIFI_CONFIGURATION_NEEDED;
    }

    auto [ssid, pass] = wifiConfig.value();
    m_wifiAdp->init(ssid, pass);

    uint8_t wifiConnectionTries = 0;
    while (m_wifiAdp->getStatus() != Wifi32Adp::Status::CONNECTED)
    {
        if (isWifiButtonPressed())
        {
            return State::WIFI_CONFIGURATION_NEEDED;
        }

        wifiConnectionTries++;
        delay(delayBetweenConnectionTiresMs);
        logger::logInf(".");

        if (wifiConnectionTries >= connectionRetriesBeforeRebootMs)
        {
            logger::logErr("WiFi connection issue, reboot.");
            delay(waitBeforeRebootMs);
            ESP.restart();
        }
    }

    logger::logInf("Connected to %s IP: %s MAC: %s, channel %d", m_wifiAdp->getSsid(),
                   m_wifiAdp->getLocalIp(), m_wifiAdp->getMacAddr(), m_wifiAdp->getChannel());

    return State::OK;
}

void App::wifiSettingsMode()
{
    logger::logInf("Wifi settings mode");
    m_ledIndicator->switchOn(true);

    m_mode = Mode::WIFI_SETTINGS;
    if (m_espNow)
    {
        m_espNow->deinit();
    }
    if (m_webPageMain)
    {
        m_webPageMain->stopServer();
    }
    m_wifiAdp->disconnect();

    m_webPageMainWifiConfig
        = std::make_unique<WebWifiConfigType>(m_wifiAdp, std::make_unique<Resources>());
    m_webPageMainWifiConfig->startConfiguration(m_confStorage);
}

void App::setupButtons()
{
    m_arduinoAdp->pinMode(wifiButton, Arduino32Adp::Mode::PIN_INPUT_PULLUP);
    m_arduinoAdp->pinMode(pairButton, Arduino32Adp::Mode::PIN_INPUT_PULLUP);
}

bool App::isWifiButtonPressed()
{
    return digitalRead(wifiButton) == LOW;
}

bool App::isPairButtonPressed()
{
    return digitalRead(pairButton) == LOW;
}
