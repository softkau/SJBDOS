#include "usb/classdriver/keyboard.hpp"

#include <bitset>
#include <algorithm>
#include "usb/memory.hpp"
#include "usb/device.hpp"

namespace usb {
  HIDKeyboardDriver::HIDKeyboardDriver(Device* dev, int interface_index)
      : HIDBaseDriver{dev, interface_index, 8} {
  }

  Error HIDKeyboardDriver::OnDataReceived() {
    std::bitset<256> prv, cur;
    for (int i = 2; i < 8; ++i) {
      prv.set(PreviousBuffer()[i], true);
      cur.set(Buffer()[i], true);
    }
    const auto changed = prv ^ cur;
    const auto pressed = changed & cur;

    for (int k = 1; k < 256; ++k) {
      if (changed.test(k)) {
        NotifyKeyPush(Buffer()[0], k, pressed.test(k));
      }
    }
    return MAKE_ERROR(Error::kSuccess);
  }

  void* HIDKeyboardDriver::operator new(size_t size) {
    return AllocMem(sizeof(HIDKeyboardDriver), 0, 0);
  }

  void HIDKeyboardDriver::operator delete(void* ptr) noexcept {
    FreeMem(ptr);
  }

  void HIDKeyboardDriver::SubscribeKeyPush(
      std::function<void (uint8_t modifier, uint8_t keycode, bool pressed)> observer) {
    observers_[num_observers_++] = observer;
  }

  std::function<HIDKeyboardDriver::ObserverType> HIDKeyboardDriver::default_observer;

  void HIDKeyboardDriver::NotifyKeyPush(uint8_t modifier, uint8_t keycode, bool pressed) {
    for (int i = 0; i < num_observers_; ++i) {
      observers_[i](modifier, keycode, pressed);
    }
  }
}

