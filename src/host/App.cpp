#include "App.hpp"

#include <algorithm>
#include <memory>
#include <numeric>

#include "common/MacAddr.hpp"
#include "common/logger.hpp"
#include "common/types.hpp"

void App::init()
{
    return;
    if (auto initStatus = systemInit(); initStatus == Status::FAIL)
    {
        constexpr auto msInSecond = 1000;
        constexpr auto waitBeforeRebootSec = 5;

        logger::logErr("System not initialized properly. Reboot in %ds", waitBeforeRebootSec);
        delay(waitBeforeRebootSec * msInSecond);
        m_espAdp->restart();
    }
    else if (initStatus == Status::WIFI_CONFIGURATION_NEEDED)
    {
        wifiSettingsMode();
    }
    else
    {
        m_webPageMain = std::make_unique<WebPageMain>(
            m_arduinoAdp, std::make_shared<WebServer>(m_confStorage->getServerPort()),
            std::make_unique<Resources>(), m_confStorage);

        auto newReadingCallback = [this](float temp, float hum, IDType identifier)
        {
            m_readingsStorage.addReading(identifier, temp, hum, m_timeClient->getEpochTime());

            auto reading = m_readingsStorage.getLastReadingAsJsonStr(identifier);
            m_webPageMain->sendEvent(reading.c_str(), "newReading", m_arduinoAdp->millis());
        };

        m_espNow->init(newReadingCallback);

        auto getSensorData = [this](const std::size_t &identifier)
        {
            auto data = m_readingsStorage.getReadingsAsJsonStr(identifier);
            return data;
        };

        m_webPageMain->startServer(getSensorData);
    }

    logger::logInf("System initialized");
}

void App::update()
{
    switch (m_state)
    {
    case State::INITIALIZATION_BASIC_COMPONENTS:
        logger::init();
        setupButtons();
        m_ledIndicator->switchOn(false);

        m_state = State::LOADING_CONFIGURATION;
        break;

    case State::LOADING_CONFIGURATION:
        logger::logDbg("Loading configuration");

        if (auto status = m_confStorage->load(); status == ConfStorage::State::FAIL)
        {
            logger::logWrn("Configuration file not exists, using default values");
        }

        m_state = State::CONNECTING_TO_WIFI;
        break;

    case State::CONNECTING_TO_WIFI:
        logger::logDbg("Connecting to WiFi");

        if (auto status = connectWiFi(); status == Status::OK)
        {
            m_state = State::STARTING_SERVERS;
        }
        else if (status == Status::WIFI_CONFIGURATION_NEEDED)
        {
            m_state = State::HOSTING_WIFI_CONFIGURATION;
        }
        else
        {
            m_state = State::ERROR_REBOOTING;
        }
        break;

    case State::HOSTING_WIFI_CONFIGURATION:

        break;

    case State::STARTING_SERVERS:
        logger::logDbg("Starting servers");

        m_timeClient->begin();
        m_timeClient->update();

        {
            m_webPageMain = std::make_unique<WebPageMain>(
                m_arduinoAdp, std::make_shared<WebServer>(m_confStorage->getServerPort()),
                std::make_unique<Resources>(), m_confStorage);

            auto newReadingCallback = [this](float temp, float hum, IDType identifier)
            {
                m_readingsStorage.addReading(identifier, temp, hum, m_timeClient->getEpochTime());

                auto reading = m_readingsStorage.getLastReadingAsJsonStr(identifier);
                m_webPageMain->sendEvent(reading.c_str(), "newReading", m_arduinoAdp->millis());
            };

            m_espNow->init(newReadingCallback);

            auto getSensorData = [this](const std::size_t &identifier)
            {
                auto data = m_readingsStorage.getReadingsAsJsonStr(identifier);
                return data;
            };

            m_webPageMain->startServer(getSensorData);
        }
        m_state = State::RUNNING;
        break;

    case State::RUNNING:
        break;
    }

    static decltype(m_arduinoAdp->millis()) wifiModeStartTime = 0;
    if (m_mode != Mode::WIFI_SETTINGS && isWifiButtonPressed())
    {
        wifiModeStartTime = m_arduinoAdp->millis();
        wifiSettingsMode();
    }

    static decltype(m_arduinoAdp->millis()) resetStarted = 0;
    if (isWifiButtonPressed())
    {
        if (resetStarted == 0)
        {
            resetStarted = m_arduinoAdp->millis();
        }
        else if (m_arduinoAdp->millis() - resetStarted > m_resetToFactorySettings)
        {
            logger::logWrn("Reset to factory settings!");
            m_confStorage->setDefault();
            m_confStorage->save();
            m_espAdp->restart();
        }
    }
    else
    {
        resetStarted = 0;
    }

    if (m_mode == Mode::WIFI_SETTINGS
        && m_arduinoAdp->millis() > m_wifiConfigServerTimeoutMillis + wifiModeStartTime)
    {
        logger::logInf("Wifi configuration timeout. Reboot...");
        m_espAdp->restart();
    }

    if (isPairButtonPressed())
    {
        m_pairingManager->enablePairingForPeriod();
    }

    m_pairingManager->update();
    m_ledIndicator->update();
}

