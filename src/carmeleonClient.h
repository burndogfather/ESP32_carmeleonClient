#ifndef CARMELEON_CLIENT_H
#define CARMELEON_CLIENT_H

#include <Ethernet/EthernetESP32.h>
#include <Http/HttpSecure.h>
#include "HttpsOTAWrapper.h"

class carmeleonClient {
public:
  EthernetClass Eth;
  HttpSecure Http;
  HttpsOTAWrapper Ota;

  carmeleonClient();

  bool connectToServer(const char* host, uint16_t port);

};

#endif