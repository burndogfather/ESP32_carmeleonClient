#include <Arduino.h>
#include <carmeleonClient.h>

#define BULTIN_LED 2

carmeleonClient carmeleon;

//ENC28J60Driver driver;
//EMACDriver driver(ETH_PHY_LAN8720);
W5500Driver driver(5, -1, 34);
//                 cs irq rst
/*
3v3-3.3v
GND-GND
D5-SCS
D18-SCLK
D23-MOSI
D19-MISO
D34-RST
*/

WSEvent* evt = nullptr; //전역선언

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

  //연결성공 이벤트
  carmeleon.Eth.onConnected([]() {
	Serial.println("이더넷 연결됨!");
  });

  //연결끊김 이벤트
  carmeleon.Eth.onDisconnected([]() {
	Serial.println("❌ 이더넷 연결 끊김");
	digitalWrite(BULTIN_LED, LOW);
  });

  //아이피할당받을때 이벤트
  carmeleon.Eth.onGotIP([]() {
	Serial.print("✅ 이더넷 아이피할당됨 : "); 
	Serial.println(carmeleon.Eth.localIP());
	digitalWrite(BULTIN_LED, HIGH);
  });
  

  carmeleon.Eth.setHostname("helloworld"); //네트워크상에 출력하는 호스트네임 지정
  carmeleon.Eth.setDNS(dns1, dns2); //DNS서버 지정
  
  Serial.println("Ethernet연결 시도 중...");
  //이더넷 연결시작 (맥주소)
  while (carmeleon.Eth.begin(mac) == 0) {
	Serial.println("Ethernet연결 재시도!");
	delay(50);
  }


  //이더넷 연결과 관련된 각종 상태값
  if (carmeleon.Eth.hardwareStatus() == EthernetNoHardware) {
	Serial.println("Ethernet없음! 하드웨어를 확인하세요.");
  } else if (carmeleon.Eth.linkStatus() == LinkOFF) {
	Serial.println("Ethernet케이블 연결 안됨!");
  } else if (carmeleon.Eth.linkStatus() == Unknown) {
	Serial.println("Ethernet케이블 오류!");
  } else if (carmeleon.Eth.linkStatus() == LinkON) {
	Serial.println("Ethernet연결 성공!");
  }

  Serial.println("NTP서버 동기화중 : "); 
  carmeleon.Eth.setNTP("time.bora.net"); //NTP서버 지정 및 시간정보 동기화 실행
  time_t now = time(nullptr);
  Serial.printf("NTP 동기화완료(KST): %s", ctime(&now));
  

  Serial.println("======연결정보======");
  Serial.print("IP 주소: "); Serial.println(carmeleon.Eth.localIP());
  Serial.print("서브넷 마스크: "); Serial.println(carmeleon.Eth.subnetMask());
  Serial.print("게이트웨이: "); Serial.println(carmeleon.Eth.gatewayIP());
  Serial.print("DNS 서버: "); Serial.println(carmeleon.Eth.dnsServerIP());


  Serial.println("======프레임워크 기반 WS통신======");


  evt = &carmeleon.ws(
	"wss://도메인/ws/123",
	{ //Header 커스텀데이터
	  {"User-Agent", "blablam"},
	  {"Firmware-version", "0.1"},
	  {"Macaddress", carmeleon.Eth.MACAddressString()},
	  {"Localip", carmeleon.Eth.localIPString()}
	}
  );

  evt->KeepAlive(true);

  evt->onConnected([](){
	Serial.println("ws 연결됨!");
  });

  evt->onDisconnected([](){
	Serial.println("ws 연결끊김!");
  });

  evt->onReceiveString([](String res){
	Serial.print("문자열응답 : ");
	Serial.println(res);
  });

  evt->onReceive([](Response res){
	Serial.println("전체응답보기 : ");
	res.prettyPrint(); // 전체 응답 보기
	if (!res.is<bool>("is_success")) {
	  Serial.println("❌ 요청 실패");
	}
	if (!res.json.containsKey("output")) {
	  return;
	  
	}
	Serial.println("output : "+String(res["output"]));
  });

  evt->onSend([](String raw){
	Serial.print("ws 메시지전송됨:");
	Serial.println(raw);
  });

  evt->start();

}


void loop() {
  delay(6000);
  
  evt->KeepAlive(true);
  
  evt->send("ping");
  
  evt->send({
  	{"RLY",{1.0, 0.0}},
	{"TMP_OFS", 0.0}
  });
}
