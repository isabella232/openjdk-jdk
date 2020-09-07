#include "precompiled.hpp"
#include "jfr/support/jfrAdaptiveSampler.hpp"
#include "jfr/utilities/jfrTime.hpp"
#include "jfr/utilities/jfrTimeConverter.hpp"
#include "runtime/mutexLocker.hpp"
#include "utilities/globalDefinitions.hpp"

namespace {
  static THREAD_LOCAL SamplerSupport* _sampler_support = NULL;

  static SamplerSupport* get_sampler_support() {
    if (!_sampler_support) {
      _sampler_support = NEW_C_HEAP_OBJ(SamplerSupport, mtInternal);
      ::new (_sampler_support) SamplerSupport(true);
    }
    return _sampler_support;
  }
}

SamplerWindow::SamplerWindow(double probability, size_t samples_budget, jlong duration_ns) :
  _sample_all(probability == 1.0), _probability(probability), _start_ticks(jfr_ticks()), _window_duration_ticks(JfrTimeConverter::nanos_to_countertime(duration_ns)), _samples_budget(samples_budget) {
  Atomic::store(&_running_count, (size_t)0);
  Atomic::store(&_sample_count, (size_t)0);
}

jlong SamplerWindow::jfr_ticks() {
  return ((jlong (*)())JfrTime::time_function())();
}

bool SamplerWindow::should_sample() {
  Atomic::inc(&_running_count, memory_order_acq_rel);
  double n_rand = _sample_all ? -1 : get_sampler_support()->next_random_uniform(); // if probability is 100% just ignore threshold and always pass
  if (n_rand < _probability) {
    if (Atomic::add(&_sample_count, (size_t)1, memory_order_acq_rel) <= _samples_budget) {
      return true;
    }
  }
  return false;
}

const bool SamplerWindow::is_expired() {
  jlong ticks = jfr_ticks();
  return ticks - _start_ticks > _window_duration_ticks;
}

AdaptiveSampler::AdaptiveSampler(size_t window_duration, size_t samples_per_window, size_t window_lookback_cnt, size_t budget_lookback_cnt) :
_window_duration(window_duration * 1000000), _samples_per_window(samples_per_window),
_window_lookback_alpha(compute_interval_alpha(window_lookback_cnt)), _budget_lookback_cnt(budget_lookback_cnt),
_budget_lookback_alpha(compute_interval_alpha(budget_lookback_cnt)),  
_probability(0.1), _avg_count(0), _window_mutex(Mutex::special, "Sampler mutex", false, Mutex::_safepoint_check_never) {
  MutexLocker ml(&_window_mutex, Mutex::_no_safepoint_check_flag);
  _samples_budget = _samples_per_window * (1 + budget_lookback_cnt);
  _avg_samples = std::numeric_limits<double>::quiet_NaN();
  _window = NEW_C_HEAP_OBJ(SamplerWindow, mtInternal);
  ::new (_window) SamplerWindow(_probability, _samples_budget, _window_duration);
}

double AdaptiveSampler::compute_interval_alpha(size_t interval) {
    return 1 - std::pow(interval, (double)-1 / (double)interval);
}

// needs to be called under _window_mutex
void AdaptiveSampler::recalculate_averages() {
  double adjustment_factor = _window->adjustment_factor();
  double samples = _window->sample_count() * adjustment_factor;
  double total_count = _window->total_count() * adjustment_factor;

  _avg_samples = std::isnan(_avg_samples) ? samples : _avg_samples + _budget_lookback_alpha * (samples - _avg_samples);
  _samples_budget = fmax<double>(_samples_per_window - _avg_samples, 0) * _budget_lookback_cnt;

  // fprintf(stdout, "=== avg samples: %f, samples: %f, adjustment: %f\n", _avg_samples, samples, adjustment_factor);

  if (_avg_count == 0) {
    _avg_count = total_count;
  } else {
    // need to convert int '*_count' variables to double to prevent bit overflow
    _avg_count = _avg_count + _window_lookback_alpha * ((double)total_count - (double)_avg_count);
  }
}

void AdaptiveSampler::rotate_window() {
  // will be called only when the previous window has expired
  MutexLocker ml(&_window_mutex, Mutex::_no_safepoint_check_flag);
  SamplerWindow* prev_window = _window;
  if (_window != NULL && _window->is_expired()) { // re-check if expired when under mutex
    recalculate_averages();

    if (_avg_count == 0) {
      _probability = 1;
    } else {
      double p = (_samples_per_window + _samples_budget) / (double)_avg_count;
      _probability = p <= 1 ? p : 1;
    }

    // fprintf(stdout, "=== rotate window: budget = %f, probability = %f\n", _samples_budget, _probability);
    _window = NEW_C_HEAP_OBJ(SamplerWindow, mtInternal);
    ::new (_window) SamplerWindow(_probability, _samples_budget, _window_duration);
    delete prev_window;
  }
}

bool AdaptiveSampler::should_sample() {
  if (_window->is_expired()) {
    rotate_window();
  }
  return _window->should_sample();
}

AdaptiveSampler::~AdaptiveSampler() {
  MutexLocker ml(&_window_mutex, Mutex::_no_safepoint_check_flag);
  delete _window;
  _window = NULL;
}