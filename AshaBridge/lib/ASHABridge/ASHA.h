// you wrote separate classes, in future please put it in one class
#include <string>

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
    std::string category;  // Actuator or Sensor
    std::string type;      // LED or Motor
    std::string metadata;  // "My porch fan"
    std::string DorA;      // digital or analog
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
    DeviceType LED(const std::string& metadata, const std::string& DorA);
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
};