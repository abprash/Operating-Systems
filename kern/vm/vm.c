/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *  The President and Fellows of Harvard College.
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
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <limits.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <stat.h>
#include <uio.h>


static int prevspot;
static unsigned int kern_pcount;
struct ppage *coremap;
static unsigned long cmap_pcount;
static paddr_t usedbytes;
struct spinlock cm_lock = SPINLOCK_INITIALIZER;

struct vnode *swapdisk;
bool haveswap = false;
struct slot *swaptable;
int swap_pcount;
static int prevslot;
struct lock *swaptable_lock;

static int preveviction;

static unsigned long found;

void
cm_bootstrap(){

  //Gets the ramsize and calculates the amount of page entires needed
  paddr_t ramsize = ram_getsize();

  cmap_pcount = ramsize / PAGE_SIZE;
  if(ramsize % PAGE_SIZE != 0) cmap_pcount++;

  //Determining the amount of memory we need to steal for our coremap
  //which will be the size of our coremap
  unsigned long size = sizeof(struct ppage) * cmap_pcount;
  unsigned long tosteal = size / PAGE_SIZE;
  if(size % PAGE_SIZE != 0) tosteal++;

  //Steals amount of our data structure for page * page count
  coremap = (struct ppage *)PADDR_TO_KVADDR(ram_stealmem(tosteal));

  //Must set the bits for kernel to invalid so that we do not give away that memory
  //Sets the usedbytes
  usedbytes = ram_getfirstfree();
  kern_pcount = (usedbytes)/ PAGE_SIZE;
  if(usedbytes % PAGE_SIZE != 0) kern_pcount++;

  for(unsigned int i = 0; i < cmap_pcount; i++){
    if(i < kern_pcount){
      coremap[i].valid = 1;
      coremap[i].kern = 1;
    }else{
      coremap[i].valid = 0;
      coremap[i].kern = 0;
    }
    coremap[i].touched = 0;
    coremap[i].swapping = 0;
    coremap[i].chunk = 0;
    coremap[i].pte = NULL;
  }
  //For quicker searching, start looking further in
  prevspot = kern_pcount;
  found = kern_pcount;
}

void
swap_bootstrap(){
  //Sets up the swapdisk if available
  prevslot = 0;
  preveviction = kern_pcount;
  //Opening the swapdisk
  char *filename = kstrdup("lhd0raw:");
  int result = vfs_open(filename, O_RDWR, 0, &swapdisk);
  if(!result){
    //Acquiring info about swapdisk to know how large to make our swaptable
    struct stat info;
    VOP_STAT(swapdisk, &info);
    unsigned int swapsize = (int)info.st_size;
    KASSERT((swapsize & PAGE_FRAME) == swapsize);
    //idk why this temp is necessary but it seems to be...
    int temp = swapsize / PAGE_SIZE;
    //Initialize a data structure for the swaptable and its lock
    swap_pcount = temp;
    swaptable = kmalloc(sizeof(struct slot) * swap_pcount);
    swaptable_lock = lock_create("swap");
    lock_acquire(swaptable_lock);
    for(int i = 0; i < swap_pcount; i++){
      swaptable[i].occupied = 0;
    }
    haveswap = true;
    lock_release(swaptable_lock);
  }
}

