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

/*
 * Called by the driver during initialization.
 */

struct cv *male_cv;
struct semaphore *male_sem;
struct lock *male_lock;
struct cv *female_cv;
struct semaphore *female_sem;
struct lock *female_lock;

volatile unsigned male_count;
volatile unsigned female_count;
volatile unsigned matchmaker_count;

void whalemating_init() {

	male_cv = cv_create("malecv");
	female_cv = cv_create("femalecv");

	male_sem = sem_create("malesem", 0);
	female_sem = sem_create("femalesem", 0);

	male_lock = lock_create("malelock");
	female_lock = lock_create("female_lock");

	male_count = 0;
	female_count = 0;
	matchmaker_count = 0;

	return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {

	kprintf_n("cleanup time!");

	sem_destroy(male_sem);
	sem_destroy(female_sem);

	lock_destroy(male_lock);
	lock_destroy(female_lock);

	cv_destroy(male_cv);
	cv_destroy(female_cv);

	return;
}

void
male(uint32_t index)
{

		/*
	 * Implement this function by calling male_start and male_end when
	 * appropriate.
	 */
	lock_acquire(male_lock);
	male_start(index);

	V(male_sem);

	if(female_count <= 0 || matchmaker_count <= 0){
		cv_wait(male_cv, male_lock);
	}

	male_end(index);

	lock_release(male_lock);
	return;
}

void
female(uint32_t index)
{
	/*
	 * Implement this function by calling female_start and female_end when
	 * appropriate.
	 */
	lock_acquire(female_lock);
	female_start(index);

	V(female_sem);

	if(male_count <= 0 || matchmaker_count <= 0){
		cv_wait(female_cv, female_lock);
	}

	female_end(index);

	lock_release(female_lock);
	return;
}

void
matchmaker(uint32_t index)
{
	/*
	 * Implement this function by calling matchmaker_start and matchmaker_end
	 * when appropriate.
	 */

   matchmaker_start(index);

   P(male_sem);
	 P(female_sem);

	 lock_acquire(male_lock);
	 cv_signal(male_cv, male_lock);
	 lock_release(male_lock);

	 lock_acquire(female_lock);
	 cv_signal(female_cv, female_lock);
	 lock_release(female_lock);

   matchmaker_end(index);



   return;
}
