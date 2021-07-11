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

	/* must be equal */
	KASSERT(vaddr == ((ph.p_vaddr & PAGE_FRAME) + page_offset_from_segbase));
	/* prepare data to give to load_segment */

	int bytes_to_align_first =  ph.p_vaddr - (ph.p_vaddr & PAGE_FRAME);

	/* if the page we want is greater than the segment size, it means that we have to just allocate an empty page */
	if (page_offset_from_segbase >= ph.p_filesz + bytes_to_align_first)
	{
		result = 0;
	}
	else /* else, we have to read from file */
	{
		/* 
		 * load the needed page .
		 */

		/* if we want to read the first page */
		if (page_offset_from_segbase == 0)
		{
			/* if we are in the first page, we have to read starting from the first used vaddr */
			vaddr = ph.p_vaddr;
			bytes_toread_from_file = ((ph.p_vaddr & PAGE_FRAME) + PAGE_SIZE) - ph.p_vaddr;
		}

		/*
		 * if  ph.p_filesz-page (segment size minus offset of page) is less than PAGE_SIZE, 
		 * read the remaning part (less than PAGE_SIZE)
		 */
		else if (bytes_to_align_first + ph.p_filesz - page_offset_from_segbase < PAGE_SIZE)
		{
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