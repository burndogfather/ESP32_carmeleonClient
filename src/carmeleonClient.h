#ifndef CARMELEON_CLIENT_H
#define CARMELEON_CLIENT_H

#include <Ethernet/EthernetESP32.h>

class carmeleonClient : public EthernetClass {
public:
  carmeleonClient();

  bool connectToServer(const char* host, uint16_t port);
  bool sendPing();
  void loop();
};

#endif