#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/phase_shifts.h>
#include <linux/cred.h>

#include <asm/page_types.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>

#include "phase_shifts_data.h"
#include "hash_table.h"

MODULE_LICENSE("Dual BSD/GPL");

static uid_t WATCHED_USER = (uid_t) 2013;

static unsigned long HASH_SIZE = 16;
static unsigned long LOCALITY_SIZE = 64;

/** Function that extracts the page number out of an address.
 * @address - address from which we would like to extract page number.
 */
static inline unsigned long get_page_number(unsigned long address)
{
	return address >> PAGE_SHIFT;
}
static inline unsigned long pn_to_addr(unsigned long pn)
{
	return pn << PAGE_SHIFT;
}
/** Function follows an address to get pte. It also gives you the lock for it, and locks it. 
 */
static int follow_pte(struct mm_struct *mm, unsigned long address,
		pte_t **ptepp, spinlock_t **ptlp)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep;

	pgd = pgd_offset(mm, address);
	if (pgd_none(*pgd) || unlikely(pgd_bad(*pgd)))
		goto out;

	pud = pud_offset(pgd, address);
	if (pud_none(*pud) || unlikely(pud_bad(*pud)))
		goto out;

	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd) || unlikely(pmd_bad(*pmd)))
		goto out;

	/* We cannot handle huge page PFN maps. Luckily they don't exist. */

	ptep = pte_offset_map_lock(mm, pmd, address, ptlp);
	if (!ptep)
		goto out;
	if (!pte_present(*ptep))
		goto unlock;
	*ptepp = ptep;
	return 0;
unlock:
	pte_unmap_unlock(ptep, *ptlp);
out:
	return -EINVAL;
}
/** swap_out_page() removes a page from our locality list. It does it by clearing the User/Supervisor flag, thus a next reference would cause a page fault.
 * @page - page that is being "swapp
 * @mm - mm of the original process.
 */
static void swap_out_page(struct locality_page* page, struct mm_struct* mm)
{
	unsigned long address = pn_to_addr(page->nm_page);
	pte_t* ptep;
	spinlock_t* ptl;
	pte_t entry;
	if(follow_pte(mm, address, &ptep, &ptl)) // Checks if found such pte.
		return;
	
	entry = *ptep;
	entry = pte_clear_flags(entry, _PAGE_USER);
	set_pte(ptep, entry);
	update_mmu_cache(vma, address, ptep);
	pte_unmap_unlock(ptep, ptl);
}

static struct locality_page* alloc_locality_page(unsigned long address)
{
	struct locality_page* page = (struct locality_page *) kmalloc(sizeof(struct locality_page), GFP_KERNEL);
	page->nm_page = get_page_number(address);
	INIT_HLIST_NODE(&page->hash_list);
	INIT_LIST_HEAD(&page->locality_list);
	
	return page;
}
static void free_locality_page(struct locality_page* page)
{
	kfree(page);
}
static void phase_shifts_data_init (struct phase_shift_detection_scheme* scheme)
{
	scheme->locality_hash_tbl = alloc_hash(HASH_SIZE);
	INIT_LIST_HEAD(&scheme->locality_list);
	scheme->locality_list_size = 0;
	scheme->locality_max_size = LOCALITY_SIZE;
	scheme->hash_table_size = HASH_SIZE;
	scheme->current_tick_faults = 0;
	scheme->previous_tick_faults = 0;
	spin_lock_init(&scheme->lock);
	
}
static void free_phase_shifts_data (struct phase_shift_detection_scheme* scheme)
{
	// This function also removes the locality pages themselves, thus removing the locality list.
	free_hash_list(scheme->locality_hash_tbl, scheme->hash_table_size);
	kfree(scheme);
	printk ( KERN_ALERT "phase_shifts_detector: cleaned info. \n" );
}

static void exit_callback (struct task_struct* p)
{
	char buff[TASK_COMM_LEN];
	if(p->phase_shifts_private_data)
	{
		free_phase_shifts_data(p->phase_shifts_private_data);
		p->phase_shifts_private_data = NULL;
		printk( KERN_ALERT "%s[%d]: execution has ended. \n", get_task_comm(buff, p), task_pid_nr(p) );
	}
}
static void exec_callback(struct task_struct* p)
{
	struct phase_shift_detection_scheme* data;
	char buff[TASK_COMM_LEN];
	if(task_uid(p) == WATCHED_USER)
	{
		data = (struct phase_shift_detection_scheme*) kmalloc(sizeof(struct phase_shift_detection_scheme), GFP_KERNEL);
		phase_shifts_data_init(data);
		if(p->phase_shifts_private_data)
		{
			free_phase_shifts_data(p->phase_shifts_private_data);
		}
		p->phase_shifts_private_data = (void*) data;
		printk( KERN_ALERT "%s[%d]: execution has begun. \n", get_task_comm(buff, p), task_pid_nr(p) );
	}
}

