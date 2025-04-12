#ifndef CARMELEON_CLIENT_H
#define CARMELEON_CLIENT_H

#include <Ethernet/EthernetESP32.h>
#include <Http/HttpSecure.h>

class carmeleonClient {
public:
  EthernetClass Eth;
  HttpSecure Http;

  carmeleonClient();

  bool connectToServer(const char* host, uint16_t port);
  void loop();
};

#endif