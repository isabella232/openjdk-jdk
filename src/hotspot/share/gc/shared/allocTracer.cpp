/*
 * Copyright (c) 2013, 2020, Oracle and/or its affiliates. All rights reserved.
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
#include "jfr/jfrEvents.hpp"
#include "runtime/handles.hpp"
#include "utilities/globalDefinitions.hpp"
#include "utilities/macros.hpp"
#if INCLUDE_JFR
#include "jfr/support/jfrAllocationTracer.hpp"
#endif

/*
 * Allocation events are throttled so not each event will get written to JFR.
 * _skipped_allocations and _skipped_events are maintained for tracking
 * information about discarded allocation events, and are included in an event
 * that is accepted.
 */
static THREAD_LOCAL size_t _skipped_allocations = 0;
static THREAD_LOCAL size_t _skipped_events = 0;

static void send_allocation_sample(Klass* klass, HeapWord* obj, size_t obj_alloc_size, size_t memory_alloc_size, Thread* thread) {
  EventObjectAllocationSample event;
  if (event.should_commit()) {
    event.set_objectClass(klass);
    event.set_allocationSize(obj_alloc_size);
    event.set_allocatedSinceLast(_skipped_allocations + memory_alloc_size);
    event.set_skippedEvents(_skipped_events);
    event.commit();
    _skipped_allocations = 0;
    _skipped_events = 0;
  } else {
    _skipped_events++;
    _skipped_allocations += memory_alloc_size;
  }
}

void AllocTracer::send_allocation_outside_tlab(Klass* klass, HeapWord* obj, size_t alloc_size, Thread* thread) {
  JFR_ONLY(JfrAllocationTracer tracer(obj, alloc_size, thread);)
  EventObjectAllocationOutsideTLAB event;
  if (event.should_commit()) {
    event.set_objectClass(klass);
    event.set_allocationSize(alloc_size);
    event.commit();
  }
  send_allocation_sample(klass, obj, alloc_size, alloc_size, thread);
}

void AllocTracer::send_allocation_in_new_tlab(Klass* klass, HeapWord* obj, size_t tlab_size, size_t alloc_size, Thread* thread) {
  JFR_ONLY(JfrAllocationTracer tracer(obj, alloc_size, thread);)
  EventObjectAllocationInNewTLAB event;
  if (event.should_commit()) {
    event.set_objectClass(klass);
    event.set_allocationSize(alloc_size);
    event.set_tlabSize(tlab_size);
    event.commit();
  }
  send_allocation_sample(klass, obj, alloc_size, tlab_size, thread);
}

void AllocTracer::send_allocation_requiring_gc_event(size_t size, uint gcId) {
  EventAllocationRequiringGC event;
  if (event.should_commit()) {
    event.set_gcId(gcId);
    event.set_size(size);
    event.commit();
  }
}
