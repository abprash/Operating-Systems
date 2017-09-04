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

struct lock *quad0_lock;
struct lock *quad1_lock;
struct lock *quad2_lock;
struct lock *quad3_lock;


/*
 * Called by the driver during initialization.
 */

void
stoplight_init() {

	quad0_lock = lock_create("0");
	quad1_lock = lock_create("1");
	quad2_lock = lock_create("2");
	quad3_lock = lock_create("3");

	return;
}

/*
 * Called by the driver during teardown.
 */

void stoplight_cleanup() {
	kprintf_n("cleaning up!");
	lock_destroy(quad0_lock);
	lock_destroy(quad1_lock);
	lock_destroy(quad2_lock);
	lock_destroy(quad3_lock);
	return;
}


struct lock *
dir_to_lock(uint32_t direction){
	if(direction == 0) return quad0_lock;
	if(direction == 1) return quad1_lock;
	if(direction == 2) return quad2_lock;
	if(direction == 3) return quad3_lock;
	return NULL;
}

void
turnright(uint32_t direction, uint32_t index)
{

	lock_acquire(dir_to_lock(direction));
	inQuadrant(direction, index);
	leaveIntersection(index);
	lock_release(dir_to_lock(direction));

	return;
}
void
gostraight(uint32_t direction, uint32_t index)
{
	uint32_t first;
	uint32_t second;
	if(direction > ((direction + 3) % 4)){
		first = direction;
		second = (direction + 3) % 4;
	}else{
		second = direction;
		first = (direction + 3) % 4;
	}

	lock_acquire(dir_to_lock(first));
	lock_acquire(dir_to_lock(second));

	inQuadrant(direction, index);
	inQuadrant((direction + 3) % 4, index);
	leaveIntersection(index);
	lock_release(dir_to_lock(first));
	lock_release(dir_to_lock(second));

	return;
}


void
turnleft(uint32_t direction, uint32_t index)
{
	uint32_t first = direction;
	uint32_t second = (direction + 3) % 4;
	uint32_t third = (direction + 6) % 4;
	uint32_t swap;

	if(second > first){
		swap = first;
		first = second;
		second = swap;
	}
	if(third > second){
		swap = second;
		second = third;
		third = swap;
	}
	if(second > first){
		swap = first;
		first = second;
		second = swap;
	}

	lock_acquire(dir_to_lock(first));
	lock_acquire(dir_to_lock(second));
	lock_acquire(dir_to_lock(third));

	inQuadrant(direction, index);
	inQuadrant((direction + 3) % 4, index);
	inQuadrant((direction + 6) % 4, index);
	leaveIntersection(index);
	lock_release(dir_to_lock(first));
	lock_release(dir_to_lock(second));
	lock_release(dir_to_lock(third));


	return;
	
}
