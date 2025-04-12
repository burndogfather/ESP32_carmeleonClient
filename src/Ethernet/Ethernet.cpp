/*
  This file is part of the EthernetESP32 library for Arduino
  https://github.com/Networking-for-Arduino/EthernetESP32
  Copyright 2024 Juraj Andrassy

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "Ethernet.h"

#include "esp_eth_phy.h"
#include "esp_eth_mac.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "driver/gpio.h"

#include <esp_netif.h>
#include "lwip/netif.h"


static uint8_t nextIndex = 0;

EthernetClass::EthernetClass() {
  index = nextIndex;
  nextIndex++;
}

static void ethEventCB(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == ETH_EVENT) {
    EthernetClass* eth = (EthernetClass*) arg;
    esp_eth_handle_t ethHandle = *((esp_eth_handle_t*) event_data);
    if (eth != NULL && eth->getEthHandle() == ethHandle) {
      eth->_onEthEvent(event_id, event_data);
    }
  }
}

void EthernetClass::init(EthDriver& ethDriver) {
  static bool printed = false;
  if (!printed && Serial) {
    printed = true;
    Serial.println();
    Serial.println("    _____   ___  ______ ___  ___ _____  _      _____  _____  _   _");
    Serial.println("   /  __ \\ / _ \\ | ___ \\|  \\/  ||  ___|| |    |  ___||  _  || \\ | |");
    Serial.println("   | /  \\// /_\\ \\| |_/ /| .  . || |__  | |    | |__  | | | ||  \\| |");
    Serial.println("   | |    |  _  ||    / | |\\/| ||  __| | |    |  __| | | | || . ` |");
    Serial.println("   | \\__/\\| | | || |\\ \\ | |  | || |___ | |____| |___ \\ \\_/ /| |\\  |");
    Serial.println("    \\____/\\_| |_/\\_| \\_|\\_|  |_/\\____/ \\_____/\\____/  \\___/ \\_| \\_/");
    Serial.println("                              DADOL corp");
    Serial.println();
  }
  driver = &ethDriver;
}

int EthernetClass::begin(uint8_t *mac, unsigned long timeout) {
  if (netif() != NULL) {
    config(INADDR_NONE);
  }
  if (beginETH(mac)) {
    hwStatus = EthernetHardwareFound;
    if (timeout) {
      const unsigned long start = millis();
      while (!hasIP() && ((millis() - start) < timeout)) {
        delay(10);
      }
    }
  }
  return hasIP();
}

void EthernetClass::begin(uint8_t *mac, IPAddress localIP, IPAddress dnsIP, IPAddress gatewayIP, IPAddress netmask) {

  if (localIP.type() == IPv4) {
    // setting auto values
    if (dnsIP == INADDR_NONE) {
      dnsIP = localIP;
      dnsIP[3] = 1;
    }
    if (gatewayIP == INADDR_NONE) {
      gatewayIP = localIP;
      gatewayIP[3] = 1;
    }
    if (netmask == INADDR_NONE) {
      netmask = IPAddress(255, 255, 255, 0);
    }
  }
//  if (config(localIP, gatewayIP, netmask, dnsIP) && beginETH(mac)) {
  if (beginETH(mac) && config(localIP, gatewayIP, netmask, dnsIP) ) {
    hwStatus = EthernetHardwareFound;
  }
  const unsigned long start = millis();
  while (!linkUp() && ((millis() - start) < 3000)) {
    delay(10);
  }


}

int EthernetClass::begin(unsigned long timeout) {
  return begin(nullptr, timeout);
}

void EthernetClass::begin(IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet) {
  begin(nullptr, ip, dns, gateway, subnet);
}

int EthernetClass::maintain() {
  return 0;
}

void EthernetClass::end() {

  //  Network.removeEvent(onEthConnected, ARDUINO_EVENT_ETH_CONNECTED);

  if (_ntpServer) {
    free(_ntpServer);
    _ntpServer = nullptr;
  }

  if (ethHandle != NULL) {
    if (esp_eth_stop(ethHandle) != ESP_OK) {
      log_e("Failed to stop Ethernet");
      return;
    }
    //wait for stop
    while (getStatusBits() & ESP_NETIF_STARTED_BIT) {
      delay(10);
    }
    //delete glue first
    if (glueHandle != NULL) {
      if (esp_eth_del_netif_glue(glueHandle) != ESP_OK) {
        log_e("Failed to del_netif_glue Ethernet");
        return;
      }
      glueHandle = NULL;
    }
    //uninstall driver
    if (esp_eth_driver_uninstall(ethHandle) != ESP_OK) {
      log_e("Failed to uninstall Ethernet");
      return;
    }
    ethHandle = NULL;
    driver->end();
  }
  if (_eth_ev_instance != NULL) {
    if (esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, _eth_ev_instance) == ESP_OK) {
      _eth_ev_instance = NULL;
    }
  }
  destroyNetif();
}

EthernetLinkStatus EthernetClass::linkStatus() {
  if (netif() == NULL) {
    return Unknown;
  }
  return linkUp() ? LinkON : LinkOFF;
}

EthernetHardwareStatus EthernetClass::hardwareStatus() {
  return hwStatus;
}

void EthernetClass::MACAddress(uint8_t *mac) {
  macAddress(mac);
}

IPAddress EthernetClass::dnsServerIP() {
  return dnsIP();
}

void EthernetClass::setDnsServerIP(const IPAddress dns) {
  dnsIP(0, dns);
}

void EthernetClass::setDNS(IPAddress dns, IPAddress dns2) {
  dnsIP(0, dns);
  if (dns2 != INADDR_NONE) {
    dnsIP(1, dns2);
  }
}

void EthernetClass::setHostname(const char* hostname) {

  if (_pendingHostname) {
    free(_pendingHostname);
    _pendingHostname = nullptr;
  }

  if (hostname && strlen(hostname) > 0) {
    _pendingHostname = strdup(hostname);
    if (!_pendingHostname) {
      log_e("Failed to allocate memory for hostname");
    }
  }

  if (_esp_netif != nullptr) {
      log_w("setHostname called after begin(). Hostname might not be effective until next connection/DHCP renewal.");
  }
}

bool EthernetClass::setNTP(const char* ntpServer, long gmtOffset_sec, int daylightOffset_sec) {
  if (!linkUp()) {
    log_e("setNTP Error: Ethernet link is down");
    return false;
  }

  // NTP 서버 설정
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  // 기존 NTP 서버 메모리 해제
  if (_ntpServer) {
      free(_ntpServer);
  }
  _ntpServer = strdup(ntpServer);

  // 시간 동기화 대기 (최대 10초)
  time_t now = 0;
  int retry = 0;
  while ((now = time(nullptr)) < 24 * 3600 && retry++ < 100) {
    delay(100);  // 0.1초마다 체크 → 최대 10초
  }

  if (now < 24 * 3600) {
    log_e("\nsetNTP Failed to synchronize time!");
    return false;
  }

  return true;
}

int EthernetClass::hostByName(const char *hostname, IPAddress &result) {
  return Network.hostByName(hostname, result);
}

size_t EthernetClass::printDriverInfo(Print &out) const {
  return 0;
}

bool EthernetClass::beginETH(uint8_t *macAddrP) {
  esp_err_t ret = ESP_OK;

  if (index > 2) {
    log_e("More than 3 Ethernet interfaces");
    return false;
  }
  if (driver == nullptr) {
    log_e("Ethernet driver is not set");
    return false;
  }
  if (_esp_netif != NULL || ethHandle != NULL) {
    log_w("Ethernet already started");
    return true;
  }

  Network.begin();

  if (driver->usesIRQ()) {
    ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
      log_e("GPIO ISR handler install failed: %d", ret);
      return false;
    }
  }

  driver->begin();

  esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(driver->mac, driver->phy);
  ret = esp_eth_driver_install(&eth_config, &ethHandle);
  if (ret != ESP_OK) {
    log_e("Ethernet driver install failed: %d", ret);
    return false;
  }
  if (ethHandle == NULL) {
    log_e("esp_eth_driver_install failed! eth_handle is NULL");
    return false;
  }

  uint8_t macAddr[ETH_ADDR_LEN];
  if (macAddrP != nullptr) {
    memcpy(macAddr, macAddrP, ETH_ADDR_LEN);
  } else {
    // Derive a new MAC address for this interface
    uint8_t base_mac_addr[ETH_ADDR_LEN];
    ret = esp_efuse_mac_get_default(base_mac_addr);
    if (ret != ESP_OK) {
      log_e("Get EFUSE MAC failed: %d", ret);
      return false;
    }
    base_mac_addr[ETH_ADDR_LEN - 1] += index;
    esp_derive_local_mac(macAddr, base_mac_addr);
  }

  ret = esp_eth_ioctl(ethHandle, ETH_CMD_S_MAC_ADDR, macAddr);
  if (ret != ESP_OK) {
    log_e("Ethernet MAC address config failed: %d", ret);
    return false;
  }

  esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
  esp_netif_inherent_config_t esp_netif_config = ESP_NETIF_INHERENT_DEFAULT_ETH();
  char key[10];
  char desc[10];
  if (nextIndex > 1) {
    snprintf(key, sizeof(key), "ETH_%d", index);
    esp_netif_config.if_key = key;
    snprintf(desc, sizeof(desc), "eth%d", index);
    esp_netif_config.if_desc = desc;
    esp_netif_config.route_prio -= index * 5;
  }

  cfg.base = &esp_netif_config;
  _esp_netif = esp_netif_new(&cfg);
  if (_esp_netif == NULL) {
    log_e("esp_netif_new failed");
    return false;
  }

  if (_pendingHostname != nullptr) {
    esp_err_t ret = esp_netif_set_hostname(_esp_netif, _pendingHostname);
    if (ret != ESP_OK) {
        log_e("esp_netif_set_hostname failed during init: 0x%X", ret);
        return false;
    }
    // 설정 후 임시 저장된 호스트네임 메모리 해제
    free(_pendingHostname);
    _pendingHostname = nullptr;
  }else{
    esp_netif_set_hostname(_esp_netif, "carmeleon-client");
  }

  // Attach Ethernet driver to TCP/IP stack
  glueHandle = esp_eth_new_netif_glue(ethHandle);
  if (glueHandle == NULL) {
    log_e("esp_eth_new_netif_glue failed");
    return false;
  }

  ret = esp_netif_attach(_esp_netif, glueHandle);
  if (ret != ESP_OK) {
    log_e("esp_netif_attach failed: %d", ret);
    return false;
  }

  if (_eth_ev_instance == NULL && esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &ethEventCB, this, &_eth_ev_instance)) {
    log_e("event_handler_instance_register for ETH_EVENT Failed!");
    return false;
  }

  initNetif((Network_Interface_ID)(ESP_NETIF_ID_ETH + index));

  ret = esp_eth_start(ethHandle);
  if (ret != ESP_OK) {
    log_e("esp_eth_start failed: %d", ret);
    return false;
  }


//  Network.onSysEvent(onEthConnected, ARDUINO_EVENT_ETH_CONNECTED);

  return true;
}

void EthernetClass::onGotIP(std::function<void()> cb) {
  _onGotIP = cb;
}

void EthernetClass::onConnected(std::function<void()> cb) {
  _connectedCallback = cb;
}

void EthernetClass::onDisconnected(std::function<void()> cb) {
  _disconnectedCallback = cb;
}


void EthernetClass::_onEthEvent(int32_t eventId, void *eventData) {
  arduino_event_t arduino_event;
  arduino_event.event_id = ARDUINO_EVENT_MAX;

  if (eventId == ETHERNET_EVENT_CONNECTED || eventId == ETHERNET_EVENT_START) {
    // 연결되었다고 해도 IP가 아직일 수 있으므로 주기적으로 확인
    xTaskCreate([](void* param) {
      EthernetClass* self = static_cast<EthernetClass*>(param);
      for (int i = 0; i < 50; ++i) { // 최대 5초 대기
        if (self->localIP() != INADDR_NONE) {
          if (self->_onGotIP) self->_onGotIP();
          break;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
      }
      vTaskDelete(NULL);
    }, "onGotIP", 2048, this, 1, NULL);
  }

  if (eventId == ETHERNET_EVENT_CONNECTED) {
    log_v("%s Connected", desc());
    arduino_event.event_id = ARDUINO_EVENT_ETH_CONNECTED;
    arduino_event.event_info.eth_connected = ethHandle;
    setStatusBits(ESP_NETIF_CONNECTED_BIT);
    _connectedCallback();
  } else if (eventId == ETHERNET_EVENT_DISCONNECTED) {
    log_v("%s Disconnected", desc());
    arduino_event.event_id = ARDUINO_EVENT_ETH_DISCONNECTED;
    clearStatusBits(ESP_NETIF_CONNECTED_BIT | ESP_NETIF_HAS_IP_BIT | ESP_NETIF_HAS_LOCAL_IP6_BIT | ESP_NETIF_HAS_GLOBAL_IP6_BIT);
    _disconnectedCallback();
  } else if (eventId == ETHERNET_EVENT_START) {
    log_v("%s Started", desc());
    arduino_event.event_id = ARDUINO_EVENT_ETH_START;
    setStatusBits(ESP_NETIF_STARTED_BIT);
  } else if (eventId == ETHERNET_EVENT_STOP) {
    log_v("%s Stopped", desc());
    arduino_event.event_id = ARDUINO_EVENT_ETH_STOP;
    clearStatusBits(ESP_NETIF_STARTED_BIT | ESP_NETIF_CONNECTED_BIT | ESP_NETIF_HAS_IP_BIT //
        | ESP_NETIF_HAS_LOCAL_IP6_BIT | ESP_NETIF_HAS_GLOBAL_IP6_BIT | ESP_NETIF_HAS_STATIC_IP_BIT
    );
  }
  if (arduino_event.event_id < ARDUINO_EVENT_MAX) {
    Network.postEvent(&arduino_event);
  }
}

EthernetClass Ethernet;
