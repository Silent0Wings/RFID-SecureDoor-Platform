#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice device) {
    String name = device.haveName() ? device.getName().c_str() : "(no name)";
    Serial.printf("Found: %s | RSSI: %d | Addr: %s\n",
                  name.c_str(),
                  device.getRSSI(),
                  device.getAddress().toString().c_str());
  }
};


void setup() {
  Serial.begin(115200);
  BLEDevice::init("");
  BLEScan *pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pScan->setActiveScan(true);
  pScan->start(10, false);
}

void loop() {}
