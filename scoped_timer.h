#ifndef SCOPED_TIMER_H
#define SCOPED_TIMER_H

// George Liontos' timer function

#include <chrono>
#include <iostream>

#define MEASURE_SCOPE(scope_name)                                                     \
  Scoped_Timer timer {scope_name}

class Scoped_Timer {
public:
  using clock_type = std::chrono::steady_clock;

  explicit Scoped_Timer(const char *function)
      : function_{function}, start_{clock_type::now()} {}

  ~Scoped_Timer() {
    using namespace std::chrono;
    const auto stop = clock_type::now();
    const auto duration = stop - start_;
    const auto ms = duration_cast<milliseconds>(duration).count();
    std::cout << ms << " ms " << function_ << std::endl;
  }

private:
  const char *function_{};
  const clock_type::time_point start_{};
};

#endif  // SCOPED_TIMER_H
