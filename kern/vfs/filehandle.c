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
 #include <spl.h>
 #include <current.h>
 #include <filehandle.h>
 #include <vfs.h>

 struct filehandle *
 filehandle_create(const char *name){
   struct filehandle *filehandle;

   filehandle = kmalloc(sizeof(filehandle));
 	 if(filehandle == NULL){
 	 	return NULL;
	 }

 	//set the name
 	filehandle->fh_name = kstrdup(name);
 	if(filehandle->fh_name == NULL){
 		kfree(filehandle);
 		return NULL;
 	}

  //create and associate lock
  filehandle->fh_lock = lock_create(name);
  if(filehandle->fh_lock == NULL){
    filehandle_destroy(filehandle);
    return NULL;
  }

  filehandle->fh_refcount = 1;
  filehandle->fh_offset = 0;
  filehandle->fh_flag = -3;
  filehandle->fh_fileobj = NULL;

  // off_t
  return filehandle;
 }

void
filehandle_destroy(struct filehandle *filehandle){

  if(filehandle != NULL){

    //No other processes point to this filehandle
    if(filehandle->fh_refcount == 0){
      kfree(filehandle->fh_name);
      vfs_close(filehandle->fh_fileobj);
      if(filehandle->fh_fileobj != NULL) kfree(filehandle->fh_fileobj);
      lock_destroy(filehandle->fh_lock);
      kfree(filehandle);
    }
  }
}