/*Gets physical pages*/
paddr_t
getppages(unsigned long npages, bool kern, bool swapping){

  //Search for a segment of npages in pmem
  unsigned int start = 0;
  unsigned long c = 0;
  for(unsigned long i = prevspot; i < cmap_pcount && c < npages; i++){
    if(coremap[i].valid == 0 && coremap[i].kern==0) c++;
    else c = 0;
    if(c == npages){
      start = i - npages + 1;
      break;
    }
  }

  if(c != npages){
    //if spot not found in our optimized location to start, we want to reset that start location
    prevspot = kern_pcount;
    for(unsigned long i = kern_pcount; i < cmap_pcount && c < npages; i++){
      if(coremap[i].valid == 0 && coremap[i].kern==0) c++;
      else c = 0;
      if(c == npages){
        start = i - npages + 1;
        break;
      }
    }
  }

  // Search done without lock, now check with the lock
  spinlock_acquire(&cm_lock);
  c = 0;
  for(unsigned long i = start; i < cmap_pcount && c < npages; i++){
    if(coremap[i].valid == 0 && coremap[i].kern==0) c++;
    else c = 0;
    if(c == npages){
      start = i - npages + 1;
      break;
    }
  }

  if(c != npages){
    for(unsigned long i = kern_pcount; i < cmap_pcount && c < npages; i++){
      if(coremap[i].valid == 0 && coremap[i].kern==0) c++;
      else c = 0;
      if(c == npages){
        start = i - npages + 1;
        break;
      }
    }
  }

  //Found a segment of free pages large enough
  if(c == npages){
    prevspot = start + npages;
    //Increment through the coremap at the segment we found
    //and initialize the ppages
    for(unsigned int i = start; i < npages + start; i++){
      if(i == start) coremap[i].chunk = npages;
      else coremap[i].chunk = 0;
      if(kern) coremap[i].kern = 1;
      else coremap[i].kern = 0;
      if(swapping) coremap[i].swapping = 1;
      else coremap[i].swapping = 0;
      coremap[i].valid = 1;
      coremap[i].touched = 1;
    }
    usedbytes += npages * PAGE_SIZE;
    paddr_t paddr = start * PAGE_SIZE;
    bzero((void *)PADDR_TO_KVADDR(paddr & PAGE_FRAME), npages * PAGE_SIZE);
    spinlock_release(&cm_lock);
    return paddr;
  }
  // //Otherwise, we need to swap out npages
  #if OPT_DUMBVM
  #else
  if(haveswap){
    KASSERT(npages == 1);
    return swapout(kern, swapping);
  }
  #endif
  spinlock_release(&cm_lock);
  return 0;
  //swapout releases the spinlock
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages) {
  paddr_t paddr = getppages(npages, true, false);
  if(paddr) return PADDR_TO_KVADDR(paddr);
  return 0;
}

void
free_kpages(vaddr_t addr) {
  //conversion
  spinlock_acquire(&cm_lock);
  paddr_t paddr = KVADDR_TO_PADDR(addr);

  unsigned long ppn = paddr / PAGE_SIZE;

  if(coremap[ppn].valid == 0){
    kprintf("coremap entry %lu pte info:\nppn: 0x%08x  (%d)  vpn: 0x%08x  (%d)", ppn, coremap[ppn].pte->ppn, coremap[ppn].pte->ppn, coremap[ppn].pte->vpn, coremap[ppn].pte->vpn);
    panic("freeing non valid coremap entry");
  }
  KASSERT(coremap[ppn].chunk != 0);

  unsigned npages = coremap[ppn].chunk;
  coremap[ppn].chunk = 0;
  unsigned int limit = npages + ppn;
  for(unsigned int i = ppn; i < limit; i++){
    // if(coremap[i].pte != NULL) lock_acquire(coremap[i].pte->lock);
    KASSERT(coremap[i].chunk == 0 && coremap[i].valid == 1 && coremap[i].swapping == 0);
    coremap[i].valid = 0;
    coremap[i].kern = 0;
    coremap[i].touched = 0;
    coremap[i].swapping = 0;
    coremap[i].pte = NULL;
    // if(coremap[i].pte != NULL) lock_release(coremap[i].pte->lock);
  }

  bzero((void *)PADDR_TO_KVADDR(paddr & PAGE_FRAME), npages * PAGE_SIZE);
  usedbytes -= npages * PAGE_SIZE;
  spinlock_release(&cm_lock);


}

unsigned
int
coremap_used_bytes() {
  return usedbytes;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts) {
  (void)ts;
}


