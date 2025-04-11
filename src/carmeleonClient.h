#ifndef CARMELEON_CLIENT_H
#define CARMELEON_CLIENT_H

#include <Ethernet/EthernetESP32.h>
#include <Https/HttpSecure.h>

class carmeleonClient {
public:
  EthernetClass Ethernet;
  HttpSecure Http;

  carmeleonClient();

  bool connectToServer(const char* host, uint16_t port);
  void loop();
};

#endif