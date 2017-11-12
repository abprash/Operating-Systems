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
#include <proc.h>
#include <kern/errno.h>
#include <kern/wait.h>
#include <addrspace.h>
#include <process.h>
#include <machine/tlb.h>
#include "opt-dumbvm.h"

/*
 * Extends the heap end of the current process' addrspace
 */
#if OPT_DUMBVM
#else
int
sys_sbrk(intptr_t amount, void** retaddr){
  //Checks that the amount is a multiple of PAGE_SIZE
  if(amount % PAGE_SIZE != 0){
    return EINVAL;
  }

  //Grab the current proc's addrspace
  struct addrspace *as = curproc->p_addrspace;
  KASSERT(as != NULL);

  //Grab the heap lock
  lock_acquire(as->hplock);

  //For simplicities sake
  struct region *heap = as->heap;

  //Store the current break for returning after
  int ret = heap->vaddr + heap->size;
  KASSERT((heap->vaddr + heap->size) % PAGE_SIZE == 0);

  //Checks that we wont bring the heap to a negative size
  if(amount < 0 && heap->size < (size_t)(-1 * amount)){
    lock_release(as->hplock);
    return EINVAL;
  }

  //Checks that our heap doesn't collide with our stack
  if(heap->vaddr + heap->size + amount >= STACKBOTTOM){
    lock_release(as->hplock);
    return ENOMEM;
  }

  //Increases size by amount
  heap->size += amount;

  //If amount is negative we need to make sure that we are freeing these pages
  if(amount < 0){
    struct addrspace *as = curproc->p_addrspace;
    vaddr_t top = heap->vaddr + heap->size - amount;
    vaddr_t bottom = heap->vaddr + heap->size;
    //Iterate through our page table handleing entires within the range of where our heap is no longer
    bool head_safe = false;
  	struct pte *pte_cur = as->pt_head;
  	struct pte *pte_next = NULL;
  	while(pte_cur != NULL){
      //If we have iterated past our head, we can assure that the head pointer is safe
      if(pte_cur != as->pt_head) head_safe = true;
      vaddr_t vaddr = pte_cur->vpn & PAGE_FRAME;
      //Within range
      if(vaddr >= bottom && vaddr < top){
        //If our head pointer has not been secured yet we increment it
        if(!head_safe) as->pt_head = pte_cur->next;
        //Shoots down the tlb entry in question if it is currently in the tlb
        int i = tlb_probe(pte_cur->vpn & PAGE_FRAME, 0);
        if(i > 0) tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
        //Frees page
    		free_kpages(PADDR_TO_KVADDR(pte_cur->ppn & PAGE_FRAME));
    		pte_next = pte_cur->next;
    		kfree(pte_cur);
        pte_cur = pte_next;
      }else{
        pte_next = pte_cur->next;
    		pte_cur = pte_next;
      }
  	}
  }

  *retaddr = (void *)ret;
  lock_release(as->hplock);
  return 0;
}

#endif