//Shouldn't have to be sychronized i dont think based on where it's being called
//along with the fact that processes are unithreaded
void
cm_touch(paddr_t addr){
  unsigned long ppn = addr / PAGE_SIZE;
  coremap[ppn].touched = 1;
}

paddr_t
swapout(bool kern, bool swapping){
  printCoreMap();
  //Find a page that we can evict while resetting the touched bit
  unsigned int found = 0;
  for(unsigned int i = preveviction; i < cmap_pcount; i++){
    if(!found && !coremap[i].kern && !coremap[i].swapping && (!coremap[i].touched || !coremap[i].valid)) found = i;
    if(coremap[i].touched) coremap[i].touched = 0;
  }
  if(!found){
    preveviction = kern_pcount;
    for(unsigned int i = kern_pcount; i < cmap_pcount; i++){
      if(!found && !coremap[i].kern && !coremap[i].swapping && (!coremap[i].touched || !coremap[i].valid)) found = i;
      if(coremap[i].touched) coremap[i].touched = 0;
    }
    if(!found){
      for(unsigned int i = kern_pcount; i < cmap_pcount; i++){
        if(!found && !coremap[i].kern && !coremap[i].swapping && (!coremap[i].touched || !coremap[i].valid)) found = i;
        if(coremap[i].touched) coremap[i].touched = 0;
      }
    }
  }else preveviction = found + 1;

  //If we don't find a gap, we should just loop again
  if(!found){
    panic("no eviction candidate");
    return swapout(kern, swapping);
  }
  KASSERT(found >= kern_pcount && found < cmap_pcount);
  //Should never happen
  //Sets their valid and touched bits to 1 so that they aren't swapped out while swapping out
  //Actually doesn't completely prevent this but it's possible
  KASSERT(coremap[found].pte->ppn != TEMP_PPN);
  KASSERT(coremap[found].pte->ppn != INVAL_PPN);
  KASSERT((unsigned int)coremap[found].pte->ppn == found);
  KASSERT(coremap[found].kern != 1);
  KASSERT(coremap[found].swapping != 1);


  coremap[found].swapping = 1;
  spinlock_release(&cm_lock);

  KASSERT(coremap[found].pte != NULL);
  struct pte *ptecheck = coremap[found].pte;
  lock_acquire(coremap[found].pte->lock);
  KASSERT(coremap[found].pte->ppn != INVAL_PPN);

  coremap[found].touched = 1;
  coremap[found].valid = 1;

  //Swaps out that page
  //Now that we have the PTE, must first remove all TLB entries that map from that PTE's vpn
  uint32_t hi = coremap[found].pte->vpn << 12;
  int spl = splhigh();
  int index = tlb_probe(hi, 0);
  if(index >= 0) tlb_write(TLBHI_INVALID(index), TLBLO_INVALID(), index);
  //Invalidate the ppn and slot number so it doesnt continue adding TLB entires
  int s = coremap[found].pte->slot;
  coremap[found].pte->slot = -1;
  // kprintf("Invalidating PPN 0x%08x\n", iter->ppn << 12);
  paddr_t oldpaddr = coremap[found].pte->ppn << 12;
  coremap[found].pte->ppn = INVAL_PPN;
  splx(spl);
  //Now we need to copy to swapdisk, we must first search for an opening if the PTE doesnt already have one
  if(s < 0){
    lock_acquire(swaptable_lock);
    for(s = prevslot; s < swap_pcount; s++){
      if(!swaptable[s].occupied) break;
    }
    if(s == swap_pcount){
      prevslot = 0;
      for(s = 0; s < swap_pcount; s++){
        if(!swaptable[s].occupied) break;
      }
    }else prevslot = s + 1;
    //Would mean swapdisk is full
    KASSERT(s != swap_pcount - 1);
    //Mark the slot as occupied so we can drop the lock
    swaptable[s].occupied = 1;
    lock_release(swaptable_lock);
  }
  //Allocate space for uio and iovec to assure they arn't passed in unitialized
  struct uio uio;
  struct iovec iovec;

  //Initialize the uio with a userpointer
  vaddr_t copyfrom = PADDR_TO_KVADDR(oldpaddr);
  uint32_t offset = s * PAGE_SIZE;
  uio_kinit(&iovec, &uio, (void *)copyfrom, PAGE_SIZE, offset, UIO_WRITE);

  //Writes to the file for us
  int result = VOP_WRITE(swapdisk, &uio);
  if(result){
    kprintf("Result on Write in Swapout: %d\n", result);
    panic("vop_write");
  }

  //Now that we have written our page to disk, we can notify the PTE that it is stored on disk and release lock
  // kprintf("\n-----%d\n", s);
  coremap[found].pte->slot = s;

  KASSERT(coremap[found].valid == 1);
  if(kern) coremap[found].kern = 1;
  else coremap[found].kern = 0;
  coremap[found].touched = 1;
  coremap[found].chunk = 1;
  if(swapping) coremap[found].swapping = 1;
  else coremap[found].swapping = 0;

  paddr_t paddr = found * PAGE_SIZE;
  bzero((void *)PADDR_TO_KVADDR(paddr & PAGE_FRAME), PAGE_SIZE);
  KASSERT(coremap[found].pte == ptecheck);
  lock_release(coremap[found].pte->lock);
  coremap[found].pte = NULL;
  // kprintf("After Swapping: \n");
  // printPageTable();
  return paddr;
}

