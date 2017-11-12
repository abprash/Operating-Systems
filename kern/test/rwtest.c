/*
 * All the contents of this file are overwritten during automated
 * testing. Please consider this before changing anything in this file.
 */

#include <types.h>
#include <lib.h>
#include <clock.h>
#include <thread.h>
#include <synch.h>
#include <test.h>
#include <kern/test161.h>
#include <spinlock.h>
#include <vm.h>

/*
 * Use these stubs to test your reader-writer locks.
 */

#define CREATELOOPS		8
#define NSEMLOOPS     63
#define NLOCKLOOPS    120
#define NCVLOOPS      5
#define NTHREADS      32
#define SYNCHTEST_YIELDER_MAX 16

static volatile unsigned long testval1;
static volatile unsigned long testval2;
static volatile unsigned long testval3;

struct spinlock status_lock;
static bool test_status = TEST161_FAIL;
struct rwlock *testrw;


static
void
rwtestthread(void *junk, unsigned long num)
{
	(void)junk;

	int i;

	for (i=0; i<NLOCKLOOPS; i++) {

		if(i % 3 != 0){
			rwlock_acquire_write(testrw);
		}else{
			rwlock_acquire_read(testrw);
		}

		kprintf_n("acquired");

		random_yielder(4);

		testval1 = num;
		testval2 = num*num;
		testval3 = num%3;

		if (testval2 != testval1*testval1) {
			goto fail;
		}
		random_yielder(4);

		if (testval2%3 != (testval3*testval3)%3) {
			goto fail;
		}
		random_yielder(4);

		if (testval3 != testval1%3) {
			goto fail;
		}
		random_yielder(4);

		if (testval1 != num) {
			goto fail;
		}
		random_yielder(4);

		if (testval2 != num*num) {
			goto fail;
		}
		random_yielder(4);

		if (testval3 != num%3) {
			goto fail;
		}
		random_yielder(4);

		// if (!(lock_do_i_hold(testlock))) {
		// 	goto fail;
		// }
		random_yielder(4);
		kprintf_n("releasing hopefully");
		if(i % 3 != 0){
			rwlock_release_write(testrw);
		}else{
			rwlock_release_read(testrw);
		}
	}

	return;

fail:
	if(i % 3 != 0){
		rwlock_release_write(testrw);
	}else{
		rwlock_release_read(testrw);
	}
}


int rwtest(int nargs, char **args) {
	(void)nargs;
	(void)args;

	int i, result;

	kprintf_n("Starting rwt1...\n");
	for(i = 0; i < CREATELOOPS; i++){
		testrw = rwlock_create("tester");
		if(testrw == NULL){
			panic("rwt1: rwlock_create failed\n");
		}
		if(i != CREATELOOPS - 1){
			rwlock_destroy(testrw);
		}
	}
	spinlock_init(&status_lock);
	test_status = TEST161_SUCCESS;

	for (i=0; i<NTHREADS; i++) {
		result = thread_fork("synchtest", NULL, rwtestthread, NULL, i);
		if (result) {
			panic("lt1: thread_fork failed: %s\n", strerror(result));
		}
	}



	success(TEST161_FAIL, SECRET, "rwt1");

	return 0;
}

int rwtest2(int nargs, char **args) {
	(void)nargs;
	(void)args;

	int* intarr[1000];
	for(int i = 0; i < 1000; i++){
		intarr[i] = kmalloc(sizeof(int));
	}
	(void)intarr;
	return 0;

}

int rwtest3(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("Starting rwt3...\n");
	success(TEST161_FAIL, SECRET, "rwt3");

	return 0;
}

int rwtest4(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("Starting rwt4...\n");
	success(TEST161_FAIL, SECRET, "rwt4");

	return 0;
}

int rwtest5(int nargs, char **args) {
	(void)nargs;
	(void)args;

	kprintf_n("Starting rwt5...\n");
	success(TEST161_FAIL, SECRET, "rwt5");

	return 0;
}
