#ifndef CARMELEON_CLIENT_H
#define CARMELEON_CLIENT_H

#include "Ethernet/EthernetESP32.h"
#include "Http/HttpSecure.h"
#include "HttpsOTAWrapper.h"
#include "Framework.h"

class carmeleonClient {
public:
  EthernetClass Eth;
  HttpSecure Http;

  HttpsOTAWrapper Ota;
  CarmeleonFramework Framework;

  carmeleonClient();


};

#endif