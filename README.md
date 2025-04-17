ESP32 Carmeleon Client
================

ESP32 Carmeleon Client는 Carmeleon서버와 AES암호화통신을 활용한 API요청이나 스트림기반의 소켓통신을 ESP32에서 손쉽게 구현하기 위한 통합 라이브러리입니다. 

carmeleonClient 클래스의 주요특징 : 
- Carmeleon Framework 간 API/WSS 통신기능 지원
- Ethernet 연결의 내부적지원 (W5500, ENC28J60 등)
- Lowlevel수준의 TLS 또는 SSL 기반의 웹 또는 웹소켓 연결지원
- Json, Messagepack등 다양한 직렬화 기능 포함
- Cookie 및 Redirect처리등 브라우저의 기본 기능 내재화
- OTA업데이트 지원
- 기타 각종 독립적 이벤트처리


 
테스트환경 : ESP32S Dev Module(Espressif) + W5500(WIZnet) 
개발환경 : PlatformIO  
서버환경 : Carmeleon Framework v3.0.0 (DADOL corp) 
 
(Carmeleon Framework API연동 및 WSS통신 대응목적으로 개발된 라이브러리입니다) 
(Carmeleon Framework가 아닌 HTTP 통신은 examples의 lowlevel을 활용할 수 있습니다) 
 
========================== 

