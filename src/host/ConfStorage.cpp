#include "ConfStorage.hpp"

#include <SPIFFS.h>

#include <algorithm>
#include <optional>

#include "RaiiFile.hpp"
#include "common/logger.hpp"

ConfStorage::ConfStorage()
{
    SPIFFS.begin(true);
}

ConfStorage::State ConfStorage::load()
{
    if (!SPIFFS.exists(configFilePath))
    {
        logger::logInf("File not exists, setting and saving defaults");
        reset();
    }
    else
    {
        logger::logInf("Loading configuration from: %s", configFilePath);

        RaiiFile configFile(SPIFFS, configFilePath);
        std::string data = configFile->readString().c_str();
        try
        {
            m_jsonData = nlohmann::json::parse(data);
        }
        catch (nlohmann::json::parse_error err)
        {
            logger::logErr("Can't parse json data, %s", err.what());
            reset();

            return State::FAIL;
        }
    }

    logger::logInf("Config: %s", m_jsonData.dump());

    return State::OK;
}

ConfStorage::State ConfStorage::save()
{
    try
    {
        RaiiFile configFile(SPIFFS, configFilePath, FILE_WRITE);
        auto data = m_jsonData.dump();
        configFile->print(data.c_str());
    }
    catch (nlohmann::json::type_error err)
    {
        logger::logErr("Can't dump json data of configuration file, %s", err.what());
        return State::FAIL;
    }

    return State::OK;
}

ConfStorage::State ConfStorage::reset()
{
    logger::logInf("Rewriting default config");
    setDefaultData();
    return save();
}

void ConfStorage::setWifiConfig(std::string ssid, std::string pass)
{
    m_jsonData["wifi"]["ssid"] = ssid;
    m_jsonData["wifi"]["pass"] = pass;
}

std::pair<std::string, std::string> ConfStorage::getCredentials()
{
    return {m_jsonData["user"], m_jsonData["passwd"]};
}

std::optional<std::pair<std::string, std::string>> ConfStorage::getWifiConfig()
{
    try
    {
        std::string ssid = m_jsonData["wifi"]["ssid"];
        std::string pass = m_jsonData["wifi"]["pass"];

        return std::make_pair(ssid, pass);
    }
    catch (nlohmann::json::type_error err)
    {
        return std::nullopt;
    }

    return std::nullopt;
}

std::string ConfStorage::getSensorName(IDType identifier)
{
    return m_jsonData["sensors"][identifier];
}

std::string ConfStorage::getSensorIDsToNamesJsonStr()
{
    return m_jsonData["sensors"].dump();
}

std::size_t ConfStorage::getSensorUpdatePeriodMins()
{
    return 1;
}

std::size_t ConfStorage::getServerPort()
{
    return 80;
}

nlohmann::json ConfStorage::getConfigWithoutCredentials()
{
    nlohmann::json dataWithoutCred = m_jsonData;
    dataWithoutCred.erase("user");
    dataWithoutCred.erase("passwd");

    return dataWithoutCred;
}

void ConfStorage::setDefaultData()
{
    m_jsonData["user"] = "admin";
    m_jsonData["passwd"] = "passwd";
    m_jsonData["sensors"] = {};
    m_jsonData["serverPort"] = 80;
    m_jsonData["sensorUpdatePeriodMins"] = 1;

    // TODO: STUB, remove after implementation ready
    m_jsonData["sensors"] = {{"2506682365", "Some sensor name"}};
}