void
printPageTable(){
  struct pte *iter = curproc->p_addrspace->pt_head;
  while(iter != NULL){
    if(iter->next != NULL) kprintf("0x%08x, ", iter->ppn << 12);
    else kprintf("0x%08x\n", iter->ppn << 12);
    iter = iter->next;
  }
}

void
printCoreMap(){
  for(unsigned int i = 0; i < cmap_pcount; i++){
    KASSERT(coremap[i].kern == 1 || coremap[i].kern == 0);
    KASSERT(coremap[i].valid == 1 || coremap[i].valid == 0);
    KASSERT(coremap[i].swapping == 1 || coremap[i].swapping == 0);
    KASSERT(coremap[i].touched == 1 || coremap[i].touched == 0);
    // if(!coremap[i].valid){
    //   kprintf("f");
    // }else if(!coremap[i].kern){
    //   kprintf("u");
    // }else{
    //   // kprintf("k");
    // }
  }
  // kprintf("\n");
}

void
swapin(struct pte *pte){
  // kprintf("\n%d\n", pte->slot);
  //allocates a physical page
  paddr_t paddr = getppages(1, false, true);
  //Makes sure the addr is page aligned
  KASSERT(paddr != 0);
  KASSERT((paddr & PAGE_FRAME) == paddr);

  KASSERT(coremap[paddr / PAGE_SIZE].valid);
  KASSERT(!coremap[paddr / PAGE_SIZE].kern);
  KASSERT(coremap[paddr / PAGE_SIZE].swapping);
  KASSERT(coremap[paddr / PAGE_SIZE].pte == NULL);


  //Allocate space for uio and iovec to assure they arn't passed in unitialized
  struct uio uio;
  struct iovec iovec;

  //Initialize the uio with a userpointer
  vaddr_t readto = PADDR_TO_KVADDR(paddr);
  KASSERT(pte->slot >= 0);
  uint32_t offset = pte->slot * PAGE_SIZE;
  uio_kinit(&iovec, &uio, (void *)readto, PAGE_SIZE, offset, UIO_READ);

  int result = VOP_READ(swapdisk, &uio);
  KASSERT(!result);

  KASSERT((paddr >> 12) != INVAL_PPN);
  pte->ppn = paddr >> 12;
  coremap[paddr / PAGE_SIZE].pte = pte;
  coremap[paddr / PAGE_SIZE].swapping = 0;
}