App::Status App::systemInit()
{
    // Let the board be electrically ready before initialization
    constexpr auto waitBeforeInitializationMs = 1000;
    delay(waitBeforeInitializationMs);
    logger::init();

    setupButtons();
    m_ledIndicator->switchOn(false);

    if (auto status = initConfig(); status != Status::OK)
    {
        return status;
    }
    if (auto status = connectWiFi(); status != Status::OK)
    {
        return status;
    }

    m_timeClient->begin();
    m_timeClient->update();

    return Status::OK;
}

App::Status App::initConfig()
{
    auto status = m_confStorage->load();
    if (status == ConfStorage::State::FAIL)
    {
        logger::logWrn("Configuration file not exists, using default values");
    }

    return Status::OK;
}

App::Status App::connectWiFi()
{
    logger::logInf("Connecting to WiFi");

    m_wifiAdp->setMode(Wifi32Adp::Mode::AP_STA);

    auto wifiConfig = m_confStorage->getWifiConfig();
    if (!wifiConfig)
    {
        logger::logWrn("No wifi configuration!");
        return Status::WIFI_CONFIGURATION_NEEDED;
    }

    auto [ssid, pass] = wifiConfig.value();
    m_wifiAdp->init(ssid, pass);

    uint8_t wifiConnectionTries = 0;
    while (m_wifiAdp->getStatus() != Wifi32Adp::Status::CONNECTED)
    {
        if (isWifiButtonPressed())
        {
            return Status::WIFI_CONFIGURATION_NEEDED;
        }

        wifiConnectionTries++;
        delay(m_delayBetweenConnectionRetiresMs);
        logger::logInf(".");

        if (wifiConnectionTries >= m_connectionRetriesBeforeRebootMs)
        {
            logger::logWrn("WiFi connection issue, reboot.");
            delay(m_onErrorWaitBeforeRebootMs);
            m_espAdp->restart();
        }
    }

    logger::logInf("Connected to %s IP: %s MAC: %s, channel %d", m_wifiAdp->getSsid(),
                   m_wifiAdp->getLocalIp(), m_wifiAdp->getMacAddr(), m_wifiAdp->getChannel());

    return Status::OK;
}

void App::wifiSettingsMode()
{
    logger::logInf("Wifi configuration mode");
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
    m_webWifiConfig->startConfiguration(m_confStorage);
}

void App::setupButtons()
{
    m_arduinoAdp->pinMode(m_wifiButton, Arduino32Adp::Mode::PIN_INPUT_PULLUP);
    m_arduinoAdp->pinMode(m_pairButton, Arduino32Adp::Mode::PIN_INPUT_PULLUP);
}

bool App::isWifiButtonPressed()
{
    return m_arduinoAdp->digitalRead(m_wifiButton) == false;
}

bool App::isPairButtonPressed()
{
    return m_arduinoAdp->digitalRead(m_pairButton) == false;
}
