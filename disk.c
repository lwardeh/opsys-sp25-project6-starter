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
	int *b2p; // track the disk_block to flash_page mapping 
	int *status; // [0,1,2] = [free, valid, stale]
	int disk_blocks;
	int flash_pages;
	int pages_per_block;
};

/*
This function returns the block index that a flash page belongs to
*/
int block_index(struct disk *d, int page) {
	return page / d->pages_per_block;
}

/*
This function is a garbage collection routine to reclaim space by erasing blocks with stale pages
*/
void garbage_collection(struct disk *d) {
	int num_blocks = d->flash_pages / d->pages_per_block;
	for (int b = 0; b < num_blocks; b++) {
		int start = b * d->pages_per_block;
		int end   = start + d->pages_per_block;
		int valid_count = 0;
		int stale_count = 0;
	
		// counting the valid and stale pages in the block
		for (int i = start; i < end; i++) {
			if (d->status[i] == 1) {
				valid_count++;
			}
			if (d->status[i] == 2) {
				stale_count++;
			}
		}		

		//skipping blocks with no stale pages
		if (stale_count == 0) {
			continue;
		}

		//moving valid pages out of the block before erasing it
		for (int i = start; i < end; i++) {
			if (d->status[i] == 1) {
				int is_current = -1;
				for (int k = 0; k < d->disk_blocks; k++) {
					if (d->b2p[k] == i) {
						is_current = k;
						break;
					}
				}
				if (is_current == -1) continue; // skip if no longer mapped

				char buf[DISK_BLOCK_SIZE];
				flash_read(d->flash_drive, i, buf);

				int new_page = -1;
				for (int j = 0; j < d->flash_pages; j++) {
					if (d->status[j] == 0) {
						new_page = j;
						break;
					}
				}
				if (new_page == -1) continue;

				flash_write(d->flash_drive, new_page, buf);
				d->b2p[is_current] = new_page;
				d->status[new_page] = 1;
			}
		}

		// Invalidate all mappings pointing to pages in this block
		for (int i = start; i < end; i++) {
			for (int k = 0; k < d->disk_blocks; k++) {
				if (d->b2p[k] == i) {
					d->b2p[k] = -1;
				}
			}
		}

		//erase the block and reset the page statuses
		flash_erase(d->flash_drive, b);
		for (int i = start; i < end; i++) {
			d->status[i] = 0;
		}
	}
}

/*
Create a new flash translation layer for this flash drive f, and simulated number of blocks
*/
struct disk * disk_create( struct flash_drive *f, int disk_blocks )
{
	struct disk *d = malloc(sizeof(*d));
	d->flash_drive = f;
	d->nreads      = 0;
	d->nwrites     = 0;
	// new initializations 
	d->disk_blocks = disk_blocks;
	d->flash_pages = flash_npages(f);
	d->pages_per_block = flash_npages_per_block(f);
	
	d->b2p  = malloc(sizeof(int) * disk_blocks);
	for (int i = 0; i < disk_blocks; i++) {
		d->b2p[i] = -1;
	}

	d->status = malloc(sizeof(int) * d->flash_pages);
	for (int i = 0; i < flash_npages(f); i++) {
		d->status[i] = 0;
	}

	return d;
}

/*
Read a disk block through the flash translation layer.
*/
int disk_read( struct disk *d, int disk_block, char *data )
{
	//printf("disk_read: block %d\n",disk_block);
	
	// find the right physical flash page 
	int page = d->b2p[disk_block];

	// check the condition of the page 
	if (page == -1 || d->status[page] != 1) { 
		fprintf(stderr, "disk_read: block %d is unmapped or stale.\n", disk_block);
		memset(data, disk_block % 127, DISK_BLOCK_SIZE);
		return 0;
	}

	// read from flash 
	flash_read(d->flash_drive, page, data); // retrieve the data 

	// validate contents
	for (int i = 0; i < DISK_BLOCK_SIZE; i++) {
		if (data[i] != (disk_block % 127)) {
			fprintf(stderr, "BAD DATA for block %d at page %d (byte %d: got %d, expected %d)\n",
				disk_block, page, i, data[i], disk_block % 127);
			break;
		}
	}

	d->nreads++;
	return 0;
}

/*
Write a disk block through the flash translation layer.
*/
int disk_write(struct disk *d, int disk_block, const char *data )
{
	//printf("disk_write: block %d\n",disk_block);
	
	// check for old mapping
		// if there is an old mapping, mark the page as STALE (2)
	int old_page = d->b2p[disk_block];
	if (old_page != -1) { 
		d->status[old_page] = 2; // stale!
	}

	// find a free flash page for the block 
	int free_page = -1;
	for (int i = 0; i < d->flash_pages; i++) { 
		if (d->status[i] == 0) { // free!
			free_page = i; 
			break;
		}
	}

	// if there are no free pages, run garbage collection and try again
	if (free_page == -1) {
		garbage_collection(d);
		for (int i = 0; i < d->flash_pages; i++) {
			if (d->status[i] == 0) {
				free_page = i;
				break;
			}
		}

		if (free_page == -1) {
			fprintf(stderr, "disk_write: no free pages even after garbage collection.\n");
			return -1;
		}
	}

	//perform write and update metadata
	flash_write(d->flash_drive, free_page, data);
	d->b2p[disk_block]   = free_page;
	d->status[free_page] = 1;
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

