#ifndef _ST_H_
#define _ST_H_

typedef struct symboltable *ST;

ST  	STinit(int) ;
void 	STinsert(ST, Item) ;
Item    STsearch(ST, pid_t, vaddr_t);
void    STdelete(ST , pid_t , vaddr_t );
void	STdisplay(ST) ;

#endif