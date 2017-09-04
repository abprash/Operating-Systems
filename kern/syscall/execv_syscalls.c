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

#include <process.h>
#include <file.h>
#include <kern/errno.h>
#include <proc.h>
#include <addrspace.h>
#include <filetable.h>
#include <syscall.h>
#include <limits.h>
#include <test.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <kern/limits.h>
#include <copyinout.h>
/*
 * Process syscall execv replaces the currently executing program with a newly
 * loaded program image.
 */

static char final_args[ARG_MAX];
static int arg_lengths[4000];

int
sys_execv(const char *prognam, char **args){
//check if the prognam is valid
	if(prognam == NULL || args == NULL){
		return EFAULT;
	}

	bzero(final_args, ARG_MAX);
	char safeprognam[__PATH_MAX];
	size_t actsize;
	int result = copyinstr((const_userptr_t)prognam, safeprognam, (size_t) __PATH_MAX, &actsize);
	if(result)return result;

	int total_size = 0;
	int copy_position = 0;
	int size_with_pad = 0;
	int arg_count = 0;
	actsize = 0;


	result = copyin((userptr_t)args, &final_args, sizeof(userptr_t));
 	if(result) return result;


	while(args[arg_count] != NULL){
		result = copyinstr((const_userptr_t) args[arg_count], &final_args[copy_position], (size_t) ARG_MAX, &actsize);
		if(result)	return result;
		int padding = (4 - (actsize-1)%4 - 1);
		arg_lengths[arg_count] = actsize + padding;
		arg_count++;
		copy_position += actsize;

		//try adding the padding here

		copy_position+=padding;
		size_with_pad += actsize+padding;
	}

	total_size = size_with_pad + (4*arg_count) + 4;
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	// open the prognam's file
	result = vfs_open(safeprognam, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	//now create the new addrspace
	as = as_create();

	if(as == NULL){
		vfs_close(v);
		return ENOMEM;
	}
	//set the addrspace to the current process'
	proc_setas(as);
	//activate the process
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return result;
	}
	vaddr_t bottom_of_stack = stackptr - total_size;
	vaddr_t lower = stackptr - total_size;
	vaddr_t upper = stackptr - size_with_pad;
	vaddr_t upper2 = stackptr - size_with_pad;
	for(int i=0; i<arg_count; i++){

		copyout( &upper,(userptr_t) lower, sizeof(void **));
		lower+=4;
		upper+=arg_lengths[i];
	}

	copyout( &final_args[0], (userptr_t) upper2, sizeof(char) * size_with_pad);
	for(int i=0; i<size_with_pad; i++){
	 }

	enter_new_process(arg_count,
			(userptr_t) bottom_of_stack,
			NULL ,
			bottom_of_stack, entrypoint);
		panic("enter_new_process returned\n");

	return EINVAL;
}
