/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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

#include <file.h>
#include <kern/errno.h>
#include <proc.h>
#include <addrspace.h>
#include <filetable.h>
#include <syscall.h>
#include <process.h>
#include <limits.h>
/*
 * Duplicates the currently running process.
 * Identical except for pid
 */

pid_t
sys_fork(struct trapframe *parenttf, pid_t *retaddr){

  //Allocates new process for child
  struct proc *child = kmalloc(sizeof(struct proc));
  if(child == NULL){
    return ENOMEM;
  }

  //Make a copy of the parenttf onto the heap to be passed to forkentry
  struct trapframe *temptf = kmalloc(sizeof(struct trapframe));
  if(temptf == NULL){
    kfree(child);
    return ENOMEM;
  }

  //Adds the process to the process table and gives it a pid
  int result = processtable_add(child);
  if(result){
    kfree(child);
    kfree(temptf);
    return result;
  }

  //For reference to the parent
  struct proc *parent = curthread->t_proc;

  //Assigns the new process' PPID to the caller's PID
  child->ppid = parent->pid;

  //Declare an addrspace as_copy to then copy into
  struct addrspace *tempas;
  result = as_copy(parent->p_addrspace, &tempas);
  if(result){
    kfree(child);
    kfree(temptf);
    return result;
  }
  child->p_addrspace = tempas;

  //Copy the parent filetable
  child->p_filetable = filetable_createcopy(parent->p_filetable);
  if(child->p_filetable == NULL){
    kfree(child);
    kfree(temptf);
    return ENOMEM;
  }

  //Copy cwd
  child->p_cwd = parent->p_cwd;
  VOP_INCREF(child->p_cwd);

  //Give the child a semaphore
  child->p_sem = sem_create("", 0);
  if(child->p_sem == NULL){
    kfree(temptf);
    filetable_destroy(child->p_filetable);
    kfree(child);
    return ENOMEM;
  }

  //Makes sure that it isn't simply copying address
  *temptf = *parenttf;

  result = thread_fork(curthread->t_name, child, enter_forked_process, (void *)temptf, 0);
  if(result){
    kfree(temptf);
    filetable_destroy(child->p_filetable);
    kfree(child);
    return ENOMEM;
  }
  // while(true){}
  //Ensures that the parent gets the expected return value
  *retaddr = child->pid;


  return 0;

}
