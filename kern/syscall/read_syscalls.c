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


/*
 * System call: read an open file
 */

//const void buf
ssize_t
sys_read(int fd, void *buf, size_t nbytes, int32_t *retaddr){

  //Get's the current process' filetable
  struct filehandle **filetable = curthread->t_proc->p_filetable;
  KASSERT(filetable != NULL);

  //Checks if the given fd is a valid index, if not, return error
  if(fd >= MAX_FILES || fd < 0){
		return EBADF;
	}

  //Get's the filehandle at the given index (fd)
  struct filehandle *filehandle = filetable[fd];

  //Checks if the filehandle exists, if it doesn't, return error
  if(filehandle == NULL){
		return EBADF;
	}

  // Checks filehandle flags to make sure we can read
  if(filehandle->fh_flag == O_WRONLY + 1){
    return EBADF;
  }

  lock_acquire(filehandle->fh_lock);

  //Allocate space for uio and iovec to assure they arn't passed in unitialized
  struct uio *uio = kmalloc(sizeof(uio));
  struct iovec *iovec = kmalloc(sizeof(iovec));

  //Initialize the uio with a userpointer
  uio_uinit(iovec, uio, buf, nbytes, filehandle->fh_offset, UIO_READ, curthread->t_proc->p_addrspace);

  //Writes to the file for us
  int result = VOP_READ(filehandle->fh_fileobj, uio);
  if(result){
    //signal error return by vop_read
    kfree(uio);
    kfree(iovec);
    lock_release(filehandle->fh_lock);
    return result;
  }


  //Calculates how much of the read we wanted done was actually done
  int32_t retval = nbytes - uio->uio_resid;

  //update the filehandles seek position based on how much was read
  filehandle->fh_offset += retval;

  //Copy our retval out to the system dispatcher
  *retaddr = retval;

  lock_release(filehandle->fh_lock);

  //returns 0 to signal no error occured
  return 0;


}
