EthernetClientSecure
================

EthernetiClientSecure 클래스는 TLS(SSL)를 사용한 보안 연결을 지원하며, 
EthernetClient 클래스를 상속받아 해당 클래스의 모든 인터페이스를 포함합니다. 

EthernetClientSecure 클래스를 사용하여 보안 연결을 설정하는 세 가지 방법 : 
- 루트 인증서(CA 인증서) 사용 
- 루트 CA 인증서 + 클라이언트 인증서 및 키 조합 사용 
- 미리 공유된 키(PSK, Pre-Shared Key) 사용 

https://github.com/tuan-karma/ESP32_WiFiClientSecure 의 Ethernet전용 포팅버전입니다. 

종속 Ethernet 라이브러리 : https://github.com/arduino-libraries/Ethernet 

WiFiClientSecure와 사용하는 방법이 100%로 동일합니다. 


(Carmeleon Framework WSS대응 목적으로 개발된 라이브러리입니다) 