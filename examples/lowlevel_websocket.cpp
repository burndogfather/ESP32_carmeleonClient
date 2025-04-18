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





    if(carmeleon.Http.begin("wss://도메인/ws/test")){
        Serial.println("웹소켓 준비 됨");
    }else{
        Serial.println("웹소켓 준비 실패");
    }

    carmeleon.Http.onHandshake([]() {
        Serial.println("✅ 웹소켓 연결됨!");
    });

    carmeleon.Http.onDisconnected([]() {
        Serial.println("❌ 웹소켓 끊김!");
    });

    carmeleon.Http.onMsgString([](String msg) {
        Serial.printf("📨 문자열 수신: %s\n", msg.c_str());

        if( msg == "hi" ){
            Serial.printf("HI수신!");
        }
    });
    carmeleon.Http.onMsgBinary([](std::vector<uint8_t> data) {
        Serial.printf("📦 바이너리 수신 (%d바이트): ", data.size());
        for (auto b : data) Serial.printf("%02X ", b);
        Serial.println();
    });
    
    //연결을 계속 유지
    carmeleon.Http.KeepAlive(true);

    carmeleon.Http.requestHeader("User-Agent", "carmeleonclient/1.0");
    carmeleon.Http.requestHeader("Firmware-version", "0.1");

    if (carmeleon.Http.handshake()) {

        carmeleon.Http.sendMsgString("hello");
        carmeleon.Http.sendMsgString("ping");
        carmeleon.Http.sendMsgString("hello");
        carmeleon.Http.sendMsgString("hello");

        std::vector<uint8_t> bin = { 0x82, 0xA3, 0x52, 0x4C, 0x59, 0x92, 0x01, 0x00, 0xA7, 0x54, 0x4D, 0x50, 0x5F, 0x4F, 0x46, 0x53, 0x00 };
        carmeleon.Http.sendMsgBinary(bin);
    
    } else {
        Serial.println("웹소켓 시작 실패");
    }

    //클라이언트측에서 연결끊기, KeepAlive(false)을 따로 해줘야함
    carmeleon.Http.KeepAlive(false);
    carmeleon.Http.end();

}

void loop(){


}
  