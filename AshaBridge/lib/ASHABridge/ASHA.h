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
    std::string category; // ACtuator or Sensor
    std::string type; // LED or Motor
    std::string metadata; // "My porch fan"
    std::string DorA; // digital or analog
};


// the structure that holds all registereddevices like sensor etc
struct RegisteredDevice {
    int id;
    DeviceType deviceType;
    int pin;
};


// ACTUATORS
class ASHA_Actuators {
    public:
        DeviceType LED(const std::string& metadata);

};



// ADDING_DEVICE_CONNECTION
class ASHA_Devices {
public:
    int addDevice(const DeviceType& deviceType, int pin);


private:
    static const int MAX_DEVICES = 32;
    RegisteredDevice devices[MAX_DEVICES];
    int count = 0;

};


