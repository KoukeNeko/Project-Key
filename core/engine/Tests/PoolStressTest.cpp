// Stress test for the thread pool in SentenceModel.cpp (class Pool), which runs the sentence
// model's matrix products. Usage: PoolStressTest [seconds, default 60] [spinning threads, default 0]
//
// Runs of random size back to back, with spinning threads taking cores from the pool's workers so
// that one is late. A worker late from one call must take an index of the next call only with that
// call's task, and every block must run exactly once: the test exits 1 when one ran twice or not
// at all, and 3 when run() does not return for 10 seconds. The build script runs it for a few seconds.
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

}  // namespace

int main(int argc, char** argv) {
  int seconds = argc > 1 ? std::atoi(argv[1]) : 60;
  int spinners = argc > 2 ? std::atoi(argv[2]) : 0;

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
    ProjectKey::Pool::Shared().run(count, task);
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
  std::printf("pool: %llu calls in %d s with %d spinning threads: %llu blocks ran twice or more, %llu did not run\n",
              static_cast<unsigned long long>(calls.load()), seconds, spinners, static_cast<unsigned long long>(repeated.load()),
              static_cast<unsigned long long>(missed.load()));
  return repeated || missed ? 1 : 0;
}
