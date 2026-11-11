// Stress test for the thread pool in SentenceModel.cpp (class Pool), which runs the sentence
// model's matrix products. Usage: PoolStressTest [seconds, default 60] [spinning threads, default 0]
//
// Not in the build scripts: it is expected to fail while the pool has the race it was written for.
// Suspected mechanism: work() reads next_, count_ and task_ without the lock, so a worker that is
// late from one call can take an index of the next call while run() is still setting it up. That
// runs one task twice, and a task_ of nullptr or a done_ past count_ follows; finished_.wait()
// then never sees done_ == count_ and run() never returns.
//
// SentenceModel.cpp is included whole so that the test exercises the real Pool (it lives in an
// anonymous namespace), not a copy of it.
#undef NDEBUG
#include "../SentenceModel.cpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

namespace {

using Clock = std::chrono::steady_clock;

constexpr int kMaxCount = 64;
// Static, so a worker that is late cannot touch freed memory; a late run of an index shows up as a count of 2.
std::atomic<int> ran[kMaxCount];

// About what a block of a matrix product costs, in a few sizes: from nothing to some microseconds.
void burn(int rounds) {
  volatile float x = 1.0f;
  for (int i = 0; i < rounds; i++) x = x * 1.0000001f + 0.5f;
}

// The same pool with every read of its shared state under the lock. It is here only to show that
// the race is the cause: the test passes with it ("fixed" on the command line) and hangs without.
class FixedPool {
 public:
  static FixedPool& Shared() {
    static FixedPool pool(std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 0, 7));
    return pool;
  }
  void run(int count, const std::function<void(int)>& task) {
    if (workers_.empty() || count <= 1) {
      for (int i = 0; i < count; i++) task(i);
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      task_ = &task;
      count_ = count;
      next_ = 0;
      done_ = 0;
      generation_++;
    }
    wake_.notify_all();
    work();
    std::unique_lock<std::mutex> lock(mutex_);
    finished_.wait(lock, [&] { return done_ == count_; });
    task_ = nullptr;
  }
  ~FixedPool() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_all();
    for (std::thread& worker : workers_) worker.join();
  }

 private:
  explicit FixedPool(int threads) {
    for (int i = 0; i < threads; i++) {
      workers_.emplace_back([this] {
        unsigned seen = 0;
        for (;;) {
          {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || generation_ != seen; });
            if (stopping_) return;
            seen = generation_;
          }
          work();
        }
      });
    }
  }
  void work() {
    for (;;) {
      const std::function<void(int)>* task;
      int i;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (next_ >= count_) return;
        i = next_++;
        task = task_;
      }
      (*task)(i);
      std::lock_guard<std::mutex> lock(mutex_);
      if (++done_ == count_) finished_.notify_all();
    }
  }
  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable wake_, finished_;
  const std::function<void(int)>* task_ = nullptr;
  int next_ = 0, count_ = 0, done_ = 0;
  unsigned generation_ = 0;
  bool stopping_ = false;
};

}  // namespace

int main(int argc, char** argv) {
  int seconds = argc > 1 ? std::atoi(argv[1]) : 60;
  int spinners = argc > 2 ? std::atoi(argv[2]) : 0;
  bool fixed = argc > 3 && std::string(argv[3]) == "fixed";

  std::atomic<uint64_t> calls{0}, repeated{0}, missed{0};
  std::atomic<bool> finished{false};
  std::atomic<int> currentCount{0};

  // The pool's own threads sit idle between calls, as they do under the composer; the spinners
  // take cores from them, which is what makes a worker late.
  std::vector<std::thread> hogs;
  for (int i = 0; i < spinners; i++) {
    hogs.emplace_back([&] {
      volatile uint64_t n = 0;
      while (!finished) n = n + 1;
    });
  }

  std::thread watchdog([&] {
    uint64_t seen = ~0ull;
    Clock::time_point since = Clock::now();
    while (!finished) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      if (calls != seen) {
        seen = calls;
        since = Clock::now();
      } else if (Clock::now() - since > std::chrono::seconds(10)) {
        std::printf("HANG: no run() returned for 10 s after %llu calls (count %d); repeated tasks so far %llu, missed %llu\n",
                    static_cast<unsigned long long>(calls.load()), currentCount.load(),
                    static_cast<unsigned long long>(repeated.load()), static_cast<unsigned long long>(missed.load()));
        std::fflush(stdout);
        std::_Exit(3);
      }
    }
  });

  std::mt19937 random(12345);
  const int rounds[] = {0, 0, 20, 200, 2000};
  Clock::time_point start = Clock::now(), reported = start;
  int shown = 0;
  while (Clock::now() - start < std::chrono::seconds(seconds)) {
    int count = 2 + static_cast<int>(random() % (kMaxCount - 1));  // run() with one block runs inline
    int work = rounds[random() % 5];
    for (int i = 0; i < count; i++) ran[i] = 0;
    currentCount = count;
    // A new std::function each call, as MatMul passes its lambda.
    std::function<void(int)> task = [&](int block) {
      ran[block]++;
      burn(work);
    };
    if (fixed) {
      FixedPool::Shared().run(count, task);
    } else {
      ProjectKey::Pool::Shared().run(count, task);
    }
    for (int i = 0; i < count; i++) {
      int n = ran[i];
      if (n == 1) continue;
      (n > 1 ? repeated : missed)++;
      if (shown++ < 5) std::printf("call %llu: block %d of %d ran %d times\n", static_cast<unsigned long long>(calls.load()), i, count, n);
    }
    calls++;
    if (Clock::now() - reported > std::chrono::seconds(5)) {
      reported = Clock::now();
      std::printf("%llu calls, %llu repeated, %llu missed\n", static_cast<unsigned long long>(calls.load()),
                  static_cast<unsigned long long>(repeated.load()), static_cast<unsigned long long>(missed.load()));
      std::fflush(stdout);
    }
  }
  finished = true;
  watchdog.join();
  for (std::thread& hog : hogs) hog.join();
  std::printf("%s pool: %llu calls in %d s with %d spinning threads: %llu blocks ran twice or more, %llu did not run\n",
              fixed ? "fixed" : "real", static_cast<unsigned long long>(calls.load()), seconds, spinners, static_cast<unsigned long long>(repeated.load()),
              static_cast<unsigned long long>(missed.load()));
  return repeated || missed ? 1 : 0;
}
