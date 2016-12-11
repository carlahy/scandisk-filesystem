#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"

//Returns the number of clusters for a file
int get_file_cluster_length(uint16_t cluster, uint8_t *image_buf, struct bpb33* bpb) {

	int cluster_len = 0;
	while(!is_end_of_file(cluster) ) {
		cluster = get_fat_entry(cluster, image_buf, bpb);
		cluster_len++;
	}
	return cluster_len;
}

//Iterates through the array of clusters and lists the unreferenced clusters
void find_unreferenced(int referencedClusters[], int numClusters) {
	//The first cluster is at index 2
	int ucount = 0;
    printf("Unreferenced:");
    for(int i = 2; i < numClusters; i++) {
    	if(referencedClusters[i] != 1) {
    		printf(" %i", i);
    		ucount++;
    	}
    }
    printf("\nThere are %i unreferenced\n", ucount);
}

//Iterates through the array of clusters not referenced by any file, and checks if they are free in the FAT 
void check_cluster_free(int referencedClusters[], uint8_t* image_buf, struct bpb33* bpb) {
	for(int i=0; i < bpb->bpbSectors / bpb->bpbSecPerClust; i++) {
		if(get_fat_entry(i, image_buf, bpb) == CLUST_FREE) {
			referencedClusters[i] = 1;
		}
	}
	return;
}

//Follows the linked list for a file in the FAT 
void follow_FAT(int referencedClusters[], uint16_t cluster, uint8_t* image_buf, struct bpb33* bpb) {

	while(!is_end_of_file(cluster) ) {
		//Mark cluster as referenced
		referencedClusters[cluster] = 1;
		cluster = get_fat_entry(cluster, image_buf, bpb);
		//Cluster is FREE or ROOT, either way is not correct end of file termination
		if(cluster == 0) {
			break;
		}

	}
	return;
}

void check_referenced_clusters(int referencedClusters[], uint16_t cluster, uint8_t* image_buf, struct bpb33* bpb) {
	struct direntry *dirent;
    int d,i;
    dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    int bytesPerCluster = bpb->bpbBytesPerSec * bpb->bpbSecPerClust;

    while (1) {

		for (d = 0; d < bytesPerCluster; d += sizeof(struct direntry)) {
		    char name[9];
		    char extension[4];
		    uint16_t file_cluster;
		    name[8] = ' ';
		    extension[3] = ' ';
		    memcpy(name, &(dirent->deName[0]), 8);
		    memcpy(extension, dirent->deExtension, 3);
		    if (name[0] == SLOT_EMPTY)
			return;

		    /* skip over deleted entries */
		    if (((uint8_t)name[0]) == SLOT_DELETED)	{
		    	continue;
		    }
		    /* names are space padded - remove the spaces */
		    for (i = 8; i > 0; i--) {
				if (name[i] == ' ') 
				    name[i] = '\0';
				else 
				    break;
		    }
		    /* remove the spaces from extensions */
		    for (i = 3; i > 0; i--) {
				if (extension[i] == ' ') 
				    extension[i] = '\0';
				else 
				    break;
		    }
		    /* skip over '.' and '..' directories */
		    if (strcmp(name, ".")==0) {
				dirent++;
				continue;
		    }
		    if (strcmp(name, "..")==0) {
				dirent++;
				continue;
		    }

		    if ((dirent->deAttributes & ATTR_VOLUME) != 0) {
		    	//Skip over
	    	} else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
		    	//Cluster is name of a directory, keep looking for a file
				file_cluster = getushort(dirent->deStartCluster);
				check_referenced_clusters(referencedClusters, file_cluster, image_buf, bpb);
		    } else {
		    	//Cluster is start of a file, follow FAT for this file and mark all clusters as referenced
				uint16_t file_start_cluster = getushort(dirent->deStartCluster);
				follow_FAT(referencedClusters, file_start_cluster, image_buf, bpb);
		    }
		    dirent++;
		}
		if (cluster == 0) {
		    // root dir is special
		    dirent++;
		} else {
		    cluster = get_fat_entry(cluster, image_buf, bpb);
		    dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
		}

	}
	return;
}

