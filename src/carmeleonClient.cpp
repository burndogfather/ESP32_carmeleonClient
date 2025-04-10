#include "carmeleonClient.h"

carmeleonClient::carmeleonClient() {
  // 생성자에서 기본 설정 가능
}

bool carmeleonClient::connectToServer(const char* host, uint16_t port) {
  IPAddress ip;
  if (!hostByName(host, ip)) {
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

bool carmeleonClient::sendPing() {
  // 네트워크 상태 확인
  if (linkStatus() != LinkON) {
	Serial.println("네트워크 링크 꺼짐");
	return false;
  }

  // ping처럼 간단한 요청을 보낼 수 있음
  return true;
}

void carmeleonClient::loop() {
  maintain(); // DHCP 리스 연장
}