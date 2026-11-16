// The speech model (語音模型) for dictation (Setting::kDictation), which the settings windows download
// apart from the models: Qwen3-ASR 0.6B (GGUF, with its audio encoder in a second file), run by
// llama.cpp's mtmd on the CPU. The platforms record while the dictation key is held
// (Controller::dictationKey) and transcribe on a thread of their own, since it takes about a
// quarter of the recording's length. Only platforms built with llama.cpp compile this file.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ProjectKey {

class SpeechModel {
 public:
  SpeechModel();
  ~SpeechModel();
  SpeechModel(const SpeechModel&) = delete;
  SpeechModel& operator=(const SpeechModel&) = delete;

  // The model in folder dir (UTF-8), as a release downloads it (kFileName, kEncoderFileName).
  bool load(const std::string& dir, int threads);
  bool isLoaded() const;
  // The rate, in samples per second, the recording must be in (16 kHz for Qwen3-ASR).
  int sampleRate() const;
  // What was said in mono samples from -1 to 1, in Traditional Chinese as Taiwan writes it. words, the
  // user's own words (DictationWords), bias it toward their spelling. Empty when nothing was
  // recognized or it failed. One call at a time.
  std::string transcribe(const std::vector<float>& samples, const std::vector<std::string>& words);

  // Its files in a downloaded release's folder, kept as the models' are (NewestDownload, Models.h).
  static constexpr char kFileName[] = "speech.gguf";
  static constexpr char kEncoderFileName[] = "speech-encoder.gguf";

 private:
  struct Llama;
  std::unique_ptr<Llama> llama_;
};

}  // namespace ProjectKey
