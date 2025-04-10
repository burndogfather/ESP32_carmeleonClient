#include "carmeleonClient.h"

carmeleonClient::carmeleonClient() {
  // 생성자에서 특별히 할 일 없음
}

bool carmeleonClient::connectToServer(const char* host, uint16_t port) {
  IPAddress ip;
  if (!Ethernet.hostByName(host, ip)) {
    Serial.println("DNS 실패");
    return false;
  }

  EthernetClient client;
  if (!client.connect(ip, port)) {
    Serial.println("서버 연결 실패");
    return false;
  }

  Serial.println("서버 연결 성공");
  client.stop();
  return true;
}

void carmeleonClient::loop() {
  Ethernet.maintain();
}