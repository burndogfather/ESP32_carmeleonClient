ESP32 Carmeleon Client
================

ESP32 Carmeleon Client는 Carmeleon서버와 실시간 스트림통신을 ESP32에서 손쉽게 구현하기 위한 통합 라이브러리입니다. 

carmeleonClient 클래스의 주요특징 : 
- Carmeleon Framework 간 API/WSS 통신기능 지원
- Ethernet 연결의 내부적지원 (W5500, ENC28J60 등)
- Lowlevel수준의 TLS 또는 SSL 기반의 웹 또는 웹소켓 연결지원
- Json, Messagepack등 다양한 직렬화 기능 포함
- Cookie 및 Redirect처리등 브라우저의 기본 기능 내재화
- OTA업데이트 지원
- 기타 각종 독립적 이벤트처리


 
테스트환경 : ESP32S Develop Module  
개발환경 : PlatformIO  
서버환경 : Carmeleon Framework v3.0.0 (DADOL corp) 
(Carmeleon Framework API연동 및 WSS통신 대응목적으로 개발된 라이브러리입니다) 
 

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