### 중요사항 
PlatformIO 프로젝트 폴더 내부에 "platformio.ini" 파일을 아래와 같이 수정해주시길 바랍니다. 
"erase_flash.py", "littlefsbuilder.py", "partitions.csv" 소스코드는 [여기에](https://github.com/burndogfather/ESP32_carmeleonClient/tree/master/PlatformIO) 
```ini
[env:esp32dev]
platform = https://raw.githubusercontent.com/burndogfather/ESP32_carmeleonClient/refs/heads/master/platform-espressif32_v54.03.20.zip
framework = arduino
board = esp32dev
monitor_speed = 115200
upload_speed = 921600 #ESP32의 최대 업로드 스피드

lib_deps = 
	https://github.com/burndogfather/ESP32_carmeleonClient.git

board_build.flash_mode = qio
board_build.f_cpu = 240000000L # CPU클럭 (240Mhz)
board_build.f_flash = 80000000L  # 플래시 클럭 속도 증가 (80MHz)
board_build.partitions = partitions.csv #파티션 지정파일
board_build.flash_size = 4MB
board_build.filesystem = littlefs #쿠키를 저장하는 방식

build_flags =  

extra_scripts =
	#pre:erase_flash.py # OTA이후 파티션이 정리되지 않아 쓰기를 못하면 이것을 활성화하면 업로드직전에 포맷한다
	./littlefsbuilder.py
```
 
========================== 
 
이더넷관련 Method 목록 
- carmeleonClient.Eth.init(EthDriver& ethDriver)
- carmeleonClient.Eth.begin(uint8_t *mac)
- carmeleonClient.Eth.MACAddress(uint8_t *mac)
- carmeleonClient.Eth.MACAddressString()
- carmeleonClient.Eth.localIPString()
- carmeleonClient.Eth.dnsServerIP()
- carmeleonClient.Eth.setDnsServerIP(const IPAddress dns)
- carmeleonClient.Eth.setHostname(const char* hostname)
- carmeleonClient.Eth.setDNS(IPAddress dns)
- carmeleonClient.Eth.setNTP(const char* ntpServer)
- carmeleonClient.Eth.getNTPServer()
- carmeleonClient.Eth.hostByName(const char *hostname, IPAddress &result)
- carmeleonClient.Eth.onGotIP(std::function<void()> cb)
- carmeleonClient.Eth.onConnected(std::function<void()> cb)
- carmeleonClient.Eth.onDisconnected(std::function<void()> cb)
- carmeleonClient.Eth.printDriverInfo(Print &out)
- carmeleonClient.Eth.linkStatus()
- carmeleonClient.Eth.hardwareStatus()
- carmeleonClient.Eth.end() 
 
 
API관련 Method 목록 
- Response res = carmeleonClient.api(const String& url, std::initializer_list<std::pair<const char*, JsonVariantWrapper>> params = {})
- res.prettyPrint() 
- res.json
- res.json.containsKey("key")
- res.["key"]
- res.["key"][i]
- res.["key"].is<T>()
- res.["key"].as<T>()
 
 
WS관련 Method 목록 
- WSEvent& evt = carmeleonClient.ws(const String& url, const std::map<String,String>& headers)
- evt.start()
- evt.KeepAlive(bool enable)
- evt.onConnected(std::function<void()> cb)
- evt.onDisconnected(std::function<void()> cb)
- evt.onReceiveString(std::function<void(String)> cb)
- evt.onReceive(std::function<void(Response)> cb)
- evt.onSend(std::function<void(String)> cb)
- evt.send(const String& msg)
- evt.send(const std::vector<uint8_t>& binaryData)
- evt.send(JsonDocument& doc)
- evt.send(std::initializer_list<std::pair<const char*, JsonVariantWrapper>> kv)
- evt.end()
 
 
LowLevel HTTP Method 목록 
- carmeleonClient.Http.begin(const char* url)
- carmeleonClient.Http.requestHeader(const String& name, const String& value)
- carmeleonClient.Http.responseHeader(const String& name)
- carmeleonClient.Http.printAllResponseHeaders()
- carmeleonClient.Http.responseBody()
- carmeleonClient.Http.statusCode()
- carmeleonClient.Http.printAllCookies()
- carmeleonClient.Http.clearAllCookies()
- carmeleonClient.Http.getCookie(const String& domain, const String& name)
- carmeleonClient.Http.setCookie(const String& domain, const String& name, const String& value, time_t expire)
- carmeleonClient.Http.removeCookie(const String& domain, const String& name)
- carmeleonClient.Http.debugCookiesystem()
- carmeleonClient.Http.get()
- carmeleonClient.Http.post(const String& body, const String& contentType)
- carmeleonClient.Http.put(const String& body, const String& contentType)
- carmeleonClient.Http.patch(const String& body, const String& contentType)
- carmeleonClient.Http.del()
- carmeleonClient.Http.head()
- carmeleonClient.Http.handshake()
- carmeleonClient.Http.KeepAlive(bool enabled)
- carmeleonClient.Http.connected()
- carmeleonClient.Http.sendMsgString(const String& message)
- carmeleonClient.Http.sendMsgBinary(const std::vector<uint8_t>& data)
- carmeleonClient.Http.onConnected(std::function<void()> cb)
- carmeleonClient.Http.onHandshake(std::function<void()> cb)
- carmeleonClient.Http.onDisconnected(std::function<void()> cb)
- carmeleonClient.Http.onMsgString(std::function<void(String)> cb)
- carmeleonClient.Http.onMsgBinary(std::function<void(std::vector<uint8_t>)> cb)
- carmeleonClient.Http.end()
 
========================== 

### 사용예시 
[여기](https://github.com/burndogfather/ESP32_carmeleonClient/tree/master/examples)에서 각 기능별로 사용예제를 확인할 수 있습니다. 
```c++
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



  Serial.println("======프레임워크 기반 API통신======");
  Response res = carmeleon.api(
	"https://도메인/api/washnow/area_select", 
	{  //FORM 전송 데이터
	  {"page", "1"},
	  {"pagging", "50"},
	  {"searchtxt", "삼성"},
	  {"seq", {1, 2, 3}}
	}
  );
  Serial.println("전체응답보기 : ");
  res.prettyPrint(); // 전체 응답 보기

  if(res["is_success"] == true){
	Serial.println("✅ 요청 성공");

	// dataset 배열 순차 출력
	Serial.println("\n[검색 결과 목록]");

	if (!res.json.containsKey("dataset")) {
	  Serial.println("⚠️ 결과 데이터에 'dataset' 필드가 없습니다");
	  return;
	}

	JsonArray dataset = res.json["dataset"];

	if (dataset.size() == 0) {
	  Serial.println("🔍 검색 결과가 없습니다");
	  return;
	}

	for (size_t i = 0; i < res["dataset"].size(); i++) {
	  Serial.print(" - "+String(i + 1)+ " : ");
	  Serial.print(String(res["dataset"][i]["code"])+" / ");
	  Serial.println(String(res["dataset"][i]["name"]));
	}
  }else{
	Serial.println("❌ 요청 실패");
  }


  Serial.println("======프레임워크 기반 WS통신======");
  carmeleon.Http.clearAllCookies();
  WSEvent& evt = carmeleon.ws(
	"wss://도메인/ws/689d2efc-2b88-494d-a5f6-a9d892b2f859",
	{ //Header 커스텀데이터
	  {"User-Agent", "유저에이전트"},
	  {"Firmware-version", "0.1"},
	  {"Macaddress", carmeleon.Eth.MACAddressString()},
	  {"Localip", carmeleon.Eth.localIPString()}
	}
  );

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
	res.prettyPrint(); // 전체 응답 보기
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

  delay(6000);

  evt.KeepAlive(true);

  evt.send("ping");

  //메시지를 최대한 다 수신받으려면 delay(200) 사용필요
  delay(200);

  evt.send({
	{"RLY",{1.0, 0.0}},
	{"TMP_OFS", 0.0}
  });
  
  delay(5000);
  //연결끊기
  evt.end();
  


  Serial.println("======HTTPS RAW GET요청과 응답======");
  if (carmeleon.Http.begin("https://postman-echo.com/get")) {
	carmeleon.Http.requestHeader("User-Agent", "carmeleon/1.0");
	int status = carmeleon.Http.get();
	Serial.print("응답 코드: ");
	Serial.println(status);
	Serial.println("응답 헤더:");
	Serial.println(carmeleon.Http.responseHeader("Content-Type"));
	Serial.println("응답 본문:");
	Serial.println(carmeleon.Http.responseBody());
	
  } else {
	Serial.println("HTTP 시작 실패");
  }
  carmeleon.Http.end();
  
  
  Serial.println("======HTTPS RAW POST요청과 응답======");
  if (carmeleon.Http.begin("https://postman-echo.com/post")) {
	carmeleon.Http.requestHeader("User-Agent", "carmeleon/1.0");
	int status = carmeleon.Http.post("{\"message\":\"Hello from carmeleon!\"}", "application/json");
	Serial.print("응답 코드: ");
	Serial.println(status);
	Serial.println("응답 헤더:");
	Serial.println(carmeleon.Http.responseHeader("Content-Type"));
	Serial.println("응답 본문:");
	Serial.println(carmeleon.Http.responseBody());
	
  } else {
	Serial.println("HTTP 시작 실패");
  }
  carmeleon.Http.end();
  
  
  Serial.println("======HTTPS RAW PUT요청과 응답======");
  if (carmeleon.Http.begin("https://postman-echo.com/put")) {
	int status = carmeleon.Http.put("{\"message\":\"Hello from carmeleon!\"}", "application/json");
	Serial.print("응답 코드: ");
	Serial.println(status);
	Serial.println("응답 헤더:");
	Serial.println(carmeleon.Http.responseHeader("Location"));
	Serial.println("응답 본문:");
	Serial.println(carmeleon.Http.responseBody());
	
  } else {
	Serial.println("HTTP 시작 실패");
  }
  carmeleon.Http.end();

 

  Serial.println("======HTTPS 쿠키처리 테스트======");
  if (carmeleon.Http.begin("https://도메인/경로")) {
	carmeleon.Http.requestHeader("User-Agent", "carmeleon/1.0");
	int status = carmeleon.Http.get();
	Serial.print("응답 코드: ");
	Serial.println(status);
	Serial.println("응답 헤더:");
	Serial.println(carmeleon.Http.responseHeader("Content-Type"));
	carmeleon.Http.printAllResponseHeaders();
  } else {
	Serial.println("HTTP 시작 실패");
  }
  carmeleon.Http.end();

  carmeleon.Http.printAllCookies();
  carmeleon.Http.debugCookiesystem();
  carmeleon.Http.clearAllCookies();
  carmeleon.Http.debugCookiesystem();

  carmeleon.Http.printAllCookies();
  Serial.print("토큰값: ");
  Serial.println(carmeleon.Http.getCookie("도메인", "_TOKEN_"));

  

  Serial.println("======쿠키 변경 테스트======");
  carmeleon.Http.setCookie("도메인", "_TOKEN_", "test");
  Serial.print("변경후 토큰값: ");
  Serial.println(carmeleon.Http.getCookie("도메인", "_TOKEN_"));
  carmeleon.Http.printAllCookies();
  
 
  carmeleon.Http.onConnected([]() {
	Serial.println("✅ 전송!");
  });
  carmeleon.Http.onDisconnected([]() {
	Serial.println("❌ 수신받음!");
  });
  Serial.println("======리다이렉트 테스트======");
  if (carmeleon.Http.begin("http://도메인")) {
	carmeleon.Http.setCookie("도메인", "_TOKEN_", "test");
	carmeleon.Http.removeCookie("도메인", "_TOKEN_");
	carmeleon.Http.requestHeader("User-Agent", "carmeleon/1.0");
	int status = carmeleon.Http.get();
	Serial.print("응답 코드: ");
	Serial.println(status);
	Serial.print("응답 헤더: ");
	carmeleon.Http.printAllResponseHeaders();
  } else {
	Serial.println("HTTP 시작 실패");
  }
  carmeleon.Http.end();
  
  
  Serial.println("======웹소켓 테스트======");
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

	  // //이렇게 쓰면 안됨! mbedTLS는 thread-safe하지 않음!
	  // xTaskCreate([](void*){

	  //   Serial.println("다른요청이벤트 안에서 또다른 요청");
	  //   carmeleonClient eventinside_client;
	  //   eventinside_client.Http.begin("https://postman-echo.com/get");
	  //   Serial.print("eventinside_client 응답 코드: ");
	  //   Serial.println(eventinside_client.Http.get());
	  //   eventinside_client.Http.end();
	  //   vTaskDelete(NULL);
	  // }, "async_http_test", 8192, nullptr, 1, nullptr);
	}
	
  });
  carmeleon.Http.onMsgBinary([](std::vector<uint8_t> data) {
	Serial.printf("📦 바이너리 수신 (%d바이트): ", data.size());
	for (auto b : data) Serial.printf("%02X ", b);
	Serial.println();
  });
  
  //연결을 계속 유지
  carmeleon.Http.KeepAlive(true);

  carmeleon.Http.requestHeader("User-Agent", "유저에이전트");
  carmeleon.Http.requestHeader("Firmware-version", "0.1");

  if (carmeleon.Http.handshake()) {
	carmeleon.Http.sendMsgString("hello");
	carmeleon.Http.sendMsgString("ping");
	carmeleon.Http.sendMsgString("hello");
	carmeleon.Http.sendMsgString("hello");
	std::vector<uint8_t> bin = { 0x82, 0xA3, 0x52, 0x4C, 0x59, 0x92, 0x01, 0x00, 0xA7, 0x54, 0x4D, 0x50, 0x5F, 0x4F, 0x46, 0x53, 0x00 };
	carmeleon.Http.sendMsgBinary(bin);
	//carmeleon.Http.sendMsgString("bye"); //서버측에서 연결끊지만, KeepAlive(true) 로 인해 즉시 재연결된다
  } else {
	Serial.println("웹소켓 시작 실패");
  }

  //클라이언트측에서 연결끊기, KeepAlive(false)을 따로 해줘야함
  carmeleon.Http.KeepAlive(false);
  carmeleon.Http.end();
  
  

  Serial.println("======펌웨어 OTA다운로드======");
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
  if(carmeleon.Ota.begin("https://도메인/esp32_blink_firmware.bin")){
	Serial.println("OTA완료!");
  }else{
	Serial.println("OTA업데이트 실패");
  }
  
}

void loop() {
  
}

```


========================== 

### 주의사항  
ESP32의 SSL/TLS 통신을 위해서는 Espressif가 제공하는 "mbedtls_*" 내장함수를 사용하는데, 이 함수는 Thread-Safe하지 않으며, Read과정에서 Write할 수 없습니다. 
 
따라서 일반적인 POST/GET/PUT/DELETE 통신뿐만 아니라 Websocket 통신에서의 이벤트내부에 다른 통신을 중첩하여 사용할 수 없고, 반드시 "end()" 메소드를 호출시킨 뒤 처리해야 합니다. 
 
라이브러리 내부적으로는 각각의 객체가 독립적인 내부값을 다루고 있으나, 위와 같은 이슈로 인해 오류가 발생될 수 있음으로 이벤트내 중첩 요청은 사용해선 안됩니다. 
```c++
WSEvent& evt = carmeleon.ws("wss://도메인2");
evt.onDisconnected([](){
  //❌절대로 사용하면 안됨
  carmeleon.Http.begin("https://도메인1");
});
```