// The microphone, for dictation (SpeechModel.h): records the default input device while the
// dictation key is held, through miniaudio (vendored by llama.cpp), which opens the system's audio
// at run time (PulseAudio or PipeWire, Core Audio, WASAPI). Only platforms built with llama.cpp
// compile this file.
#pragma once

#include <memory>
#include <vector>

namespace ProjectKey {

class Microphone {
 public:
  Microphone();
  ~Microphone();
  Microphone(const Microphone&) = delete;
  Microphone& operator=(const Microphone&) = delete;

  // Starts recording mono samples at sampleRate, dropping any earlier recording; false when there
  // is no microphone or it cannot be opened (or, on macOS, the user has not allowed it).
  bool start(int sampleRate);
  // Stops and returns what was recorded, at most kMaxSeconds; empty when it was not recording.
  std::vector<float> stop();

  static constexpr int kMaxSeconds = 60;

 private:
  struct Device;
  std::unique_ptr<Device> device_;
};

}  // namespace ProjectKey
