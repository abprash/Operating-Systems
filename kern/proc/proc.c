/*
 * Copyright (c) 2013
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
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <filetable.h>
#include <vnode.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <limits.h>
#include <synch.h>
#include <kern/errno.h>

/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

/*
 * The process table for the userspace processes, indexed 0-MAX_PID
 */

static int pt_lastindex;

struct proc *processtable[128];

struct lock *ptlock;

static bool ptlock_created;

static int pidcounter;

/*
 * Lock for processtable modification synchronization
 */


/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	(void)name;
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	proc->p_sem = sem_create("p sem", 0);
	if(proc->p_sem == NULL){
		kfree(proc->p_name);
		kfree(proc);
		return NULL;
	}

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	//Associates a new filetable for every process created
	proc->p_filetable = filetable_create();

	// Add's the process to the processtable and sets it PID
	processtable_add(proc);

	proc->exstatus = false;
	proc->excode = 0;



	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	// kprintf("\nDestroying Process: %d\n", proc->pid);
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		VOP_DECREF(proc->p_cwd);
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}
	//Prevents race condition in which we are freeing addrspace refernces in free_kpages
	while(proc->p_numthreads != 0){}
	KASSERT(proc->p_numthreads == 0);
	filetable_destroy(proc->p_filetable);
	sem_destroy(proc->p_sem);
	for(int i = 0; i < PROC_MAX; i++){
    if(processtable[i] != NULL && processtable[i]->pid == proc->pid){
      processtable[i] = NULL;
      break;
    }
  }
	// kprintf("After removal:\n");
	// processtable_print();
	spinlock_cleanup(&proc->p_lock);
	kfree(proc);
}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
}

void
proctable_bootstrap(){
	// Initializes our processtable and pidcounter
	// spinlock_init(ptlock);
	pt_lastindex = 0;
	ptlock_created = false;
	pidcounter = 2;
	// processtable = kmalloc(sizeof(struct proc *) * PROC_MAX);
	// for(int i = 0; i < PROC_MAX; i++){
	// 	processtable[i] = NULL;
	// }
}

void
proctable_lock_bootstrap(){
	ptlock = lock_create("ptable");
	lock_acquire(ptlock);
	ptlock_created = true;
	lock_release(ptlock);
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{


	// processtable = kmalloc(sizeof(struct proc *) * PROC_MAX);
	// for(int i = 0; i < PROC_MAX; i++){
	// 	processtable[i] = NULL;
	// }

	struct proc *newproc;
	struct filehandle *stdin;
	struct filehandle *stdout;
	struct filehandle *stderr;
	struct vnode *vin = NULL;
	struct vnode *vout = NULL;
	struct vnode *verr = NULL;

	int result;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	if(newproc->p_filetable == NULL){
		proc_destroy(newproc);
		return NULL;
	}

	//TODO THE ORDER OF THESE EFFECTS WHICH ONE IS FUCKED UP MEANING THERE IS SOME RACE CONDITION
	stderr = filehandle_create("stderr");
	stdin = filehandle_create("stdin");
	stdout = filehandle_create("stdout");
	if(stdout == NULL){
		//filetable_destroy(newproc->p_filetable);
		proc_destroy(newproc);
		return NULL;
	}

	char *con1 = kstrdup("con:");

	result = vfs_open(con1, O_RDONLY, 0, &vin);
	if(result){
		//filetable_destroy(newproc->p_filetable);
		proc_destroy(newproc);
		return NULL;
	}

	// kfree(con);
	stdin->fh_fileobj = vin;
	filetable_add(newproc->p_filetable, stdin);

	char *con2 = kstrdup("con:");

	result = vfs_open(con2, O_WRONLY, 0, &vout);
	if(result){
		//filetable_destroy(newproc->p_filetable);
		proc_destroy(newproc);
		return NULL;
	}

	// kfree(con);
	stdout->fh_fileobj = vout;
	filetable_add(newproc->p_filetable, stdout);

	char *con3 = kstrdup("con:");

	result = vfs_open(con3, O_WRONLY, 0, &verr);
	if(result){
		//filetable_destroy(newproc->p_filetable);
		proc_destroy(newproc);
		return NULL;
	}
	//freeing the con
	kfree(con1);
	kfree(con2);
	kfree(con3);
	stderr->fh_fileobj = verr;
	filetable_add(newproc->p_filetable, stderr);

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */

	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;
	proc->p_addrspace = newas;
	spinlock_release(&proc->p_lock);
	return oldas;
}

int
processtable_add(struct proc *proc){
	//Gross because i implemented read lock read...getting desparate
	bool spot = false;
	if(pt_lastindex == PROC_MAX) pt_lastindex = 0;
	for(int i = pt_lastindex; i < PROC_MAX; i++){
		if(processtable[i] == NULL){
			spot = true;
			if(ptlock_created) lock_acquire(ptlock);
			for(int y = i; y < PROC_MAX; y++){
				if(processtable[y] == NULL){
					processtable[y] = proc;
					proc->pid = (pid_t)(pidcounter++);
					pt_lastindex = y + 1;
					if(ptlock_created) lock_release(ptlock);
					return 0;
				}
			}
			for(int y = 0; y < PROC_MAX; y++){
				if(processtable[y] == NULL){
					processtable[y] = proc;
					proc->pid = (pid_t)(pidcounter++);
					pt_lastindex = y + 1;
					if(ptlock_created) lock_release(ptlock);
					return 0;
				}
			}
		}
	}
	proc->pid = (pid_t) -1;
	if(ptlock_created && spot) lock_release(ptlock);
	return ENPROC;
}

// void
// processtable_print(){

// 	for(int i = 0; i < 30; i++){
// 		if(processtable[i] == NULL) kprintf("N, ");
// 		else kprintf("%d, ", processtable[i]->pid);
// 	}
// 	kprintf("\n");
// }
