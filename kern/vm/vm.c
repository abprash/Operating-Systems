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
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
// #include <mips/vm.h>
#include <addrspace.h>
#include <vm.h>

//Coremap should be an array of entries, these entires need contain only chunk and whether or not it is valid
//When 10 pages for example are kmalloced, you set the first page's chunk size to 10, the rest to zero and
//find the first gap of 10 pages in the coremap and set their valid bits to 1. Otherwise, you return NULL.


//Essentially a reverse page table
//--Allows the logical page associated with a paddr to be found
//--given that paddr
//--Has bit flags that indicate if pages are kernel pages, pinned(busy)
//--Has one entry per page frame in pmem
//Upon fault, we need to swap a page into memory
//--Need an empty page, if full, evict a page
//--Can pick page logically or randomly
//--Once a change is made, if lpage evicted:
//----Must update victim PTE to show it's in disk
//----Remove victim PTE from the TLB
//----Update the new page's PTE into the TLB
//Needs to have Excep Handler, Kern, and Coremap in it after boot is over
//I think the data structure should be an array of pointers to our entry structure of size pagecount
//Need some synchronization primitive associated to it after thread_bootstrap


//If an lpage has this paddr, it's not in memory
static const paddr_t INVAL_PADDR = 0xdeadbeef;

static struct ppage *coremap;
static unsigned long cmap_pcount;
// static paddr_t paddr;
static paddr_t nextpaddr;

void
cm_bootstrap(){

  //Gets the ramsize and calculates the amount of page entires needed
  paddr_t ramsize = ram_getsize();
  kprintf("ram size - %d\n",ramsize);
  cmap_pcount = ramsize / PAGE_SIZE;
  kprintf("no. of entries %lu \n",cmap_pcount);
  //Determining the amount of memory we need to steal for our coremap
  //which will be the size of our coremap
  unsigned long size = sizeof(struct ppage) * cmap_pcount;
  unsigned long tosteal = size / PAGE_SIZE;

  //Steals amount of our data structure for page * page count
  paddr_t coremap_paddr = ram_stealmem(tosteal);

  coremap = (struct ppage *)PADDR_TO_KVADDR(coremap_paddr);

  for(unsigned int i = 0; i < cmap_pcount; i++){
    coremap[i].valid = 0;
    coremap[i].chunk = 0;
  }

  //Sets the nextpaddr to the first available address for future page allocations
  nextpaddr = ram_getfirstfree();



}


/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages) {
  //Search for a segment of npages in pmem
  unsigned int start = 0;
  unsigned long c = 0;
  for(unsigned long i = 0; i < cmap_pcount && c < npages; i++){
    if(coremap[i].valid == 0) c++;
    else c = 0;
    start = i - npages + 1;
  }

  //Found a segment of free pages large enough
  if(c == npages){
    //Increment through the coremap at the segment we found
    //and initialize the ppages
    for(unsigned int i = start; i - start < npages; i++){
      if(i == start) coremap[i].chunk = npages;
      else coremap[i].chunk = 0;
      coremap[i].valid = 1;
    }
  }
  return 0;
}

void
free_kpages(vaddr_t addr) {
  //Calculate the ppage we want to address
  //how do we get the physical address?
  paddr_t paddr = addr - 80000000; /*conversion*/
  unsigned long ppn = paddr / PAGE_SIZE;

  if(coremap[ppn].valid == 0 || coremap[ppn].chunk == 0) panic("coremap entires being freed were not as expected upon freeing");
  KASSERT(ppn + coremap[ppn].chunk < cmap_pcount);

  for(unsigned int i = ppn; i < coremap[ppn].chunk + ppn; i++){
    if(i == ppn) coremap[i].chunk = 0;
    KASSERT(coremap[i].chunk == 0 && coremap[i].valid == 1);
    coremap[i].valid = 0;
  }


}

unsigned
int
coremap_used_bytes() {
	return 0;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts) {
	(void)ts;
}

int
vm_fault(int faulttype, vaddr_t faultaddress) {

 //TLB miss
 // if(faultaddress == VM_FAULT_READ || faultaddress == VM_FAULT_WRITE){
 //   tlb_random()
 // }
 (void)faulttype;
 (void)faultaddress;
 return 0;
}
