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
#include <kern/errno.h>
#include <lib.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <mips/tlb.h>
#include <uio.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	as->hplock = lock_create("heaplock");
	if(as->hplock == NULL){
		kfree(as);
		return NULL;
	}

	//Set the pagetable and region lists to NULL for later initialization
	as->pt_head = NULL;
	as->reg_head = NULL;
	as->heap = NULL;
	return as;
}

//TODO handle copying pages on swapdisk
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	//Handles copying the regions which are just carboncopied
	//Copies head
	new->reg_head = reg_copy(old->reg_head);
	if(old->reg_head != NULL && new->reg_head == NULL){
		return ENOMEM;
	}

	//Handles rest if there is any
	if(new->reg_head != NULL){
		struct region *olditer = old->reg_head;
		struct region *newiter = new->reg_head;
		while(olditer != NULL){
			//Checks whether or not the current region is the oldas's heap
			//if it is, we make sure we set up the newas's heap pointer
			if(olditer == old->heap) new->heap = newiter;

			//Copies and steps iterators
			newiter->next = reg_copy(olditer->next);
			if(olditer->next != NULL && newiter->next == NULL){
				return ENOMEM;
			}
			olditer = olditer->next;
			newiter = newiter->next;
		}
		//Assures that there was a heap region copied
		KASSERT(new->heap != NULL);
	}


	//Copies the pagetable
	//pte_copy will handle allocating new ppages and copying data
	//Handles copying the first PTE
	new->pt_head = pte_copy(old->pt_head);
	if(old->pt_head != NULL && new->pt_head == NULL){
		return ENOMEM;
	}
	//Handles the rest if there is any
	if(new->pt_head != NULL){
		struct pte *olditer = old->pt_head;
		struct pte *newiter = new->pt_head;
		while(olditer != NULL){
			newiter->next = pte_copy(olditer->next);
			if(olditer->next != NULL && newiter->next == NULL){
				return ENOMEM;
			}
			olditer = olditer->next;
			newiter = newiter->next;
		}
	}



	*ret = new;
	return 0;
}


//TODO handle destroying pages on swapdisk
void
as_destroy(struct addrspace *as)
{
	// (void)as;
	struct pte *pte_cur = as->pt_head;
	struct pte *pte_next;
	while(pte_cur != NULL){
		//Frees page on memory
		lock_acquire(pte_cur->lock);
		//Frees page on swapdisk
		if(pte_cur->slot > 0){
			swaptable[pte_cur->slot].occupied = 0;
		}
		pte_next = pte_cur->next;
		if(pte_cur->ppn != INVAL_PPN){
			free_kpages(PADDR_TO_KVADDR(pte_cur->ppn << 12));
		}
		lock_release(pte_cur->lock);
		lock_destroy(pte_cur->lock);
		// kprintf("\n?\n");
		kfree(pte_cur);
		pte_cur = pte_next;
	}
	as->pt_head = NULL;

	//Destroy region list
	struct region *reg_cur = as->reg_head;
	struct region *reg_next;
	while(reg_cur != NULL){
		reg_next = reg_cur->next;
		kfree(reg_cur);
		reg_cur = reg_next;
	}
	as->reg_head = NULL;
	as->heap = NULL;

	//Destroy heap lock
	lock_destroy(as->hplock);

	//Free addrspace pointer
	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}


/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	//TODO error check here
	// if(vaddr + memsize > 4MB) return some error;
	// vaddr = vaddr & PAGE_FRAME;
	struct region *newreg = kmalloc(sizeof(struct region));
	if(newreg == NULL) return ENOMEM;
	newreg->vaddr = vaddr;
	newreg->size = memsize;
	newreg->readable = readable;
	newreg->writeable = writeable;
	newreg->executable = executable;
	newreg->prev_read = -1;
	newreg->prev_write = -1;
	newreg->prev_exec = -1;
	newreg->next = as->reg_head;
	as->reg_head = newreg;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	//Want to define the heap before the stack because the methodology
	//in which we use to decide where to base it
	as_define_heap(as);

	//Create a region to make these addresses valid
	//should be 1024 pages
	int ret = as_define_region(as, STACKBOTTOM, STACKSIZE, 4, 2, 1);
	if(ret) return ret;
	*stackptr = USERSTACK;
	as->stackptr = USERSTACK;


	return 0;
}


