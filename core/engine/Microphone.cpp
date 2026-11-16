#include "Microphone.h"

#include <algorithm>
#include <mutex>

// Its own copy of miniaudio with the device API that mtmd's leaves out; static, so the two never clash.
#define MINIAUDIO_IMPLEMENTATION
#define MA_API static
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#define ma_atomic_global_lock projectkey_ma_atomic_global_lock  // the one global MA_API does not make static
#include "miniaudio/miniaudio.h"

namespace ProjectKey {

struct Microphone::Device {
  ma_device device;
  std::mutex mutex;
  std::vector<float> samples;
  size_t limit = 0;
  bool opened = false;
  ~Device() {
    if (opened) ma_device_uninit(&device);
  }

  static void Received(ma_device* device, void*, const void* input, ma_uint32 frames) {
    auto* self = static_cast<Device*>(device->pUserData);
    const float* in = static_cast<const float*>(input);
    std::lock_guard<std::mutex> lock(self->mutex);
    size_t room = self->limit - std::min(self->limit, self->samples.size());
    self->samples.insert(self->samples.end(), in, in + std::min<size_t>(frames, room));
  }
};

Microphone::Microphone() = default;
Microphone::~Microphone() = default;

bool Microphone::start(int sampleRate) {
  device_.reset();
  auto device = std::make_unique<Device>();
  device->limit = static_cast<size_t>(sampleRate) * kMaxSeconds;
  device->samples.reserve(static_cast<size_t>(sampleRate) * 10);
  ma_device_config config = ma_device_config_init(ma_device_type_capture);
  config.capture.format = ma_format_f32;
  config.capture.channels = 1;
  config.sampleRate = static_cast<ma_uint32>(sampleRate);
  config.dataCallback = Device::Received;
  config.pUserData = device.get();
  if (ma_device_init(nullptr, &config, &device->device) != MA_SUCCESS) return false;
  device->opened = true;
  if (ma_device_start(&device->device) != MA_SUCCESS) return false;
  device_ = std::move(device);
  return true;
}

std::vector<float> Microphone::stop() {
  if (!device_) return {};
  ma_device_stop(&device_->device);
  std::vector<float> samples;
  {
    std::lock_guard<std::mutex> lock(device_->mutex);
    samples = std::move(device_->samples);
  }
  device_.reset();
  return samples;
}

}  // namespace ProjectKey
