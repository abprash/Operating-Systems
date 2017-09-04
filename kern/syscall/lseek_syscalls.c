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
#include <kern/unistd.h>
#include <kern/seek.h>
#include <kern/stat.h>

/*
 * System call: change the offset of the file handle associated to the given fd
 */

off_t
sys_lseek(int fd, off_t pos, int whence, int64_t *retaddr){



  //Checks if the given fd is a valid index, if not, return error
  if(fd >= MAX_FILES || fd < 0){
		return EBADF;
	}

  //Get's the current process' filetable
  struct filehandle **filetable = curthread->t_proc->p_filetable;
  KASSERT(filetable != NULL);



  //Get's the filehandle at the given index (fd)
  struct filehandle *filehandle = filetable[fd];

  //Checks if the filehandle exists, if it doesn't, return error
  if(filehandle == NULL){
		return EBADF;
	}

  lock_acquire(filehandle->fh_lock);

  if(!VOP_ISSEEKABLE(filehandle->fh_fileobj)){
    lock_release(filehandle->fh_lock);
    return ESPIPE;
  }

  switch(whence){
    case SEEK_SET:
      //If pos is negative that would result in a negative seek value for our file
      //handle in this case
      if(pos < 0){
        lock_release(filehandle->fh_lock);
        return EINVAL;
      }
      filehandle->fh_offset = pos;
      break;
    case SEEK_CUR:
      //Would also result in negative value
      if(filehandle->fh_offset + pos < 0){
        lock_release(filehandle->fh_lock);
        return EINVAL;
      }
      filehandle->fh_offset += pos;
      break;
    case SEEK_END:
      ;
      struct stat info;
      VOP_STAT(filehandle->fh_fileobj, &info);
      if(info.st_size + pos < 0){
        lock_release(filehandle->fh_lock);
        return EINVAL;
      }
      filehandle->fh_offset = info.st_size + pos;
      break;
    default:
      //whence must be invalid, return error code
      lock_release(filehandle->fh_lock);
      return EINVAL;
      break;
  }
  //Copy our retval out to the system dispatcher
  *retaddr = filehandle->fh_offset;

  lock_release(filehandle->fh_lock);
  return 0;



}
