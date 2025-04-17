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
byte mac[] = { 0xFA, 0xC0, 0x00, 0x00, 0x00, 0x01 };  // 맥주소
IPAddress dns1(168, 126, 63, 1); // DNS정보 (KT)
IPAddress dns2(1, 1, 1, 1); //DNS2차정보 (cloudflare)

WSEvent& evt = carmeleon.ws(
    "wss://codi.farm/ws/689d2efc-2b88-494d-a5f6-a9d892b2f859",
    { //Header 커스텀데이터
        {"User-Agent", "FCO-OP-C-001"},
        {"Firmware-version", "0.1"},
        {"Macaddress", carmeleon.Eth.MACAddressString()},
        {"Localip", carmeleon.Eth.localIPString()}
    }
);

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

    

    evt.KeepAlive(true);

    evt.onConnected([](){
        Serial.println("ws 연결됨!");
    });

    evt.onDisconnected([](){
        Serial.println("ws 연결끊김!");
    });

    evt.onReceiveString([](String res){
        Serial.print("문자열응답 : ");
        Serial.println(res);
    });

    evt.onReceive([](Response res){
        Serial.println("전체응답보기 : ");
        res.prettyPrint(); 

        if (!res.is<bool>("is_success")) {
            Serial.println("❌ 요청 실패");
        }

        if (!res.json.containsKey("output")) {
            return;
        }
        Serial.println("output : "+String(res["output"]));
    });

    evt.onSend([](String raw){
        Serial.print("ws 메시지전송됨:");
        Serial.println(raw);
    });

    evt.start();
    delay(5000);

}

void loop(){
    
    evt.KeepAlive(true);

    evt.send("ping");

    evt.send({
        {"RLY",{1.0, 0.0}},
        {"TMP_OFS", 0.0}
    });

    delay(5000);
    
}
  