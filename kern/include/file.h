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

#ifndef _FILE_H_
#define _FILE_H_

#include <types.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <synch.h>


/*
 * Prototypes for IN-KERNEL entry points for system call implementations.
 */


 /*Open a file object*/
int sys_open(const char *filename, int flags, int32_t *);

/*Close an opened file*/
int sys_close(int fd);

/*Read an opened file*/
ssize_t sys_read(int, void *, size_t, int32_t *);

/*Write to an opened fileI*/
ssize_t sys_write(int, const_userptr_t, size_t, int32_t *);

/*Alters the current seek position of a filehandle based on pos and whence*/
off_t sys_lseek(int, off_t, int, int64_t *);

/*Clones the filehandle oldfd onto the filehandle newfd. Close newfd if already open*/
int sys_dup2(int, int, int32_t *);

/*The current directory of the current process is set to the directory named by pathname*/
int sys_chdir(const char *);

/*Name of current directory is stored in buf*/
int sys__getcwd(char *, size_t);

/*Add more system calls as needed*/
#endif /* _FILE_H_ */
