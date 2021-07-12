#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <elf.h>
#include <opt-virtualmem.h>
#include <kern/fcntl.h>
#include <segments.h>
#include <vfs.h>
#include <instrumentation.h>
/*
 * Load a segment at virtual address VADDR. The segment in memory
 * extends from VADDR up to (but not including) VADDR+MEMSIZE. The
 * segment on disk is located at file offset OFFSET and has length
 * FILESIZE.
 *
 * FILESIZE may be less than MEMSIZE; if so the remaining portion of
 * the in-memory segment should be zero-filled.
 *
 * Note that uiomove will catch it if someone tries to load an
 * executable whose load address is in kernel space. If you should
 * change this code to not use uiomove, be sure to check for this case
 * explicitly.
 */

static int
load_segment(struct addrspace *as, struct vnode *v,
			 off_t offset, vaddr_t vaddr,
			 size_t memsize, size_t filesize,
			 int is_executable)
{
	struct iovec iov;
	struct uio u;
	int result;

	if (filesize > memsize)
	{
		kprintf("ELF: warning: segment filesize > segment memsize\n");
		filesize = memsize;
	}

	DEBUG(DB_EXEC, "ELF: Loading %lu bytes to 0x%lx\n",
		  (unsigned long)filesize, (unsigned long)vaddr);

	iov.iov_ubase = (userptr_t)vaddr;
	iov.iov_len = memsize; // length of the memory space
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = filesize; // amount to read from the file
	u.uio_offset = offset;
	u.uio_segflg = is_executable ? UIO_USERISPACE : UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = as;

	result = VOP_READ(v, &u);
	if (result)
	{
		return result;
	}

	if (u.uio_resid != 0)
	{
		kprintf("ELF: short read on segment - file truncated?\n");
		return ENOEXEC;
	}

	/* * If memsize > filesize, the remaining space should be
	 * zero-filled. There is no need to do this explicitly,
	 * because the VM system should provide pages that do not
	 * contain other processes' data, i.e., are already zeroed.
	 *
	 * During development of your VM system, it may have bugs that
	 * cause it to (maybe only sometimes) not provide zero-filled
	 * pages, which can cause user programs to fail in strange
	 * ways. Explicitly zeroing program BSS may help identify such
	 * bugs, so the following disabled code is provided as a
	 * diagnostic tool. Note that it must be disabled again before
	 * you submit your code for grading.*/

#if 0
	{
		size_t fillamt;

		fillamt = memsize - filesize;
		if (fillamt > 0)
		{
			DEBUG(DB_EXEC, "ELF: Zero-filling %lu more bytes\n",
				  (unsigned long)fillamt);
			u.uio_resid += fillamt;
			result = uiomovezeros(fillamt, &u);
		}
	}
#endif

	return result;
}

