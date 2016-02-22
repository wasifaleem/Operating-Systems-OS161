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
 * Driver code is in kern/tests/synchprobs.c We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

static struct lock *wm_lock;
static struct cv *wm_male_available_cv, *wm_female_available_cv, *wm_male_matched_cv, *wm_female_matched_cv;
static volatile int wm_male_count = 0, wm_female_count = 0;

/*
 * Called by the driver during initialization.
 */

void whalemating_init() {
	wm_lock = lock_create("wm_lock");
	wm_male_available_cv = cv_create("wm_male_available_cv");
	wm_male_matched_cv = cv_create("wm_male_matched_cv");
	wm_female_available_cv = cv_create("wm_female_available_cv");
	wm_female_matched_cv = cv_create("wm_female_matched_cv");}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {
	lock_destroy(wm_lock);
	cv_destroy(wm_male_available_cv);
	cv_destroy(wm_male_matched_cv);
	cv_destroy(wm_female_available_cv);
	cv_destroy(wm_female_matched_cv);}

void
male(uint32_t index)
{
	male_start(index);

	lock_acquire(wm_lock);

	++wm_male_count;
	cv_signal(wm_male_available_cv, wm_lock);
	cv_wait(wm_male_matched_cv, wm_lock);
	--wm_male_count;

	lock_release(wm_lock);

	male_end(index);
}

void
female(uint32_t index)
{
	female_start(index);

	lock_acquire(wm_lock);

	++wm_female_count;
	cv_signal(wm_female_available_cv, wm_lock);
	cv_wait(wm_female_matched_cv, wm_lock);
	--wm_female_count;

	lock_release(wm_lock);

	female_end(index);
}

void
matchmaker(uint32_t index)
{
	matchmaker_start(index);

	lock_acquire(wm_lock);

	while (wm_male_count == 0) {
		cv_wait(wm_male_available_cv, wm_lock);
	}

	while (wm_female_count == 0) {
		cv_wait(wm_female_available_cv, wm_lock);
	}

	cv_signal(wm_male_matched_cv, wm_lock);
	cv_signal(wm_female_matched_cv, wm_lock);

	lock_release(wm_lock);

	matchmaker_end(index);
}
