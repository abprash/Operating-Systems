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

#include <types.h>
#include <syscall.h>
#include <file.h>
#include <filetable.h>
#include <vfs.h>
#include <uio.h>
#include <proc.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <copyinout.h>
#include <kern/limits.h>
/*
 * System call: Clones the filehandle oldfd onto the new filehandle newfd.
 */


int
sys_dup2(int oldfd, int newfd, int32_t *retaddr){

  if(oldfd == newfd){
    *retaddr = newfd;
    return 0;
  }
  //Checks oldfd and newfd values for index out of bounds
  if(oldfd >= MAX_FILES || oldfd < 0 || newfd >= MAX_FILES || newfd < 0){
    return EBADF;
  }

  //Gets the current process' filetable
  struct filehandle **filetable = curthread->t_proc->p_filetable;
  KASSERT(filetable != NULL);

  if(filetable[oldfd] == NULL){
    return EBADF;
  }

  //Removes the pointer to the filehandle from the filetable if there is one there
  //in the first place. Otherwise does nothing.
  struct filehandle *newfh = filetable[newfd];
  if(newfh != NULL){
    lock_acquire(newfh->fh_lock);
    vfs_close(newfh->fh_fileobj);
    filetable_remove(filetable, newfd);
    if(newfh != NULL) lock_release(newfh->fh_lock);
  }

  //Has the filetable at newfd point to the filehandle opened at oldfd
  filetable[newfd] = filetable[oldfd];

  //Appropriately increments the refcount of the old filehandle
  lock_acquire(filetable[oldfd]->fh_lock);
  filetable[oldfd]->fh_refcount++;
  lock_release(filetable[oldfd]->fh_lock);

  *retaddr = newfd;

  return 0;


}
