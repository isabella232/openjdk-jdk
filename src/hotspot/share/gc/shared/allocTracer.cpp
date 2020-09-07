/*
 * Copyright (c) 2013, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#include "precompiled.hpp"
#include "gc/shared/allocTracer.hpp"
#include "runtime/atomic.hpp"
#include "runtime/handles.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/macros.hpp"
#if INCLUDE_JFR
#include "jfr/jfrEvents.hpp"
#include "jfr/support/jfrAdaptiveSampler.hpp"
#include "jfr/support/jfrAllocationTracer.hpp"
#endif

namespace {
  static THREAD_LOCAL size_t _skipped_allocations = 0;
  static THREAD_LOCAL size_t _skipped_samples = 0;
  static AdaptiveSampler* _jfr_sampler = NULL;

  static void send_allocation_sample(Klass* klass, HeapWord* obj, size_t tlab_size, size_t alloc_size, Thread* thread) {
    EventObjectAllocationSample event;
    if (event.should_commit()) {
      event.set_objectClass(klass);
      event.set_allocationSize(alloc_size);
      AdaptiveSampler* sampler = Atomic::load(&_jfr_sampler);
      if (sampler != NULL && sampler->should_sample()) {
        event.set_allocatedSinceLast(_skipped_allocations + tlab_size + alloc_size);
        event.set_foldedSamples(_skipped_samples + 1);
        event.commit();
        _skipped_allocations = 0;
        _skipped_samples = 0;
      } else {
        _skipped_samples++;
        _skipped_allocations += (tlab_size + alloc_size);
      }
    }
  }
}

// AdaptiveSampler* AllocTracer::_jfr_sampler = NULL;
// size_t THREAD_LOCAL AllocTracer::_skipped_allocations = 0;
// size_t THREAD_LOCAL AllocTracer::_skipped_samples = 0;

void AllocTracer::initialize_jfr_sampler(jlong target_samples_per_minute) {
  // use heuristic(?) to set the window duration based on the requested sample rate
  // 20ms window for >10k samples per minute; 50ms window for 1k-10k samples per minute; 500ms window for <1k samples per minute
  clock_t window_size_ms = target_samples_per_minute <= 10000 ? (target_samples_per_minute <= 1000 ? 500 : 50) : 20;
  size_t windows_per_minute = 60000 / window_size_ms;
  size_t target_samples_per_window = fmax((size_t)2, target_samples_per_minute / windows_per_minute);
  size_t sample_lookback_windows = windows_per_minute / 2;
  size_t budget_loobkack_windows = windows_per_minute * 0.75;
  
  AdaptiveSampler* sampler = NEW_C_HEAP_OBJ(AdaptiveSampler, mtInternal);
  ::new (sampler) AdaptiveSampler(window_size_ms, target_samples_per_window, sample_lookback_windows, budget_loobkack_windows);
  Atomic::store(&_jfr_sampler, sampler);
}

void AllocTracer::send_allocation_outside_tlab(Klass* klass, HeapWord* obj, size_t alloc_size, Thread* thread) {
  JFR_ONLY(JfrAllocationTracer tracer(obj, alloc_size, thread);)
  EventObjectAllocationOutsideTLAB event;
  EventObjectAllocationSample sample;
  if (event.should_commit()) {
    event.set_objectClass(klass);
    event.set_allocationSize(alloc_size);
    event.commit();
  }
  send_allocation_sample(klass, obj, 0, alloc_size, thread);
}

// void AllocTracer::send_allocation_sample(Klass* klass, HeapWord* obj, size_t tlab_size, size_t alloc_size, Thread* thread) {
//   EventObjectAllocationSample event;
//   if (event.should_commit()) {
//     event.set_objectClass(klass);
//     event.set_allocationSize(alloc_size);
//     AdaptiveSampler* sampler = Atomic::load(&_jfr_sampler);
//     if (sampler != NULL && sampler->should_sample()) {
//       event.set_allocatedSinceLast(_skipped_allocations + tlab_size + alloc_size);
//       event.set_foldedSamples(_skipped_samples + 1);
//       event.commit();
//       _skipped_allocations = 0;
//       _skipped_samples = 0;
//     } else {
//       _skipped_samples++;
//       _skipped_allocations += (tlab_size + alloc_size);
//     }
//   }
// }

void AllocTracer::send_allocation_in_new_tlab(Klass* klass, HeapWord* obj, size_t tlab_size, size_t alloc_size, Thread* thread) {
  JFR_ONLY(JfrAllocationTracer tracer(obj, alloc_size, thread);)
  EventObjectAllocationInNewTLAB event;
  if (event.should_commit()) {
    event.set_objectClass(klass);
    event.set_allocationSize(alloc_size);
    event.set_tlabSize(tlab_size);
    event.commit();
  }
  send_allocation_sample(klass, obj, tlab_size, alloc_size, thread);
}

void AllocTracer::send_allocation_requiring_gc_event(size_t size, uint gcId) {
  EventAllocationRequiringGC event;
  if (event.should_commit()) {
    event.set_gcId(gcId);
    event.set_size(size);
    event.commit();
  }
}
