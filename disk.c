/*
CSE 30341 Spring 2025 Flash Translation Assignment.
This is the flash translation layer.
You should write all your code here.
*/

#include "disk.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

# include <stdbool.h>

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
	int * eraseCounts; 
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
    int required_free_pages = 20;
    int num_blocks = d->flash_pages / d->pages_per_block;

    while (1) {
        int free_pages = 0;
        for (int i = 0; i < d->flash_pages; i++) {
            if (d->status[i] == 0) free_pages++;
        }
        if (free_pages >= required_free_pages) break;

        int best_block = -1;
        int max_stale = -1;

        for (int b = 0; b < num_blocks; b++) {
            int start = b * d->pages_per_block;
            int stale_count = 0;
            for (int i = start; i < start + d->pages_per_block; i++) {
                if (d->status[i] == 2) stale_count++;
            }
            if (stale_count > max_stale) {
                best_block = b;
                max_stale = stale_count;
            }
        }

        if (best_block == -1 || max_stale == 0) {
            fprintf(stderr, "GC: no suitable block found.\n");
            return;
        }

        int start = best_block * d->pages_per_block;
        int end   = start + d->pages_per_block;

        bool can_erase = true;
        int valid_total = 0;
        int valid_moved = 0;
        int moved_pages[d->pages_per_block];  // to track pages we moved

        // Count valid pages
        for (int i = start; i < end; i++) {
            if (d->status[i] == 1) valid_total++;
        }

        // Relocate valid pages, collect stale list
        for (int i = start; i < end; i++) {
            if (d->status[i] == 1) {
                char buf[DISK_BLOCK_SIZE];
                flash_read(d->flash_drive, i, buf);

                int new_page = -1;
                for (int j = 0; j < d->flash_pages; j++) {
                    if (d->status[j] == 0) {
                        new_page = j;
                        break;
                    }
                }

                if (new_page == -1) {
                    can_erase = false;
                    break;
                }

                flash_write(d->flash_drive, new_page, buf);
                d->status[new_page] = 1;

                for (int k = 0; k < d->disk_blocks; k++) {
                    if (d->b2p[k] == i) {
                        d->b2p[k] = new_page;
                        break;
                    }
                }

                moved_pages[valid_moved++] = i;  // save to mark stale later
            }
        }

        // Only erase if all pages were moved
        if (can_erase && valid_moved == valid_total) {
            for (int m = 0; m < valid_moved; m++) {
                d->status[moved_pages[m]] = 2;  // mark old as stale now
            }

            flash_erase(d->flash_drive, best_block);
            d->eraseCounts[best_block]++;

            for (int i = start; i < end; i++) {
                if (d->status[i] != 1) {  // don't overwrite newly moved ones
                    d->status[i] = 0;
                }
            }
        } else {
            // Failed to move all valid pages
            continue;
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

	int blocks = d->flash_pages / d->pages_per_block;
	d->eraseCounts = malloc(sizeof(int) * blocks);
	memset(d->eraseCounts, 0, sizeof(int) * blocks);


	return d;
}

/*
Read a disk block through the flash translation layer.
*/
int disk_read( struct disk *d, int disk_block, char *data )
{
	printf("disk_read: block %d\n",disk_block);
	
	// find the right physical flash page 
	int page = d->b2p[disk_block];

	// check the condition of the page 
	if (page == -1 || d->status[page] != 1) { 
		fprintf(stderr, "CRASH: disk_read of block %d failed (unmapped or stale)\n", disk_block);
		// memset(data, disk_block % 127, DISK_BLOCK_SIZE);
		return -1; // verify correctness
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
int disk_write(struct disk *d, int disk_block, const char *data) {
    printf("disk_write: block %d\n", disk_block);

    // Save old page before doing anything
    int old_page = d->b2p[disk_block];

    // Trigger GC if low on free space
    int free_count = 0;
    for (int i = 0; i < d->flash_pages; i++) {
        if (d->status[i] == 0) free_count++;
    }
    if (free_count < 10) {
        garbage_collection(d);
    }

    // pick free page in block with lowest erase count 
    int free_page = -1;
    int min_erase = __INT_MAX__;

    for (int i = 0; i < d->flash_pages; i++) {
        if (d->status[i] == 0) {
            int block = i / d->pages_per_block;
            if (d->eraseCounts[block] < min_erase) {
                min_erase = d->eraseCounts[block];
                free_page = i;
            }
        }
    }

    // If still no free page, run GC again
    if (free_page == -1) {
        garbage_collection(d);
        min_erase = __INT_MAX__;
        for (int i = 0; i < d->flash_pages; i++) {
            if (d->status[i] == 0) {
                int block = i / d->pages_per_block;
                if (d->eraseCounts[block] < min_erase) {
                    min_erase = d->eraseCounts[block];
                    free_page = i;
                }
            }
        }
        if (free_page == -1) {
            fprintf(stderr, "disk_write: no free pages even after GC.\n");
            return -1;
        }
    }

    // Perform the write
    flash_write(d->flash_drive, free_page, data);
    d->b2p[disk_block] = free_page;
    d->status[free_page] = 1;
    d->nwrites++;

    // Mark old page as stale, safely (after the write)
    if (old_page != -1 && old_page != free_page) {
        d->status[old_page] = 2;
    }

    return 0;
}

/*
Report the total number of operations performed.
You can add more if you like here, but keep the display of reads and writes.
*/
void disk_report( struct disk *d )
{
	free(d->eraseCounts);
	printf("\tdisk reads: %d\n",d->nreads);
	printf("\tdisk writes: %d\n",d->nwrites);
}
