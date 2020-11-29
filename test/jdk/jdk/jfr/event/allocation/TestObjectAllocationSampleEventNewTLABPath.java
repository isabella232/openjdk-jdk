/*
 * Copyright (c) 2020, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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
 */

package jdk.jfr.event.allocation;

import static java.lang.Math.floor;

import jdk.jfr.Recording;
import jdk.jfr.consumer.RecordedEvent;
import jdk.test.lib.jfr.EventNames;
import jdk.test.lib.jfr.Events;
import jdk.test.lib.Asserts;

/**
 * @test
 * @summary Test that an allocation sample event is triggered when an allocation takes the new TLAB path.
 * @key jfr
 * @requires vm.hasJFR
 * @library /test/lib
 * @run main/othervm -XX:+UseTLAB -XX:TLABSize=100k -XX:-ResizeTLAB -XX:TLABRefillWasteFraction=1 jdk.jfr.event.allocation.TestObjectAllocationSampleEventNewTLABPath
 * @run main/othervm -XX:+UseTLAB -XX:TLABSize=100k -XX:-ResizeTLAB -XX:TLABRefillWasteFraction=1 -Xint jdk.jfr.event.allocation.TestObjectAllocationSampleEventNewTLABPath
 */

/**
 * Test that an allocation sample event is triggered when an allocation takes the new TLAB path.
 * The test is done for default and interpreted mode (-Xint).
 *
 * To force objects to be allocated using the TLAB path:
 *      the size of TLAB is set to 100k (-XX:TLABSize=100k);
 *      the size of allocated objects is set to 100k minus 16 bytes overhead;
 *      max TLAB waste at refill is set to minimum (-XX:TLABRefillWasteFraction=1),
 *          to provoke a new TLAB creation.
 */
public class TestObjectAllocationSampleEventNewTLABPath {
    private final static String EVENT_NAME = EventNames.ObjectAllocationSample;

    private static final int BYTE_ARRAY_OVERHEAD = 16; // Extra bytes used by a byte array
    private static final int OBJECT_SIZE = 100 * 1024;
    private static final int OBJECT_SIZE_ALT = OBJECT_SIZE + 8; // Object size in case of disabled CompressedOops
    private static final int OBJECTS_TO_ALLOCATE = 100;
    private static final String BYTE_ARRAY_CLASS_NAME = new byte[0].getClass().getName();
    private static int eventCount;

    // Make sure allocation isn't dead code eliminated.
    public static byte[] tmp;

    public static void main(String[] args) throws Exception {
        Recording recording = new Recording();
        recording.enable(EVENT_NAME);
        recording.start();
        System.gc();
        for (int i = 0; i < OBJECTS_TO_ALLOCATE; ++i) {
            tmp = new byte[OBJECT_SIZE - BYTE_ARRAY_OVERHEAD];
        }
        recording.stop();
        for (RecordedEvent event : Events.fromRecording(recording)) {
            verify(event);
        }
        int minCount = (int) floor(OBJECTS_TO_ALLOCATE * 0.80);
        Asserts.assertGreaterThanOrEqual(eventCount, minCount, "Too few object samples allocated");
    }

    private static void verify(RecordedEvent event) {
        Asserts.assertTrue(event.hasField("allocatedSinceLast"));
        Asserts.assertTrue(event.hasField("skippedEvents"));
        if (Thread.currentThread().getId() != event.getThread().getJavaThreadId()) {
            return;
        }
        long allocationSize = Events.assertField(event, "allocationSize").atLeast(1L).getValue();
        String className = Events.assertField(event, "objectClass.name").notEmpty().getValue();
        if (className.equals(BYTE_ARRAY_CLASS_NAME) && (allocationSize == OBJECT_SIZE || allocationSize == OBJECT_SIZE_ALT)) {
            // Count all matching allocation samples.
            ++eventCount;
        }
    }
}
