/*
 * Copyright (c) 2001, 2002, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver code is in kern/tests/synchprobs.c We will replace that file. This
 * file is yours to modify as you see fit.
 *
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is, of
 * course, stable under rotation)
 *
 *   |0 |
 * -     --
 *    01  1
 * 3  32
 * --    --
 *   | 2|
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X first.
 * The semantics of the problem are that once a car enters any quadrant it has
 * to be somewhere in the intersection until it call leaveIntersection(),
 * which it should call while in the final quadrant.
 *
 * As an example, let's say a car approaches the intersection and needs to
 * pass through quadrants 0, 3 and 2. Once you call inQuadrant(0), the car is
 * considered in quadrant 0 until you call inQuadrant(3). After you call
 * inQuadrant(2), the car is considered in quadrant 2 until you call
 * leaveIntersection().
 *
 * You will probably want to write some helper functions to assist with the
 * mappings. Modular arithmetic can help, e.g. a car passing straight through
 * the intersection entering from direction X will leave to direction (X + 2)
 * % 4 and pass through quadrants X and (X + 3) % 4.  Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in synchprobs.c to record their progress.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>
#include <stdarg.h>

#define NUM_QUADRANTS 4
#define MAX_CONCURRENT_INTERSECTION_CARS 3

static struct semaphore *intersection_semaphore;   // Allow multiple cars to enter intersection
static struct lock *quadrant_lock[NUM_QUADRANTS];  // Enforce that no two cars may be in the same quadrant at the same time

void navigate_to_quadrants(unsigned int index, int count,...);

/*
 * Called by the driver during initialization.
 */

void
stoplight_init() {
	intersection_semaphore = sem_create("intersection_semaphore", MAX_CONCURRENT_INTERSECTION_CARS);
	for (int i = 0; i< NUM_QUADRANTS; i++) {
		char name[14];
		snprintf(name, 14, "quadrant_%d_lk", i);
		quadrant_lock[i] = lock_create(name);
	}
}

/*
 * Called by the driver during teardown.
 */

void stoplight_cleanup() {
	sem_destroy(intersection_semaphore);
	for (int i = 0; i< NUM_QUADRANTS; i++) {
		lock_destroy(quadrant_lock[i]);
	}
}

void
turnright(uint32_t direction, uint32_t index)
{
	P(intersection_semaphore);

	navigate_to_quadrants(index, 1,  direction);

	V(intersection_semaphore);
}
void
gostraight(uint32_t direction, uint32_t index)
{
	P(intersection_semaphore);

	navigate_to_quadrants(index, 2,  direction, (direction + 3) % 4);

	V(intersection_semaphore);
}
void
turnleft(uint32_t direction, uint32_t index)
{
	P(intersection_semaphore);

	navigate_to_quadrants(index, 3,  direction, (direction + 3) % 4, (direction + 2) % 4);

	V(intersection_semaphore);
}

/**
 * Allows car to navigate quadrant safely.
 * Accepts and count of quadrants and their index as var-args.
 *
 * Ex: navigate_to_quadrants(index, 3,  enter_quad, pass_through_quad, exit_quad)
 */
void
navigate_to_quadrants(unsigned int index, int count, ...) {
	va_list ap;
	va_start(ap, count);

	int release = -1;
	int end_quad = count - 1;

	for (int i = 0; i < count; i++) {
		unsigned int quad = va_arg(ap, unsigned int);

		lock_acquire(quadrant_lock[quad]);
		kprintf_n("lock %d acquired\n", quad);
		inQuadrant(quad, index);

		if (release != -1) {
			kprintf_n("lock %d released\n", release);
			lock_release(quadrant_lock[release]);
		}

		if (i == end_quad) {
			leaveIntersection(index);
			kprintf_n("lock %d released\n", quad);
			lock_release(quadrant_lock[quad]);
		}

		release = quad;
	}
	va_end (ap);
}