#include "include/AgnoDuck.h"
#include "../CdpPacket.h"
#include "include/bloomfilter.h"

const int MEMORY_LOW_THRESHOLD = PACKET_LENGTH + sizeof(CdpPacket);
const int NUM_SECTORS = 312; //total desired bits divided by bits per sector
const int NUM_HASH_FUNCS = 2;
const int BITS_PER_SECTOR = 32; //size of unsigned int is 32 bits
const int MAX_MESSAGES = 100;

AgnoDuck::AgnoDuck(String name):
        filter(NUM_SECTORS, NUM_HASH_FUNCS, BITS_PER_SECTOR, MAX_MESSAGES)
{
    duckName = name;
}

AgnoDuck::~AgnoDuck() {
    if (txPacket != NULL) {
        delete txPacket;
    }
    if (rxPacket != NULL) {
        delete rxPacket;
    }
}

void AgnoDuck::setEncrypt(bool state) {
    duckcrypto::setEncrypt(state);
}

bool AgnoDuck::getEncrypt() {
    return duckcrypto::getState();
}

bool AgnoDuck::getDecrypt() {
    return duckcrypto::getDecrypt();
}

void AgnoDuck::setDecrypt(bool state) {
    duckcrypto::setDecrypt(state);
}

void AgnoDuck::setAESKey(uint8_t newKEY[32]) {
    duckcrypto::setAESKey(newKEY);
}

void AgnoDuck::setAESIv(uint8_t newIV[16]) {
    duckcrypto::setAESIV(newIV);
}

void AgnoDuck::encrypt(uint8_t* text, uint8_t* encryptedData, size_t inc) {
    duckcrypto::encryptData(text, encryptedData, inc);
}

void AgnoDuck::decrypt(uint8_t* encryptedData, uint8_t* text, size_t inc) {
    duckcrypto::decryptData(encryptedData, text, inc);
}

int AgnoDuck::setDeviceId(std::vector<byte> id) {
    if (id.size() != DUID_LENGTH) {
        logerr("ERROR  device id too long rc = " + String(DUCK_ERR_NONE));
        return DUCK_ERR_ID_TOO_LONG;
    }

    duid.clear();
    duid.assign(id.begin(), id.end());
    loginfo("setupDeviceId rc = " + String(DUCK_ERR_NONE));
    return DUCK_ERR_NONE;
}

int AgnoDuck::setDeviceId(byte* id) {
    if (id == NULL) {
        return DUCK_ERR_SETUP;
    }
    int len = *(&id + 1) - id;
    if (len > DUID_LENGTH) {
        return DUCK_ERR_ID_TOO_LONG;
    }
    duid.assign(id, id + len);
    loginfo("setupDeviceId rc = " + String(DUCK_ERR_NONE));

    return DUCK_ERR_NONE;
}

int AgnoDuck::setupSerial(int baudRate) {
    // This gives us 10 seconds to do a hard reset if the board is in a bad state after power cycle
    while (!Serial && millis() < 10000)
        ;

    Serial.begin(baudRate);
    loginfo("setupSerial rc = " + String(DUCK_ERR_NONE));
    loginfo("Running CDP Version: " + String(getCDPVersion().c_str()));
    return DUCK_ERR_NONE;
}

// TODO: use LoraConfigParams directly as argument to setupRadio
int AgnoDuck::setupRadio(float band, int ss, int rst, int di0, int di1,
                         int txPower, float bw, uint8_t sf, uint8_t gain) {
    LoraConfigParams config;

    config.band = band;
    config.ss = ss;
    config.rst = rst;
    config.di0 = di0;
    config.di1 = di1;
    config.txPower = txPower;
    config.bw = bw;
    config.sf = sf;
    config.gain = gain;
    config.func = DuckRadio::onInterrupt;
    logwarn_f("~~ band: %d\n", band);
    logwarn_f("~~ config.band: %d\n", config.band);
    int err = duckRadio.setupRadio(config);

    if (err == DUCKLORA_ERR_BEGIN) {
        logerr("ERROR setupRadio. Starting LoRa Failed. rc = " + String(err));
        return err;
    }
    if (err == DUCKLORA_ERR_SETUP) {
        logerr("ERROR setupRadio. Failed to setup Lora. rc = " + String(err));
        return err;
    }
    if (err == DUCKLORA_ERR_RECEIVE) {
        logerr("ERROR setupRadio. Failed to start receive. rc = " + String(err));
        return err;
    }

    txPacket = new DuckPacket(duid);
    rxPacket = new DuckPacket();
    loginfo("setupRadio rc = " + String(DUCK_ERR_NONE));

    return DUCK_ERR_NONE;
}

