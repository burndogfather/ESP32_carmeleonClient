#include "carmeleonClient.h"


static void ethernetMaintainTask(void* pvParameters) {
  while (true) {
    Ethernet.maintain();
    vTaskDelay(pdMS_TO_TICKS(300));  // 300ms 간격
  }
}

carmeleonClient::carmeleonClient() {
  // Ethernet 유지관리 백그라운드 태스크 생성
  xTaskCreate(
    ethernetMaintainTask,   // 태스크 함수
    "EthMaintain",          // 태스크 이름
    2048,                   // 스택 크기
    nullptr,                // 인자
    1,                      // 우선순위
    nullptr                 // 핸들 저장 안 함
  );
}


