#include "HalEspNow.h"

#include <Logging.h>

#ifndef SIMULATOR
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include <cstring>
#endif

namespace {
constexpr const char* LOG_TAG = "ESPNOW";

#ifndef SIMULATOR
void receiveEspNowPacket(const esp_now_recv_info_t* info, const uint8_t* data, const int length) {
  if (!info || !info->src_addr) return;
  // The registered callback only validates/copies into feature-owned fixed
  // storage. Rendering and SD writes stay on the application task.
  HalEspNow::dispatch(info->src_addr, data, length);
}
#endif
}  // namespace

HalEspNow* HalEspNow::active_ = nullptr;

HalEspNow::~HalEspNow() { end(); }

void HalEspNow::dispatch(const uint8_t* sourceMac, const uint8_t* data, const int length) {
  if (!active_ || !active_->callback_) return;
  active_->callback_(sourceMac, data, length, active_->context_);
}

bool HalEspNow::begin(const uint8_t channel, const ReceiveCallback callback, void* context) {
#ifdef SIMULATOR
  (void)channel;
  (void)callback;
  (void)context;
  return false;
#else
  if (started_ || active_ || !callback || channel == 0 || channel > 14) {
    LOG_ERR(LOG_TAG, "Invalid receiver start request");
    return false;
  }

  callback_ = callback;
  context_ = context;
  channel_ = channel;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false);
  WiFi.setSleep(false);
  if (esp_wifi_set_channel(channel_, WIFI_SECOND_CHAN_NONE) != ESP_OK || esp_wifi_set_ps(WIFI_PS_NONE) != ESP_OK) {
    LOG_ERR(LOG_TAG, "Failed to configure channel %u", static_cast<unsigned>(channel_));
    end();
    return false;
  }
  if (esp_now_init() != ESP_OK) {
    LOG_ERR(LOG_TAG, "esp_now_init failed");
    end();
    return false;
  }

  started_ = true;
  active_ = this;
  if (esp_now_register_recv_cb(receiveEspNowPacket) != ESP_OK) {
    LOG_ERR(LOG_TAG, "Failed to register receive callback");
    end();
    return false;
  }
  return true;
#endif
}

void HalEspNow::end() {
#ifndef SIMULATOR
  if (active_ == this) {
    active_ = nullptr;
    if (started_) esp_now_unregister_recv_cb();
  }
  if (started_) esp_now_deinit();
  WiFi.disconnect(false);
  WiFi.mode(WIFI_OFF);
#endif
  started_ = false;
  callback_ = nullptr;
  context_ = nullptr;
  channel_ = 0;
}

bool HalEspNow::addPeer(const uint8_t* peerMac) const {
#ifdef SIMULATOR
  (void)peerMac;
  return false;
#else
  if (!started_ || !peerMac) return false;
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, peerMac, ESP_NOW_ETH_ALEN);
  peer.channel = channel_;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  const esp_err_t result = esp_now_add_peer(&peer);
  return result == ESP_OK || result == ESP_ERR_ESPNOW_EXIST;
#endif
}

bool HalEspNow::send(const uint8_t* peerMac, const uint8_t* data, const size_t length) {
#ifdef SIMULATOR
  (void)peerMac;
  (void)data;
  (void)length;
  return false;
#else
  if (!data || length == 0 || !addPeer(peerMac)) return false;
  const esp_err_t result = esp_now_send(peerMac, data, length);
  if (result != ESP_OK) {
    LOG_ERR(LOG_TAG, "esp_now_send failed: %d", static_cast<int>(result));
    return false;
  }
  return true;
#endif
}