void
as_define_heap(struct addrspace *as){
	//Must iterate through our regions in order to find the highest defined address
	vaddr_t candidate = 0;
	struct region *iter = as->reg_head;
	while(iter != NULL){
		if(iter->vaddr + iter->size > candidate) candidate = iter->vaddr + iter->size;
		iter = iter->next;
	}
	//Rounding our heap base to be page aligned
	if(candidate % PAGE_SIZE != 0){
		candidate = (candidate / PAGE_SIZE + 1) * PAGE_SIZE;
	}
	KASSERT(candidate != 0);

	//Define our region
	as_define_region(as, candidate, 0, 4, 2, 1);

	//Grab a pointer to our region
	as->heap = as->reg_head;

	//Double checking we have the correct region
	KASSERT(as->heap->size == 0);
}

int
as_prepare_load(struct addrspace *as)
{
	struct region *iter = as->reg_head;
	while(iter != NULL){
		iter->prev_write = iter->writeable;
		iter->writeable = 2;
		iter = iter->next;
	}
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	struct region *iter = as->reg_head;
	while(iter != NULL){
		KASSERT(iter->prev_write != -1);
		iter->writeable = iter->prev_write;
		iter->prev_write = -1;
		iter = iter->next;
	}
	return 0;
}



struct region *
reg_copy(struct region *oldreg){
	if(oldreg == NULL) return NULL;
	struct region *ret = kmalloc(sizeof(struct region));
	if(ret == NULL) return NULL;

	//Copes region values
	ret->vaddr = oldreg->vaddr;
	ret->size = oldreg->size;
	ret->readable = oldreg->readable;
	ret->writeable = oldreg->writeable;
	ret->executable = oldreg->executable;
	ret->prev_read = oldreg->prev_read;
	ret->prev_write = oldreg->prev_write;
	ret->prev_exec = oldreg->prev_exec;
	ret->next = NULL;

	return ret;
}

//TODO
//We are going to want some form of synchronizing our physical pages so that write prevents others from accessing
//Do we copy it onto whatever what we were copying is on?
//(i.e. If the oldas's pte is on disk do we make a copy on disk instead of mem?)
struct pte *
pte_copy(struct pte *oldpte){
	if(oldpte == NULL) return NULL;
	if(haveswap) lock_acquire(oldpte->lock);

	struct pte *ret = kmalloc(sizeof(struct pte));
	if(ret == NULL){
		if(haveswap) lock_release(oldpte->lock);
		return NULL;
	}

	ret->lock = lock_create("pte");
	if(ret->lock == NULL){
		kfree(ret);
		if(haveswap) lock_release(oldpte->lock);
		return NULL;
	}

	spinlock_acquire(&cm_lock);
	if(oldpte->ppn != INVAL_PPN) coremap[oldpte->ppn].swapping = 1;
	spinlock_release(&cm_lock);



	//Allocates physical pages and creates the pte
	paddr_t paddr = getppages(1, false, true);
	if(haveswap && paddr == 0)panic("nomem?!");
	else if(paddr == 0) return NULL;

	//Makes sure the addr is page aligned
	KASSERT((paddr & PAGE_FRAME) == paddr);

	//Copies values from oldpte
	coremap[paddr / PAGE_SIZE].pte = ret;

	ret->vpn = oldpte->vpn;
	ret->ppn = paddr >> 12;
	ret->slot = -1;
	ret->permissions = oldpte->permissions;
	ret->next = NULL;


	if(haveswap && oldpte->ppn == INVAL_PPN){
		KASSERT(coremap[ret->ppn].swapping);
		// KASSERT(oldpte->slot >= 0);
		while(oldpte->slot < 0){}
		//Have to allocae a new slot on the swaptable

		//Now that we have that slot we need to copy the data from the old pte to this new slot
		//Allocate space for uio and iovec to assure they arn't passed in unitialized
    	struct uio uio;
    	struct iovec iovec;

		//Initialize the uio with a userpointer
		vaddr_t readto = PADDR_TO_KVADDR(ret->ppn << 12);
		uio_kinit(&iovec, &uio, (void *)readto, PAGE_SIZE, oldpte->slot * PAGE_SIZE, UIO_READ);

		//Writes to the file for us
		int result = VOP_READ(swapdisk, &uio);

		KASSERT(!result);
		coremap[ret->ppn].swapping = 0;

	}else{
		// Otherwise, we will be copying from memory
		// Copies data from old physical page to new one
		void * dest = (void *)PADDR_TO_KVADDR(ret->ppn << 12);
		void * src = (void *)PADDR_TO_KVADDR(oldpte->ppn << 12);

		memcpy(dest, src, PAGE_SIZE);
	}
	ret->slot = -1;

	coremap[ret->ppn].swapping = 0;
	coremap[oldpte->ppn].swapping = 0;
	if(haveswap) lock_release(oldpte->lock);

	return ret;
}
