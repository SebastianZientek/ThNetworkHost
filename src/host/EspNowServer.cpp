#include "EspNowServer.hpp"

#include <WiFi.h>

#include <array>

#include "NTPClient.h"
#include "adapters/esp32/EspNowAdp.hpp"
#include "common/Messages.hpp"
#include "common/logger.hpp"
#include "common/serializer.hpp"
#include "esp_now.h"

EspNowAdp adp;  // TODO: remove

constexpr auto macSize = 6;
constexpr auto msgSignatureSize = 4;
constexpr std::array<uint8_t, macSize> broadcastAddress{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

EspNowServer::OnSendCb EspNowServer::m_onSend;  // NOLINT
EspNowServer::OnRecvCb EspNowServer::m_onRecv;  // NOLINT

EspNowServer::EspNowServer(std::unique_ptr<IEspNowAdp> espNowAdp,
                           std::shared_ptr<EspNowPairingManager> pairingManager,
                           std::shared_ptr<NTPClient> ntpClient)
    : m_espNowAdp(std::move(espNowAdp))
    , m_ntpClient(ntpClient)
    , m_sensorUpdatePeriodMins(1)
    , m_pairingManager(pairingManager)
{
}

void EspNowServer::init(const NewReadingsCb &newReadingsCb,
                        const NewPeerCb &newPeerCb,
                        uint8_t sensorUpdatePeriodMins)
{
    if (esp_now_init() != ESP_OK)
    {
        logger::logErr("Init ESP NOW issue");
        return;
    }

    m_newReadingsCb = newReadingsCb;
    m_newPeerCb = newPeerCb;
    m_sensorUpdatePeriodMins = sensorUpdatePeriodMins;
    setOnDataRecvCb();
    setOnDataSendCb();
}

void EspNowServer::deinit()
{
    esp_now_deinit();
}

void EspNowServer::onDataRecv(const MacAddr &mac, const uint8_t *incomingData, int len)
{
    auto msgAndSignature = serializer::partialDeserialize<MsgType, Signature>(incomingData, len);

    if (!msgAndSignature)
    {
        logger::logWrn("Can't deserialize received message");
        return;
    }

    auto [msgType, signature] = msgAndSignature.value();

    if (signature != signatureTemplate)
    {
        std::string sigStr(signature.begin(), signature.end());
        std::string templateSigStr(signatureTemplate.begin(), signatureTemplate.end());
        logger::logWrn("Received message with wrong signature");
        logger::logWrn("%s - %s", sigStr, templateSigStr);
        return;
    }

    switch (msgType)
    {
    case MsgType::PAIR_REQ:
    {
        if (m_pairingManager->isPairingEnabled())
        {
            logger::logInf("PAIR_REQ received");
            PairReqMsg pairReqMsg;
            pairReqMsg.deserialize(incomingData, len);
            if (m_newPeerCb(pairReqMsg.ID))
            {
                addPeer(mac, WiFi.channel());
                sendPairOK(mac);
                esp_now_del_peer(mac.data());
            }
        }
        else
        {
            logger::logWrn("Pairing not enabled, request rejected");
        }
    }
    break;
    case MsgType::PAIR_RESP:
        logger::logWrn("Received PAIR_RESP, shouldn't be here.");
        break;
    case MsgType::SENSOR_DATA:
    {
        SensorDataMsg sDataMsg;
        sDataMsg.deserialize(incomingData, len);

        if (m_pairingManager->isPaired(sDataMsg.ID))
        {
            logger::logInf("[%u %s] T: %.1f, H: %.1f", sDataMsg.ID, m_ntpClient->getFormattedTime(),
                           sDataMsg.temperature, sDataMsg.humidity);

            m_newReadingsCb(sDataMsg.temperature, sDataMsg.humidity, sDataMsg.ID,
                            m_ntpClient->getEpochTime());
        }
        else
        {
            logger::logWrn("Ignored data from unpaired sensor, id: %u", sDataMsg.ID);
        }
    }
    break;
    case MsgType::UNKNOWN:
        logger::logWrn("Received UNKNOWN message type.");
    }
}

void EspNowServer::onDataSend(const MacAddr &mac, esp_now_send_status_t status)
{
    logger::logInf("Last Packet Send Status: ");
    if (status == 0)
    {
        logger::logInf("Delivery success: %s", mac.str());
    }
    else
    {
        logger::logWrn("Delivery fail");
    }
}

void EspNowServer::setOnDataSendCb()
{
    m_onSend = [this](const MacAddr &mac, esp_now_send_status_t status)
    {
        this->onDataSend(mac, status);
    };
    auto onDataSend = [](const uint8_t *rawMac, esp_now_send_status_t status)
    {
        MacAddr macAddr{};
        std::memcpy(macAddr.data(), rawMac, MacAddr::macAddrDigits);

        m_onSend(macAddr, status);
    };

    esp_now_register_send_cb(onDataSend);
}

void EspNowServer::setOnDataRecvCb()
{
    m_onRecv = [this](MacAddr mac, const uint8_t *incomingData, int len)
    {
        this->onDataRecv(mac, incomingData, len);
    };
    auto onDataRecv = [](const uint8_t *rawMac, const uint8_t *incomingData, int len)
    {
        MacAddr macAddr{};
        std::memcpy(macAddr.data(), rawMac, MacAddr::macAddrDigits);

        m_onRecv(macAddr, incomingData, len);
    };

    esp_now_register_recv_cb(onDataRecv);
}

void EspNowServer::addPeer(const MacAddr &mac, uint8_t channel)
{
    esp_now_peer_info_t peer = {};
    memcpy(&peer.peer_addr[0], mac.data(), ESP_NOW_ETH_ALEN);
    peer.channel = channel;
    esp_now_add_peer(&peer);
}

void EspNowServer::sendPairOK(const MacAddr &mac) const
{
    auto pairRespMsg
        = PairRespMsg::create(static_cast<uint8_t>(WiFi.channel()), m_sensorUpdatePeriodMins);
    WiFi.softAPmacAddress(pairRespMsg.hostMacAddr.data());
    auto buffer = pairRespMsg.serialize();

    auto state = esp_now_send(mac.data(), buffer.data(), buffer.size());

    if (state != ESP_OK)
    {
        logger::logWrn("esp_now_send error, code: %d", state);
    }
}
