// you wrote separate classes, in future please put it in one class
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <Lua.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <map>
#include <string>

enum class DeviceCategory { Actuator, Sensor };

enum class BusType { Digital, PWM, Analog, I2C, SPI, IR };

enum class IRProtocol { Samsung, NEC };

// WIFI
class ASHA_WIFI {
   public:
    void begin(const char* ssid, const char* pwd);
    void onConnect(void (*userdefinedfunc)());

   private:
    void (*userdefinedfunLoc)() = nullptr;
};

// the structure that holds the information for a device type
struct DeviceType {
    DeviceCategory category;  // Actuator or Sensor
    std::string metadata;     // "My porch fan"
    BusType bus;              // digital or analog or I2C or PWM
};

// the structure-type that holds all registereddevices like sensor etc
struct RegisteredDevice {
    int id;
    DeviceType deviceType;
    int pin;
};

// this structure holds details that a lua can use to publish
struct PublishMessage {
    char* topic;
    char* message;
};

// ACTUATORS
class ASHA_Actuators {
   public:
    DeviceType LED(const std::string& metadata, BusType bus);
};

// ADDING_DEVICE_CONNECTION
class ASHA_Devices {
   public:
    int addDevice(const DeviceType& deviceType, int pin);
    int getCount() const {
        return count;
    }
    RegisteredDevice getDevice(int index) const {
        return devices[index];
    }

   private:
    static const int MAX_DEVICES = 30;
    RegisteredDevice devices[MAX_DEVICES];
    int count = 0;
};

class WirelessSender {
   public:
    virtual void send(JsonVariant doc) = 0;
    virtual ~WirelessSender() {}
};

class IRSender : public WirelessSender {
   private:
    IRsend irsend;

   public:
    IRSender(int pin) : irsend(pin) {
        irsend.begin();
    }

    void send(JsonVariant doc) override;
};

class ASHA {
   public:
    ASHA_WIFI asha_wifi;
    ASHA_Devices asha_devices;
    std::string init(const std::string& ashaID);
    DeviceType genericDev(DeviceCategory deviceCategory, const std::string& metadata,
                          BusType busType);
    void run();

    static void handleCommand(JsonVariant doc);
    static ASHA* getInstance() {
        return instance;
    }
    void subscribeTopic(const char* topic);
    void publishLuaMessage(PublishMessage publishMessage);
    const std::string gettopicPayload(const char* topic) {
        return topicPayloads[topic];
    }
    bool getStopLuaRequested() {
        return stopLuaRequested;
    }

   private:
    WiFiClient espClient;
    PubSubClient mqttClient;
    std::string currentAshaID;
    unsigned long lastReconnectAttempt = 0;
    static ASHA* instance;
    Lua ashaLua;
    QueueHandle_t luaScriptQueue;
    QueueHandle_t pendingPublish;
    static void luaTask(void* param);
    unsigned long lastMqttActivity = 0;
    std::map<std::string, std::string> topicPayloads;
    std::map<int, WirelessSender*>
        wirelessSenders;  // using this to map all pins to thier corresponding objects
    QueueHandle_t pendingSubscriptions;
    volatile bool stopLuaRequested = false;

    void reconnectMQTT();
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
};
