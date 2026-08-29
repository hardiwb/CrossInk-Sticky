#pragma once

#include <cstddef>
#include <cstdint>

// Owns the global ESP-NOW receive callback and the Wi-Fi station lifetime.
// Only one instance may be active because ESP-NOW exposes process-global
// callbacks.
class HalEspNow final {
 public:
  using ReceiveCallback = void (*)(const uint8_t* sourceMac, const uint8_t* data, int length, void* context);

  HalEspNow() = default;
  ~HalEspNow();
  HalEspNow(const HalEspNow&) = delete;
  HalEspNow& operator=(const HalEspNow&) = delete;

  bool begin(uint8_t channel, ReceiveCallback callback, void* context);
  void end();
  bool send(const uint8_t* peerMac, const uint8_t* data, size_t length);
  bool started() const { return started_; }
  static void dispatch(const uint8_t* sourceMac, const uint8_t* data, int length);

 private:
  static HalEspNow* active_;
  ReceiveCallback callback_ = nullptr;
  void* context_ = nullptr;
  uint8_t channel_ = 0;
  bool started_ = false;

  bool addPeer(const uint8_t* peerMac) const;
};
