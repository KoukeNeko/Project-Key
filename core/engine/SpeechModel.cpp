#include "SpeechModel.h"

#include <filesystem>
#include <mutex>

#include "Utf8.h"
#include "llama.h"
#include "mtmd-helper.h"
#include "mtmd.h"

namespace ProjectKey {

namespace {

// The audio's tokens (about 13 a second, so a minute's recording) and what is said in them.
constexpr int kContextTokens = 2048;
constexpr int kBatchTokens = 512;
constexpr int kMaxOutputTokens = 512;
// The user's own words the prompt names, most recent first; more make every transcription slower.
constexpr size_t kMaxWords = 50;

}  // namespace

struct SpeechModel::Llama {
  llama_model* model = nullptr;
  llama_context* context = nullptr;
  mtmd_context* encoder = nullptr;
  llama_sampler* sampler = nullptr;
  ~Llama() {
    if (sampler) llama_sampler_free(sampler);
    if (encoder) mtmd_free(encoder);
    if (context) llama_free(context);
    if (model) llama_model_free(model);
  }
};

SpeechModel::SpeechModel() = default;
SpeechModel::~SpeechModel() = default;

bool SpeechModel::load(const std::string& dir, int threads) {
  static std::once_flag backend;
  std::call_once(backend, [] {
    // They would write to the app's stderr.
    llama_log_set([](ggml_log_level, const char*, void*) {}, nullptr);
    mtmd_helper_log_set([](ggml_log_level, const char*, void*) {}, nullptr);
    llama_backend_init();
  });
  std::filesystem::path folder = Path(dir);  // UTF-8, as llama.cpp takes its paths
  auto llama = std::make_unique<Llama>();
  llama_model_params modelParams = llama_model_default_params();
  modelParams.n_gpu_layers = 0;
  llama->model = llama_model_load_from_file(Utf8(folder / kFileName).c_str(), modelParams);
  if (!llama->model) return false;
  llama_context_params contextParams = llama_context_default_params();
  contextParams.n_ctx = kContextTokens;
  contextParams.n_batch = kBatchTokens;
  contextParams.n_threads = threads;
  contextParams.n_threads_batch = threads;
  llama->context = llama_init_from_model(llama->model, contextParams);
  if (!llama->context) return false;
  mtmd_context_params encoderParams = mtmd_context_params_default();
  encoderParams.use_gpu = false;
  encoderParams.print_timings = false;
  encoderParams.n_threads = threads;
  encoderParams.warmup = false;
  llama->encoder = mtmd_init_from_file(Utf8(folder / kEncoderFileName).c_str(), llama->model, encoderParams);
  if (!llama->encoder || !mtmd_support_audio(llama->encoder)) return false;
  llama->sampler = llama_sampler_init_greedy();
  llama_ = std::move(llama);
  return true;
}

bool SpeechModel::isLoaded() const { return llama_ != nullptr; }

int SpeechModel::sampleRate() const { return llama_ ? mtmd_get_audio_sample_rate(llama_->encoder) : 16000; }

std::string SpeechModel::transcribe(const std::vector<float>& samples, const std::vector<std::string>& words) {
  if (!llama_ || samples.empty()) return {};
  // Asked in Traditional Chinese, it writes Traditional Chinese; it would write Simplified otherwise.
  std::string ask = std::string(mtmd_default_marker()) + "請以台灣繁體中文轉寫。";
  if (!words.empty()) {
    ask += "可能用到的詞：";
    for (size_t i = 0; i < words.size() && i < kMaxWords; i++) ask += (i ? "、" : "") + words[i];
    ask += "。";
  }
  llama_chat_message message{"user", ask.c_str()};
  const char* chatTemplate = llama_model_chat_template(llama_->model, nullptr);
  std::vector<char> prompt(ask.size() + 256);
  int length = llama_chat_apply_template(chatTemplate, &message, 1, /*add_ass=*/true, prompt.data(), static_cast<int>(prompt.size()));
  if (length > static_cast<int>(prompt.size())) {
    prompt.resize(length);
    length = llama_chat_apply_template(chatTemplate, &message, 1, true, prompt.data(), static_cast<int>(prompt.size()));
  }
  if (length < 0) return {};

  mtmd_bitmap* audio = mtmd_bitmap_init_from_audio(samples.size(), samples.data());
  mtmd_input_chunks* chunks = mtmd_input_chunks_init();
  mtmd_input_text text{prompt.data(), static_cast<size_t>(length), /*add_special=*/true, /*parse_special=*/true};
  const mtmd_bitmap* bitmaps[] = {audio};
  llama_memory_clear(llama_get_memory(llama_->context), true);
  llama_sampler_reset(llama_->sampler);
  llama_pos past = 0;
  bool ok = mtmd_tokenize(llama_->encoder, chunks, &text, bitmaps, 1) == 0 &&
            mtmd_helper_eval_chunks(llama_->encoder, llama_->context, chunks, 0, 0, kBatchTokens, /*logits_last=*/true, &past) == 0;
  mtmd_input_chunks_free(chunks);
  mtmd_bitmap_free(audio);
  if (!ok) return {};

  const llama_vocab* vocab = llama_model_get_vocab(llama_->model);
  std::string said;
  for (int i = 0; i < kMaxOutputTokens && past < kContextTokens; i++) {
    llama_token token = llama_sampler_sample(llama_->sampler, llama_->context, -1);
    if (llama_vocab_is_eog(vocab, token)) break;
    char piece[256];
    int n = llama_token_to_piece(vocab, token, piece, sizeof piece, 0, /*special=*/true);
    if (n > 0) said.append(piece, n);
    llama_batch batch = llama_batch_get_one(&token, 1);
    if (llama_decode(llama_->context, batch) != 0) return {};
    past++;
  }
  // Qwen3-ASR names the language first: "language Chinese<asr_text>…".
  static constexpr char kTextMark[] = "<asr_text>";
  size_t mark = said.find(kTextMark);
  return mark == std::string::npos ? said : said.substr(mark + sizeof kTextMark - 1);
}

}  // namespace ProjectKey
