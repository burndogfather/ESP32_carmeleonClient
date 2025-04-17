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
  if (carmeleon.Http.begin("https://codi.farm/home")) {
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
  Serial.println(carmeleon.Http.getCookie("codi.farm", "_TOKEN_"));

  

  Serial.println("======쿠키 변경 테스트======");
  carmeleon.Http.setCookie("codi.farm", "_TOKEN_", "test");
  Serial.print("변경후 토큰값: ");
  Serial.println(carmeleon.Http.getCookie("codi.farm", "_TOKEN_"));
  carmeleon.Http.printAllCookies();
  
 
  carmeleon.Http.onConnected([]() {
    Serial.println("✅ 전송!");
  });
  carmeleon.Http.onDisconnected([]() {
    Serial.println("❌ 수신받음!");
  });
  Serial.println("======리다이렉트 테스트======");
  if (carmeleon.Http.begin("http://codi.farm")) {
    carmeleon.Http.setCookie("codi.farm", "_TOKEN_", "test");
    carmeleon.Http.removeCookie("codi.farm", "_TOKEN_");
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
  if(carmeleon.Http.begin("wss://codi.farm/ws/test")){
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

  carmeleon.Http.requestHeader("User-Agent", "FCO-OP-C-001");
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
  
  

  Response res = carmeleon.api(
    "https://test.codi.farm/api/washnow/area_select", 
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
    "wss://codi.farm/ws/689d2efc-2b88-494d-a5f6-a9d892b2f859",
    { //Header 커스텀데이터
      {"User-Agent", "FCO-OP-C-001"},
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

  evt.send({
    {"RLY",{1.0, 0.0}},
    {"TMP_OFS", 0.0}
  });
  
  //서버측에 연결끊기를 요청하기
  //evt.KeepAlive(false);
  //evt.send("bye");

  //메시지를 최대한 다 수신받으려면 delay(500) 사용필요
  


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
  if(carmeleon.Ota.begin("https://test.codi.farm/esp32_blink_firmware.bin")){
    Serial.println("OTA완료!");
  }else{
    Serial.println("OTA업데이트 실패");
  }
  
}




void loop() {
  
}
