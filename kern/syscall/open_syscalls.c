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
#include <filehandle.h>
#include <filetable.h>
#include <vfs.h>
#include <proc.h>
#include <file.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <copyinout.h>
#include <kern/limits.h>
/*
 * System call: open the file
 */


int
sys_open(const char *filename, int flags, int32_t *retaddr){

	if(filename == NULL){
		return EFAULT;
	}

	int result;

	char name[__PATH_MAX];

	//check the filename
	result = copyinstr((const_userptr_t) filename, name, sizeof(name), NULL);
	if(result) return result;

	//create the vnode
	struct vnode *vnode;

	result = vfs_open(name, flags, 0, &vnode);
	if(result){
		return result;
	}

	//get the filetable from the current process
	struct filehandle **filetable = curproc->p_filetable;

	//create the file handle
	struct filehandle *filehandle = filehandle_create(name);
	KASSERT(filehandle != NULL);

	lock_acquire(filehandle->fh_lock);

	//sets the filehandles access flags
	filehandle->fh_flag = (flags & O_ACCMODE) + 1;

	filehandle->fh_fileobj = vnode;

	int fd;
	fd = filetable_add(filetable, filehandle);

	if(fd == -1){
		lock_release(filehandle->fh_lock);
		return EMFILE;
	}

	*retaddr = fd;

	lock_release(filehandle->fh_lock);
	return 0;
}