int load_page(vaddr_t page_offset_from_segbase, vaddr_t vaddr, int segment)
{

	int bytes_toread_from_file;
	struct vnode *v;
	int result;
	struct iovec iov;
	struct uio ku;
	Elf_Ehdr eh = curproc->p_eh;
	Elf_Phdr ph;

	/* Open the file. */
	result = vfs_open(curproc->p_name, O_RDONLY, 0, &v);
	if (result)
	{
		return result;
	}

	/* read the program header for the needed segment */
	off_t offset = eh.e_phoff + segment * eh.e_phentsize;
	uio_kinit(&iov, &ku, &ph, sizeof(ph), offset, UIO_READ);

	result = VOP_READ(v, &ku);
	if (result)
	{
		return result;
	}

	if (ku.uio_resid != 0)
	{
		/* short read; problem with executable? */
		kprintf("ELF: short read on phdr - file truncated?\n");
		return ENOEXEC;
	}

	switch (ph.p_type)
	{
	case PT_NULL: /* skip */
		return 0;
	case PT_PHDR: /* skip */
		return 0;
	case PT_MIPS_REGINFO: /* skip */
		return 0;
	case PT_LOAD:
		break;
	default:
		kprintf("loadelf: unknown segment type %d\n",
				ph.p_type);
		return ENOEXEC;
	}

	/*
	 * IMPORTANT!
	 * It is very much worth noticing the following.
	 * When we need to load a page, three cases can occur:
	 * 
	 * 1)We must load the first page of the segment
	 * 2)We must load an intermediate page of the segment
	 * 3)We must load the last page of the segment
	 * 
	 * Depending on the case, the behaviour must be different.
	 * In the case (1), it is IMPORTANT to understand that we have to load the the first page
	 * starting from the vaddr of the segment. With an example, when we load a page, it could happen
	 * that the starting address is not page align: (page boundery)00444 (where 0 means not used).
	 * It is clear that the segment does not start at a multiple of a page. This MUST be taken into
	 * account when loading the first page, as well as all the others.
	 * When we load the first page, the amount of byte to read from the file could be not multiple of 
	 * a page: this implies that less than PAGE_SIZE must be read (precisily, we have to read from
	 * the starting address to the following page multiple).
	 * 
	 * When we are in case (2), any page we want to read is page aligned (properly, the first used address of 
	 * that page is aligned). This simplifies a bit things. Unfortunately, we have to keep into account that 
	 * the offset inside the file is calculated starting from the first address used in the first page, and not from
	 * the start of the page-aligned segment (they could be the same, if starting address is a multiple of a page).
	 * This means that if a segment first address is not multiple of a page, we have a segment like the following 00444 4444
	 * but on file it will be stored as 444 4444. When calculating the true offset inside the file to load the right page,
	 * we have to considering the fact that the initial padding is missing in the file. 
	 * Indeed, page_offset_from_segbase is the offset from the page-aligned segment and not from the first address.
	 * This means that we have to add the initial padding to any calculation we will perform, if we want things to work.
	 * 
	 * In the last case, (3), things are easy and similar to (2). The only true difference is that the amount of bytes
	 * that we read could be less than PAGE_SIZE. For example: 0444 4444 4400. In this case, when reading the last page, we 
	 * have to read just 44 and not the whole page (which is not present in the file). This can be easily prevented.
	 */ 


	/* 
	 * vaddr is the address of the page where the fault occured, that must be equal to the page-aligned
	 * starting segment address + the offset from it of the page we want to read.
	 */
	KASSERT(vaddr == ((ph.p_vaddr & PAGE_FRAME) + page_offset_from_segbase));
	
	/* bytes_to_align_first stores the padding bytes (if any) of the first page (example: 00044, it will contain 3) */

	int bytes_to_align_first =  ph.p_vaddr - (ph.p_vaddr & PAGE_FRAME);

	/* if the page we want is greater than the segment size, it means that we have to just allocate an empty page */
	if (page_offset_from_segbase >= ph.p_filesz + bytes_to_align_first)
	{
		increase(NEW_PAGE_ZEROED);
		result = 0;
	}
	else /* else, we have to read from file */
	{	
		increase(FAULT_WITH_LOAD);
		increase(FAULT_WITH_ELF_LOAD);

		/* 
		 * load the needed page .
		 */

		/* if we want to read the first page */
		if (page_offset_from_segbase == 0)
		{
			/* if we are in the first page, we have to read starting from the first used vaddr */
			vaddr = ph.p_vaddr;
			/* amount of bytes to read */
			bytes_toread_from_file = ((ph.p_vaddr & PAGE_FRAME) + PAGE_SIZE) - ph.p_vaddr;
		}

		/*
		 * if  ph.p_filesz-page (segment size minus offset of page) is less than PAGE_SIZE, 
		 * read the remaning part (less than PAGE_SIZE)
		 */
		else if (bytes_to_align_first + ph.p_filesz - page_offset_from_segbase < PAGE_SIZE)
		{
			/* we have to compesate for the possible padding present in the first page */
			page_offset_from_segbase = page_offset_from_segbase - PAGE_SIZE + ((ph.p_vaddr & PAGE_FRAME) + PAGE_SIZE) - ph.p_vaddr;
			bytes_toread_from_file = bytes_to_align_first + ph.p_filesz - page_offset_from_segbase;
		}
		/* read PAGE_SIZE */
		else
		{
			page_offset_from_segbase = page_offset_from_segbase - PAGE_SIZE + ((ph.p_vaddr & PAGE_FRAME) + PAGE_SIZE) - ph.p_vaddr;
			bytes_toread_from_file = PAGE_SIZE;
		}

		result = load_segment(curproc->p_addrspace, v, ph.p_offset + page_offset_from_segbase, vaddr,
							  PAGE_SIZE, bytes_toread_from_file,
							  ph.p_flags & PF_X);
	}

	if (result)
	{
		return result;
	}

	result = as_complete_load(curproc->p_addrspace);
	if (result)
	{
		return result;
	}

	return 0;
}