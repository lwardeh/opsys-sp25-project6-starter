/*
CSE 30341 Spring 2025 Flash Translation Assignment.
This is the flash translation layer.
You should write all your code here.
*/

#include "disk.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/*
Structure of the flash translation layer.
Go ahead and add or change things here as needed.
*/

struct disk {
	struct flash_drive *flash_drive;
	int nreads;
	int nwrites;
	// added features below 
	int * b2p; // track the disk_block to flash_page mapping 
	int * status; // [0,1,2] = [free, valid, stale]
};

/*
Create a new flash translation layer for this flash drive f, and simulated number of blocks
Go ahead and add or change things here as needed.
*/

struct disk * disk_create( struct flash_drive *f, int disk_blocks )
{
	struct disk *d = malloc(sizeof(*d));
	d->flash_drive = f;
	d->nreads = 0;
	d->nwrites = 0;
	// new initializations 
	d->b2p  = malloc(sizeof(int) * disk_blocks);
	for (int i = 0; i < disk_blocks; i++) d->b2p[i] = -1;
	d->status = malloc(sizeof(int) * flash_npages(f));
	for (int i = 0; i < flash_npages(f); i++) d->status[i] = 0;
	return d;
}

/*
Read a disk block through the flash translation layer.
Go ahead and add or change things here as needed.
*/

int disk_read( struct disk *d, int disk_block, char *data )
{
	printf("disk_read: block %d\n",disk_block);
	/* A dummy operation that won't get far: read the same page # as block # */
	// flash_read(d->flash_drive,disk_block,data);

	// find the right physical flash page 
	int page = d->b2p[disk_block];

	// check the condition of the page 
	if (page == -1) { 
		fprintf(stderr, "disk_read: block %d has not beem written just yet...\n", disk_block);
		return -1; 
	}

	// read from flash 
	flash_read(d->flash_drive, page, data); 
	d->nreads++;
	return 0;
}

/*
Write a disk block through the flash translation layer.
Go ahead and add or change things here as needed.
*/

int disk_write( struct disk *d, int disk_block, const char *data )
{
	printf("disk_write: block %d\n",disk_block);

	/* A dummy operation that won't get far: write the same page # as block # */
	// flash_write(d->flash_drive,disk_block,data);
	
	// check for old mapping
		// if there is an old mapping, mark the page as STALE (2)
	int oldPage = d->b2p[disk_block];
	if (oldPage != -1) { 
		d->status[oldPage] = 2; // stale!
	}

	// find a free flash page for the block 
	int freePage = -1;
	int npages = flash_npages(d->flash_drive);
	for (int i = 0; i < npages; i++) { 
		if (d->status[i] == 0) {// free
			freePage = i; 
			break;
		}
	}

	// if a free page is not found
	if (freePage == -1) { 
		fprintf(stderr, "disk_write: there are no free pages available & THERE IS NO WAY TO CLEAN STALE PAGES RN -- TODO! \n");
		return -1; // exit with error 
	}

	// write to free page (if found)
	flash_write(d->flash_drive, freePage, data);

	// update the mappings to match 
	d->b2p[disk_block] = freePage; 
	d->status[freePage] = 1; // mark as wrritten 

	d->nwrites++;
	return 0;
}

/*
Report the total number of operations performed.
You can add more if you like here, but keep the display of reads and writes.
*/

void disk_report( struct disk *d )
{
	printf("\tdisk reads: %d\n",d->nreads);
	printf("\tdisk writes: %d\n",d->nwrites);
}
