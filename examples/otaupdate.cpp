#include <Arduino.h>
#include <carmeleonClient.h>

#define BULTIN_LED 2

carmeleonClient carmeleon;

//ENC28J60Driver driver;
//EMACDriver driver(ETH_PHY_LAN8720);
W5500Driver driver;
/*
3v3-3.3v
GND-GND
D5-SCS
D18-SCLK
D23-MOSI
D19-MISO
D34-RST
*/

const char* UserAgent = "CARMELEON_CLIENT";
byte mac[] = { 0x1A, 0xAA, 0xBB, 0xCC, 0x00, 0x01 };  // 맥주소
IPAddress dns1(168, 126, 63, 1); // DNS정보 (KT)
IPAddress dns2(1, 1, 1, 1); //DNS2차정보 (cloudflare)

void setup() {

    pinMode(BULTIN_LED, OUTPUT);
    digitalWrite(BULTIN_LED, LOW);

    Serial.begin(115200);
    while (!Serial);

    //네트워크 칩관련 설정
    carmeleon.Eth.init(driver); 

    carmeleon.Eth.setHostname("helloworld"); //네트워크상에 출력하는 호스트네임 지정
    carmeleon.Eth.setDNS(dns1, dns2); //DNS서버 지정

    Serial.println("Ethernet연결 시도 중...");
    //이더넷 연결시작 (맥주소)
    while (carmeleon.Eth.begin(mac) == 0) {
        Serial.println("Ethernet연결 재시도!");
        delay(50);
    }

    Serial.println("NTP서버 동기화중 : "); 
    carmeleon.Eth.setNTP("time.bora.net"); //NTP서버 지정 및 시간정보 동기화 실행
    time_t now = time(nullptr);
    Serial.printf("NTP 동기화완료(KST): %s", ctime(&now));





    carmeleon.Ota.onConnected([]() {
        Serial.println(" + OTA서버와 연결완료!");
    });
    carmeleon.Ota.onWriting([]() {
        Serial.println(" ++ 펌웨어쓰기 시작");
    });
    carmeleon.Ota.onSuccess([]() {
        Serial.println("OTA성공! ESP.restart()으로 재부팅하세요!");
    });
    carmeleon.Ota.onFail([](String msg) {
        Serial.println("OTA실패 : "+msg);
    });
    if(carmeleon.Ota.begin("https://raw.githubusercontent.com/burndogfather/ESP32_carmeleonClient/refs/heads/master//esp32_blink_firmware.bin")){
        Serial.println("OTA완료! 리셋해주세요!");
    }else{
        Serial.println("OTA업데이트 실패");
    }

}

void loop(){


}
  