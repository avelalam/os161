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

struct semaphore *male_sem;
struct semaphore *female_sem;
struct semaphore *matcher;
struct lock *locker;
	
void whalemating_init() {

	male_sem = sem_create("malesem", 0);
	KASSERT(male_sem!= NULL);
	female_sem = sem_create("femalesem", 0);
	KASSERT(female_sem!=NULL);
	matcher = sem_create("matchersem", 0);	
	KASSERT(matcher != NULL);
	locker = lock_create("locker");
	KASSERT(locker != NULL);
	return;
}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {
	return;
}

void
male(uint32_t index)
{	
	male_start(index);
	P(male_sem);
	male_end(index);
	
//	male_start(index);
//	V(male_sem);
//	V(male_sem);
//	P(female_sem);
//	P(matcher);	
//	male_end(index);
	(void)index;
	/*
	 * Implement this function by calling male_start and male_end when
	 * appropriate.
	 */
	return;
}

void
female(uint32_t index)
{	
	female_start(index);
	P(female_sem);
	female_end(index);

//	female_start(index);
//	V(female_sem);
//	V(female_sem);
//	P(male_sem);
//	P(matcher);
//	female_end(index);
		
	(void)index;
	/*
	 * Implement this function by calling female_start and female_end when
	 * appropriate.
	 */
	return;
}

void
matchmaker(uint32_t index)
{
	matchmaker_start(index);
	V(male_sem);
	V(female_sem);
	matchmaker_end(index);
	
//	matchmaker_start(index);
//	V(matcher);
//	V(matcher);
//	P(male_sem);
//	P(female_sem);
//	matchmaker_end(index);
	(void)index;
	/*
	 * Implement this function by calling matchmaker_start and matchmaker_end
	 * when appropriate.
	 */
	return;
}
