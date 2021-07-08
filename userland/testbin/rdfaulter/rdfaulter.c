
#include <stdio.h>

#define CODE_SEGMENT_ADDRESS	0x400005

int
main(void)
{
	int i;

	printf("\nEntering the read-only faulter program - I should die immediately\n");
	*(int *)CODE_SEGMENT_ADDRESS = 5;

	// gcc 4.8 improperly demands this
	(void)i;

	printf("I didn't get killed!  Program has a bug\n");
	return 0;
}
