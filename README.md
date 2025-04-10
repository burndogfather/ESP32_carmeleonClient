ESP32 Carmeleon Client
================

ESP32 Carmeleon Client는 Carmeleon서버와 실시간 스트림통신을 ESP32에서 손쉽게 구현하기 위한 통합 라이브러리입니다. 

carmeleonClient 클래스의 주요특징 : 
- Ethernet 연결의 내부적지원 (W5500, ENC28J60 등)
- TLS 또는 SSL 기반의 웹 연결지원
- TLS 또는 SSL 기반의 웹소켓 통신지원
- Json, Messagepack등 다양한 직렬화 기능 포함
- Cookie 및 Redirect처리등 Carmeleon서버의 프로토콜 호환
- OTA업데이트 지원
- 기타 각종 독립적 이벤트처리

(Carmeleon Framework WSS대응 목적으로 개발된 라이브러리입니다) 