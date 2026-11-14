// The advanced model (進階模型), which the settings windows download apart from the models: Gemma
// 4 E2B with its vocabulary cut to the tokens Chinese text needs, quantized to 4 bits (GGUF), run
// by llama.cpp on the CPU. It reads sentences far better than the small GPT-2 (SentenceModel.h)
// but takes a few hundred milliseconds for a composition's best paths, so the platforms ask it
// only when the typing pauses (Composer::rankingRequest). Only platforms built with llama.cpp
// compile this file.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace ProjectKey {

class AdvancedModel {
 public:
  AdvancedModel();
  ~AdvancedModel();
  AdvancedModel(const AdvancedModel&) = delete;
  AdvancedModel& operator=(const AdvancedModel&) = delete;

  bool load(const std::string& path, int threads);
  bool isLoaded() const;
  // log10 P of each sentence, from the start of a text and without its end, as the distillation's
  // teacher scored them; NaN for one it could not score. One call at a time.
  std::vector<double> log10Probabilities(const std::vector<std::string>& sentences);

  // Its file in a downloaded release's folder, kept as the models' are (NewestDownload, Models.h).
  static constexpr char kFileName[] = "advanced.gguf";

 private:
  struct Llama;
  std::unique_ptr<Llama> llama_;
};

}  // namespace ProjectKey