/* Write the values into a directory entry */
void write_dirent(struct direntry *dirent, char *filename, 
		   uint16_t start_cluster, uint32_t size)
{
    char *p, *p2;
    char *uppername;
    int len, i;

    /* clean out anything old that used to be here */
    memset(dirent, 0, sizeof(struct direntry));

    /* extract just the filename part */
    uppername = strdup(filename);
    p2 = uppername;
    for (i = 0; i < strlen(filename); i++) {
	if (p2[i] == '/' || p2[i] == '\\') {
	    uppername = p2+i+1;
	}
    }

    /* convert filename to upper case */
    for (i = 0; i < strlen(uppername); i++) {
	uppername[i] = toupper(uppername[i]);
    }

    /* set the file name and extension */
    memset(dirent->deName, ' ', 8);
    p = strchr(uppername, '.');
    memcpy(dirent->deExtension, "___", 3);
    if (p == NULL) {
	fprintf(stderr, "No filename extension given - defaulting to .___\n");
    } else {
	*p = '\0';
	p++;
	len = strlen(p);
	if (len > 3) len = 3;
	memcpy(dirent->deExtension, p, len);
    }
    if (strlen(uppername)>8) {
	uppername[8]='\0';
    }
    memcpy(dirent->deName, uppername, strlen(uppername));
    free(p2);

    /* set the attributes and file size */
    dirent->deAttributes = ATTR_NORMAL;
    putushort(dirent->deStartCluster, start_cluster);
    putulong(dirent->deFileSize, size);

    /* a real filesystem would set the time and date here, but it's
       not necessary for this coursework */
}


/* create_dirent finds a free slot in the directory, and write the
   directory entry */

void create_dirent(struct direntry *dirent, char *filename, 
		   uint16_t start_cluster, uint32_t size,
		   uint8_t *image_buf, struct bpb33* bpb)
{
    while(1) {
	if (dirent->deName[0] == SLOT_EMPTY) {
	    /* we found an empty slot at the end of the directory */
	    write_dirent(dirent, filename, start_cluster, size);
	    dirent++;

	    /* make sure the next dirent is set to be empty, just in
	       case it wasn't before */
	    memset((uint8_t*)dirent, 0, sizeof(struct direntry));
	    dirent->deName[0] = SLOT_EMPTY;
	    return;
	}
	if (dirent->deName[0] == SLOT_DELETED) {
	    /* we found a deleted entry - we can just overwrite it */
	    write_dirent(dirent, filename, start_cluster, size);
	    return;
	}
	dirent++;
    }
}

void usage()
{
    fprintf(stderr, "Usage: dos_scandisk <imagename>\n");
    exit(1);
}

void recover_lost_files(int referencedClusters[], int numClusters, uint8_t *image_buf, struct bpb33* bpb) {
	//The first cluster is at index 2
	//Keep track of how many files are lost
	int lost_count = 0;

    for(int i = 2; i < numClusters; i++) {

    	//Find lost file
    	if(referencedClusters[i] != 1) {

    		lost_count++;

    		//Find lost file
    		uint32_t file_length = get_file_cluster_length(i, image_buf, bpb);
    		printf("Lost File: %i %i\n", i, file_length);

    		//Recover lost file
    		//Mark lost file clusters as referenced
    		follow_FAT(referencedClusters, i, image_buf, bpb);

    		struct direntry *dirent = (struct direntry*) cluster_to_addr(0, image_buf, bpb);
    		//Set file name
    		char prefix[] = "found";
    		char extension[] = ".dat";
    		char file_name[13];
    		sprintf(file_name, "%s%i%s", prefix, lost_count, extension);

    		int start_cluster = i;
    		int cluster_bytes = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;
    		uint32_t file_size = file_length * cluster_bytes;

    		//Create a directory entry
    		create_dirent(dirent, file_name, start_cluster, file_size, image_buf, bpb);
    	}
    }
    printf("\n");
}

int main(int argc, char** argv) {

	uint8_t *image_buf;
	int fd;
	struct bpb33* bpb;
	if(argc != 2) {
		usage();
	}

	image_buf = mmap_file(argv[1], &fd);
    bpb = check_bootsector(image_buf);

    int num_clusters = bpb->bpbSectors / bpb->bpbSecPerClust;
    //int cluster_bytes = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;

    int referencedClusters[num_clusters];
    //Initialise as 0 for unreferenced
    for(int i = 0; i < num_clusters; i++) {
    	referencedClusters[i] = 0;
    }

    //Mark referenced all clusters used in files
    check_referenced_clusters(referencedClusters, 0, image_buf, bpb);

    //Mark referenced all FREE clusters in FAT 
    check_cluster_free(referencedClusters, image_buf, bpb);

    //Print the unreferenced clusters
    find_unreferenced(referencedClusters, num_clusters);

    //Print the lost files and their size, and recover them
   	recover_lost_files(referencedClusters, num_clusters, image_buf, bpb);


    close(fd);
    exit(0);

}