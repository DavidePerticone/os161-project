/*
 * Modified version of the hash table library given at the laboratory number 11
 * of the Algorithm and Programming course, held by Stefano Quer at PoliTo. 
 * The library has been modified extensively modified to be suitable for the needs 
 * of this project. The original version can be found at the following link:
 * http://fmgroup.polito.it/quer/teaching/apaEn/laib/testi/lab11-HTLibrary/
 */

#ifndef _ST_H_
#define _ST_H_
#include <item.h>
typedef struct symboltable *ST;

ST  	STinit(int) ;
void 	STinsert(ST, Item) ;
int     STsearch(ST, pid_t, vaddr_t);
void    STdelete(ST , pid_t , vaddr_t );
void	STdisplay(ST) ;

#endif