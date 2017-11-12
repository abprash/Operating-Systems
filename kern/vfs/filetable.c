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


#include <filehandle.h>
#include <filetable.h>
#include <proc.h>
#include <lib.h>
#include <kern/errno.h>
#include <file.h>

struct filehandle **
filetable_create(){
  struct filehandle **filetable;
  filetable = kmalloc(sizeof(struct filehandle *) * 64);
  for(int i = 0; i < 64; i++){
    filetable[i] = NULL;
  }

  return filetable;
}

struct filehandle **
filetable_createcopy(struct filehandle **src){
  KASSERT(src != NULL);
  struct filehandle **filetable;
  filetable = kmalloc(sizeof(struct filehandle *) * 64);
  if(filetable == NULL){
    return NULL;
  }
  for(int i = 0; i < 64; i++){
    filetable[i] = src[i];
    if(filetable[i] != NULL){
      lock_acquire(filetable[i]->fh_lock);
      filetable[i]->fh_refcount++;
      // kprintf("\nFilehandle copied Refcount: %d\n", filetable[i]->fh_refcount);
      lock_release(filetable[i]->fh_lock);
    }
  }

  return filetable;
}

void
filetable_destroy(struct filehandle **filetable){
  for(unsigned int i = 0; i < 64; i++){
    if(filetable[i] != NULL)filetable_remove(filetable, i);
  }
  kfree(filetable);
}

int
filetable_add(struct filehandle **filetable, struct filehandle *filehandle){
  //Searchs for spot in filetable
  for(int i = 0; i < 64; i++){

    //Spot found
    if(filetable[i] == NULL){
      filetable[i] = filehandle;

      //Increment filehandles reference count
      filehandle->fh_refcount++;
      return i;
    }
  }
  return -1;
}

void
filetable_remove(struct filehandle **filetable, int fd){

  //Decrements the amount of references to the filehandle
  filetable[fd]->fh_refcount--;

  filehandle_destroy(filetable[fd]);
  filetable[fd] = NULL;
}

void
filetable_print(struct filehandle **filetable){
  for(int i = 0; i < 64; i++){
    if(filetable[i] != NULL) kprintf("%d", i);
  }
}
