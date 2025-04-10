#ifndef CARMELEON_CLIENT_H
#define CARMELEON_CLIENT_H

#include <Ethernet/EthernetESP32.h>
#include <https/HttpSecure.h>

class carmeleonClient {
public:
  EthernetClass Ethernet;  // ✅ 내부 멤버로 선언
  HttpSecure http;

  carmeleonClient();

  bool connectToServer(const char* host, uint16_t port);
  void loop();
};

#endif