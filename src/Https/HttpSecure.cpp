#ifndef CARMELEON_CLIENT_H
#define CARMELEON_CLIENT_H

#include <Arduino.h>
#include <EthernetESP32.h>
#include <EthernetClient.h>
#include "../https/carmeleonHttpSecure.h"

class carmeleonClient {
public:
  EthernetClass Ethernet;
  EthernetClient netClient;
  carmeleonHttpSecure http;

  carmeleonClient();
  void init(EthDriver& driver);
  bool begin(uint8_t* mac);
  void loop();
};

#endif