int
vm_fault(int faulttype, vaddr_t vaddr) {
  //Gets the address space
  struct addrspace *as = curproc->p_addrspace;

  //Find the region the vaddr lies in
  struct region *reg_iter = as->reg_head;
  while(reg_iter != NULL){
    if(vaddr >= reg_iter->vaddr && vaddr < reg_iter->vaddr + reg_iter->size) break;
    reg_iter = reg_iter->next;
  }

  //Not found
  if(reg_iter == NULL){
    return EINVAL;
  }

  //Else, region found
  switch(faulttype){
    case VM_FAULT_READ:
      if(!reg_iter->readable)return EFAULT;
      break;
    case VM_FAULT_WRITE:
      if(!reg_iter->writeable)return EFAULT;
      break;
    case VM_FAULT_READONLY:
      if(!reg_iter->writeable)return EFAULT;
      break;

    default:
      panic("weird");
      return EINVAL;
  }

  //Checks the validity of the vaddr
  if(vaddr > MIPS_KSEG0){
    return EFAULT;
  }

  //Iterate through the page table and check for a page with that vpn
  struct pte *iter = as->pt_head;
  while(iter != NULL){
    if(iter->vpn == (vaddr >> 12)) break;
    iter = iter->next;
  }


  //If not found, we must load the TLB
  if(iter == NULL){

    //allocates physical pages and creates the pte
    struct pte *pte = kmalloc(sizeof(struct pte));
    //Creates and initializes the pte
    if(pte == NULL){
      return ENOMEM;
    }
    pte->lock = lock_create("pte");
    KASSERT(pte->lock != NULL);
    lock_acquire(pte->lock);
    pte->vpn = vaddr >> 12;
    pte->slot = -1;
    //Found region, check permissions
    pte->permissions = reg_iter->executable | reg_iter->writeable | reg_iter->readable;
    pte->next = as->pt_head;
    pte->ppn = TEMP_PPN;
    as->pt_head = pte;
    // kprintf("!");


    paddr_t paddr = getppages(1, false, false);
    //Makes sure the addr is page aligned
    if(!haveswap && paddr == 0){
      kfree(pte);
      return ENOMEM;
    }
    KASSERT(paddr != 0);
    KASSERT((paddr & PAGE_FRAME) == paddr);
    KASSERT(coremap[paddr / PAGE_SIZE].pte == NULL);

    KASSERT((paddr >> 12) != INVAL_PPN);
    pte->ppn = paddr >> 12;

    //Assigns the pte to the coremap entry just retrieved
    coremap[paddr / PAGE_SIZE].pte = pte;

    //Masks vaddr
    uint32_t hi = vaddr & PAGE_FRAME;

    //Combines bits together for lo
    uint32_t lo = paddr | TLBLO_DIRTY | TLBLO_VALID;

    //Finally, we must load these two arguments into the TLB

    int spl = splhigh();
    KASSERT(tlb_probe(hi, 0) < 0);
    tlb_random(hi, lo);
    splx(spl);
    lock_release(pte->lock);
    return 0;
  }else{
    //Virtual page found but was not in TLB
    //Must be in swapdisk or in the process of being swapped out, need to swapin
    lock_acquire(iter->lock);
    #if OPT_DUMBVM
    #else
    if(haveswap && iter->ppn == INVAL_PPN){
      if(iter->slot < 0){
        lock_release(iter->lock);
        return 0;
      }
      swapin(iter);
    }
    #endif

    //Should be in memory otherwise
    uint32_t hi = (iter->vpn << 12) & PAGE_FRAME;
    uint32_t lo = (iter->ppn << 12) | TLBLO_DIRTY | TLBLO_VALID;
    int spl = splhigh();
    KASSERT(tlb_probe(hi, 0) < 0);
    tlb_random(hi, lo);
    splx(spl);
    //Alerts the coremap that this page was just used, data is used for eviction
    cm_touch(iter->ppn << 12);
    lock_release(iter->lock);
    return 0;
  }
 return 0;
}