void AgnoDuck::setSyncWord(byte syncWord) {
    duckRadio.setSyncWord(syncWord);
}

void AgnoDuck::setChannel(int channelNum, bool isEU) {
    duckRadio.setChannel(channelNum, isEU);
}


int AgnoDuck::sendData(byte topic, const String data,
                       const std::vector<byte> targetDevice, std::vector<byte> * outgoingMuid)
{

    const byte* buffer = (byte*)data.c_str();
    int err = sendData(topic, buffer, data.length(), targetDevice, outgoingMuid);
    return err;
}

int AgnoDuck::sendData(byte topic, const std::string data,
                       const std::vector<byte> targetDevice, std::vector<byte> * outgoingMuid)
{
    std::vector<byte> app_data;
    app_data.insert(app_data.end(), data.begin(), data.end());
    int err = sendData(topic, app_data, targetDevice, outgoingMuid);
    return err;
}

int AgnoDuck::sendData(byte topic, const byte* data, int length,
                       const std::vector<byte> targetDevice, std::vector<byte> * outgoingMuid)
{
    std::vector<byte> app_data;
    app_data.insert(app_data.end(), &data[0], &data[length]);
    int err = sendData(topic, app_data, targetDevice, outgoingMuid);
    return err;
}

int AgnoDuck::sendData(byte topic, std::vector<byte> data,
                       const std::vector<byte> targetDevice, std::vector<byte> * outgoingMuid)
{
    if (topic < reservedTopic::max_reserved) {
        logerr("ERROR send data failed, topic is reserved.");
        return DUCKPACKET_ERR_TOPIC_INVALID;
    }
    if (data.size() > MAX_DATA_LENGTH) {
        logerr("ERROR send data failed, message too large: " + String(data.size()) +
               " bytes");
        return DUCKPACKET_ERR_SIZE_INVALID;
    }
    int err = txPacket->prepareForSending(&filter, targetDevice, this->getType(), topic, data);

    if (err != DUCK_ERR_NONE) {
        return err;
    }

    err = duckRadio.sendData(txPacket->getBuffer());

    CdpPacket packet = CdpPacket(txPacket->getBuffer());

    if (err == DUCK_ERR_NONE) {
        filter.bloom_add(packet.muid.data(), MUID_LENGTH);
    }

    if (!lastMessageAck) {
        loginfo("Previous `lastMessageMuid` " + duckutils::toString(lastMessageMuid) +
                " was not acked. Overwriting `lastMessageMuid` with " +
                duckutils::toString(packet.muid));
    }

    lastMessageAck = false;
    lastMessageMuid.assign(packet.muid.begin(), packet.muid.end());
    assert(lastMessageMuid.size() == MUID_LENGTH);
    if (outgoingMuid != NULL) {
        outgoingMuid->assign(packet.muid.begin(), packet.muid.end());
        assert(outgoingMuid->size() == MUID_LENGTH);
    }
    txPacket->reset();

    return err;
}

muidStatus AgnoDuck::getMuidStatus(const std::vector<byte> & muid) const {
    if (duckutils::isEqual(muid, lastMessageMuid)) {
        if (lastMessageAck) {
            return muidStatus::acked;
        } else {
            return muidStatus::not_acked;
        }
    } else if (muid.size() != MUID_LENGTH) {
        return muidStatus::invalid;
    } else {
        return muidStatus::unrecognized;
    }
}

// TODO: implement this using new packet format
bool AgnoDuck::reboot(void*) {
    /*
      String reboot = "REBOOT";
      loginfo(reboot);
      DuckRadio::getInstance()->sendPayloadStandard(reboot, "boot");
      duckesp::restartDuck();
    */
    return true;
}

