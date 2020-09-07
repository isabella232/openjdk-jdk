#ifndef SHARE_JFR_SUPPORT_JFRADAPTIVESAMPLER_HPP
#define SHARE_JFR_SUPPORT_JFRADAPTIVESAMPLER_HPP

#include <cmath>

#include "runtime/atomic.hpp"
#include "runtime/mutex.hpp"
#include "runtime/samplerSupport.hpp"

/**
 * The adaptive sampler is guaranteeing a maximum number of samples picked per a certain time interval.
 * The maximum number is a soft limit which can be, in extreme situations be crossed but the overshoot
 * is usually staying within 15-20% of the requested limit (the actual number is affected by the 'sampling budget' size).
 * As for the implementation - the sampler is using fixed size time windows and adjusts the sampling rate/probability
 * for the next window based on what it learned in the past. While this strategy fares pretty well for a fairly stable system 
 * it can fail for bursty ones when an extremely bursty window can influence the moving average in a way that several subsequent
 * windows will end up undersampled. As a measure of compensation the adaptive sampler employs the concept of 'sampling budget'
 * The 'sampling budget' is working as a 'spike damper', smoothing out the extremes in a way that the overall target rate
 * is obeyed without highly over- or under-sampled winows.
 */

class SamplerWindow : public CHeapObj<mtInternal> {
    private:
    const bool _sample_all;
    const double _probability;
    const jlong _start_ticks;
    const jlong _window_duration_ticks;
    const size_t _samples_budget;

    volatile size_t _running_count;
    volatile size_t _sample_count;

    static jlong jfr_ticks();

    public:
    SamplerWindow(double probability, size_t samples_budget, jlong duration_ns);
    bool should_sample();

    inline
    const size_t sample_count() {
        size_t count = Atomic::load(&_sample_count);
        return count <= _samples_budget ? count : _samples_budget;
    }

    inline
    const size_t total_count() {
        return Atomic::load(&_running_count);
    }

    /**
     * Ratio between the requested and the measured winow duration
     */ 
    inline
    const double adjustment_factor() {
        return (double)_window_duration_ticks / (jfr_ticks() - _start_ticks);
    }

    const bool is_expired();
};

class AdaptiveSampler : public CHeapObj<mtInternal> {
    private:
    static double compute_interval_alpha(size_t interval);

    const jlong _window_duration;
    const size_t _samples_per_window;
    const double _window_lookback_alpha;
    const size_t _budget_lookback_cnt;
    const double _budget_lookback_alpha;

    double _samples_budget;
    double _probability;

    double _avg_samples;
    size_t _avg_count;

    // synchronizes operations mutating the _window variable
    Mutex _window_mutex;

    SamplerWindow* _window;

    // needs to be called under _window_mutex
    void recalculate_averages();
    void rotate_window();

    public:
    AdaptiveSampler(size_t window_duration, size_t samples_per_window, size_t window_lookback_cnt, size_t budget_lookback_cnt);
    ~AdaptiveSampler();

    bool should_sample();
};

#endif // SHARE_JFR_SUPPORT_JFRADAPTIVESAMPLER_HPP