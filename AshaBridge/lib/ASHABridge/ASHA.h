// you wrote separate classes, in future please put it in one class
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <string>

enum class DeviceCategory { Actuator, Sensor };

enum class BusType { Digital, PWM, Analog, I2C, SPI };

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

class ASHA {
   public:
    ASHA_WIFI asha_wifi;
    ASHA_Devices asha_devices;
    std::string init(const std::string& ashaID);
    DeviceType genericDev(DeviceCategory deviceCategory, const std::string& metadata,
                          BusType busType);
    void run();

    static void handleCommand(JsonVariant doc);

   private:
    WiFiClient espClient;
    PubSubClient mqttClient;
    std::string currentAshaID;
    unsigned long lastReconnectAttempt = 0;

    void reconnectMQTT();
    static void mqttCallback(char* topic, byte* payload, unsigned int length);
};