/**
 * Page fault handler callback. From where its called, its safe to assume that the user has access rights to it.
 */
static void fault_callback (struct mm_struct *mm,
		     struct vm_area_struct *vma, unsigned long address,
		     pte_t *pte, pmd_t *pmd, unsigned int flags)
{
	struct phase_shift_detection_scheme* scheme = current->phase_shifts_private_data;
	struct locality_page* page;
	pte_t entry;
	spinlock_t* ptl;
	
	if(scheme)
	{
		ptl = pte_lockptr(mm, pmd);
		entry = *pte;
		
		
		if( !pte_present(entry) && (vma->vm_flags & VM_EXEC)) // Page not in memory, and the memory region is executable - 1st case. 
			// Fault was read, page is present in memory, and area is executable.
		{
			printk(KERN_ALERT "PAGE FAULT MOTHERFUCKER 1st. %p \n", (void*)get_page_number(address));
			goto add_this_page;
		}
		else if(pte_present(entry) && (vma->vm_flags & VM_EXEC) && (!(flags & FAULT_FLAG_WRITE)))
		{
			printk(KERN_ALERT "PAGE FAULT MOTHERFUCKER 2nd. %p \n", (void*)get_page_number(address));
			goto add_this_page;
		} 
		
		return;
		
	add_this_page:
	
		spin_lock(&scheme->lock);
			
			page = hash_find(scheme->locality_hash_tbl, scheme->hash_table_size, get_page_number(address));
			if(page) // If page already in lists.
			{
				// Moves the element to the head (new references would cause it to move back to head of locality list.
				list_del_init(&page->locality_list);
				list_add(&page->locality_list, &scheme->locality_list);
			}
			else // Otherwise, page is not already in lists.
			{
				if(scheme->locality_list_size == scheme->locality_max_size) // Checks if list is full... :(
				{
					/* Getting the page to be "swapped out". */
					page = list_entry(scheme->locality_list.prev, struct locality_page, locality_list); 
					/* Deletes swapped out page to */
					list_del(&page->locality_list);
					hlist_del(&page->hash_list);
					
					/* In this case, makes sure that a next reference causes a page fault with swap_out_page, and frees the page's space. */
					//swap_out_page(page, mm);
					free_locality_page(page);
				}
				
				// Adds our new page to the list and hash table.
				page = (struct locality_page*) alloc_locality_page(address);
				list_add(&page->locality_list, &scheme->locality_list);
				hash_add(scheme->locality_hash_tbl,  scheme->hash_table_size, page, page->nm_page);
				scheme->locality_list_size++;
			}
			spin_unlock(&scheme->lock);
	}
}


/**
 * Timer callback. Tested and doesn't crash atm.
 */
static void timer_callback (struct task_struct* p, int user_tick)
{	
	struct phase_shift_detection_scheme* scheme = (struct phase_shift_detection_scheme*) p->phase_shifts_private_data;
	int detected = 0;
	char buff[TASK_COMM_LEN];
	// Check if a scheme on this process is well defined, and tick was user time.
	if(scheme && user_tick)
	{
		/* 
		 * Locking the phase shift detection scheme associated with current.
		 */
		spin_lock(&scheme->lock); 
		
		// Check for a tick - if previous tick had no faults while current one did.
		if(!(scheme->previous_tick_faults) && scheme->current_tick_faults > 0)
		{
			// If so, sets a flag to indicate a detection.
			detected = 1;
		}
		
		// Resetting counters. 
		scheme->previous_tick_faults = scheme->current_tick_faults;
		scheme->current_tick_faults = 0;
		
		
		spin_unlock(&scheme->lock);
		
		// After exiting critical section, can safely print of a shift detection.
		
		if(detected)
		{
			printk( KERN_ALERT "%s[%d]: phase shift detected. \n", get_task_comm(buff, p), task_pid_nr(p) );
		}
	}
}

static int phase_shifts_init(void)
{
	// Init callbacks.
	phase_shifts_algorithm->exit_callback = exit_callback;
	phase_shifts_algorithm->exec_callback = exec_callback;
	phase_shifts_algorithm->fault_callback = fault_callback;
	phase_shifts_algorithm->timer_callback = timer_callback;
	
	printk(KERN_ALERT "Phase shifts detection algorithm activated. \n");
	return 0;
}

static void phase_shifts_exit(void)
{
	// Deactivate callbacks.
	phase_shifts_algorithm->exit_callback = NULL;
	phase_shifts_algorithm->exec_callback = NULL;
	phase_shifts_algorithm->fault_callback = NULL;
	phase_shifts_algorithm->timer_callback = NULL;
	
	
	printk(KERN_ALERT "Phase shifts detection algorithm deactivated. \n");
}

module_init(phase_shifts_init);
module_exit(phase_shifts_exit);