// TODO: implement this using new packet format
bool AgnoDuck::imAlive(void*) {
    /*
    String alive = "Health Quack";
    loginfo(alive);
    DuckRadio::getInstance()->sendPayloadStandard(alive, "health");
    */
    return true;
}

int AgnoDuck::startReceive() {
    int err = duckRadio.startReceive();
    if (err != DUCK_ERR_NONE) {
        logerr("ERROR recieving packet...");
    }
    return err;
}

int AgnoDuck::sendPong() {
    int err = DUCK_ERR_NONE;
    std::vector<byte> data(1, 0);
    err = txPacket->prepareForSending(&filter, ZERO_DUID, this->getType(), reservedTopic::pong, data);
    if (err != DUCK_ERR_NONE) {
        logerr("ERROR Oops! failed to build pong packet, err = " + err);
        return err;
    }
    err = duckRadio.sendData(txPacket->getBuffer());
    if (err != DUCK_ERR_NONE) {
        logerr("ERROR Oops! Lora sendData failed, err = " + err);
        return err;
    }
    return err;
}

int AgnoDuck::sendPing() {
    int err = DUCK_ERR_NONE;
    std::vector<byte> data(1, 0);
    err = txPacket->prepareForSending(&filter, ZERO_DUID, this->getType(), reservedTopic::ping, data);
    if (err != DUCK_ERR_NONE) {
        logerr("ERROR Failed to build ping packet, err = " + err);
        return err;
    }
    err = duckRadio.sendData(txPacket->getBuffer());
    if (err != DUCK_ERR_NONE) {
        logerr("ERROR Lora sendData failed, err = " + err);
    }
    return err;
}

String AgnoDuck::getErrorString(int error) {
    String errorStr = String(error) + ": ";

    switch (error) {
        case DUCK_ERR_NONE:
            return errorStr + "No error";
        case DUCK_ERR_NOT_SUPPORTED:
            return errorStr + "Feature not supported";
        case DUCK_ERR_SETUP:
            return errorStr + "Setup failure";
        case DUCK_ERR_ID_TOO_LONG:
            return errorStr + "Id length is invalid";
        case DUCK_ERR_OTA:
            return errorStr + "OTA update failure";
        case DUCKLORA_ERR_BEGIN:
            return errorStr + "Lora module initialization failed";
        case DUCKLORA_ERR_SETUP:
            return errorStr + "Lora module configuration failed";
        case DUCKLORA_ERR_RECEIVE:
            return errorStr + "Lora module failed to read data";
        case DUCKLORA_ERR_TIMEOUT:
            return errorStr + "Lora module timed out";
        case DUCKLORA_ERR_TRANSMIT:
            return errorStr + "Lora moduled failed to send data";
        case DUCKLORA_ERR_HANDLE_PACKET:
            return errorStr + "Lora moduled failed to handle RX data";
        case DUCKLORA_ERR_MSG_TOO_LARGE:
            return errorStr + "Attempted to send a message larger than 256 bytes";

        case DUCKWIFI_ERR_NOT_AVAILABLE:
            return errorStr + "Wifi network is not availble";
        case DUCKWIFI_ERR_DISCONNECTED:
            return errorStr + "Wifi is disconnected";
        case DUCKWIFI_ERR_AP_CONFIG:
            return errorStr + "Wifi configuration failed";

        case DUCKDNS_ERR_STARTING:
            return errorStr + "DNS initialization failed";

        case DUCKPACKET_ERR_SIZE_INVALID:
            return errorStr + "Duck packet size is invalid";
        case DUCKPACKET_ERR_TOPIC_INVALID:
            return errorStr + "Duck packet topic field is invalid";
        case DUCKPACKET_ERR_MAX_HOPS:
            return errorStr + "Duck packet reached maximum allowed hops";

        case DUCK_INTERNET_ERR_SETUP:
            return errorStr + "Internet setup failed";
        case DUCK_INTERNET_ERR_SSID:
            return errorStr + "Internet SSID is not valid";
        case DUCK_INTERNET_ERR_CONNECT:
            return errorStr + "Internet connection failed";
    }

    return "Unknown error";
}