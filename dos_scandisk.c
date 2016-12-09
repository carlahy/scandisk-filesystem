#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>

#include "bootsect.h"
#include "bpb.h"
#include "direntry.h"
#include "fat.h"
#include "dos.h"

int get_file_length(uint16_t cluster, uint8_t *image_buf, struct bpb33* bpb) {

	int len = 1;
	while(!is_end_of_file(cluster) ) {
		cluster = get_fat_entry(cluster, image_buf, bpb);
		len++;
	}
	return len;
}

void print_unreferenced(int referencedClusters[], int numClusters) {
	//The first cluster is at index 2
    printf("Unreferenced:");
    for(int i = 2; i < numClusters; i++) {
    	if(referencedClusters[i] != 1) {
    		printf(" %i", i);
    	}
    }
    printf("\n");
}

void print_lost_files(int referencedClusters[], int numClusters, uint8_t *image_buf, struct bpb33* bpb) {
	//The first cluster is at index 2
    for(int i = 2; i < numClusters; i++) {

    	if(referencedClusters[i] != 1) {
    		printf("Lost File:");
    		int length = get_file_length(i, image_buf, bpb);
    		printf(" %i %i\n", i, length);
    	}
    }
    printf("\n");
}

void check_cluster_free(int referencedClusters[], uint8_t* image_buf, struct bpb33* bpb) {
	for(int i=0; i < bpb->bpbSectors / bpb->bpbSecPerClust; i++) {
		if(get_fat_entry(i, image_buf, bpb) == CLUST_FREE) {
			referencedClusters[i] = 1;
		}
	}
	return;
}

void follow_FAT(int referencedClusters[], uint16_t cluster, uint8_t* image_buf, struct bpb33* bpb) {

	int clustcount = 0;
	while(!is_end_of_file(cluster) ) {
		//Mark cluster as referenced
		referencedClusters[cluster] = 1;
		cluster = get_fat_entry(cluster, image_buf, bpb);
		clustcount++;
		//Cluster is FREE or ROOT, either way is not correct end of file termination
		//CANADA deal with end of cluster?
		if(cluster == 0) {
			break;
		}

	}
	printf("This file has %i clusters\n", clustcount);
	return;
}

void check_referenced_clusters(int referencedClusters[], uint16_t cluster, uint8_t* image_buf, struct bpb33* bpb) {
	struct direntry *dirent;
    int d,i;
    dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    int bytesPerCluster = bpb->bpbBytesPerSec * bpb->bpbSecPerClust;

    while (1) {
    	//Parse directory (for all directories)
    	
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
		    	//
	    	} else if ((dirent->deAttributes & ATTR_DIRECTORY) != 0) {
		    	//Cluster is name of a directory, keep looking for a file
				file_cluster = getushort(dirent->deStartCluster);
				check_referenced_clusters(referencedClusters, file_cluster, image_buf, bpb);
		    } else {
		    	//Cluster is start of a file, follow FAT for this file
				uint16_t file_start_cluster = getushort(dirent->deStartCluster);
				printf("Start of a file %i, length is %i\n", file_start_cluster, getulong(dirent->deFileSize));
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

void usage()
{
    fprintf(stderr, "Usage: dos_scandisk <imagename>\n");
    exit(1);
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

    int numClusters = bpb->bpbSectors / bpb->bpbSecPerClust;

    //int bytesPerCluster = bpb->bpbSecPerClust * bpb->bpbBytesPerSec
    printf("Total number of clusters = %i\n", numClusters);
    int referencedClusters[numClusters];

    //Mark referenced all clusters used in files
    check_referenced_clusters(referencedClusters, 0, image_buf, bpb);

    //Mark referenced all FREE clusters in FAT 
    check_cluster_free(referencedClusters, image_buf, bpb);

    print_unreferenced(referencedClusters, numClusters);

   	print_lost_files(referencedClusters, numClusters, image_buf, bpb);


    close(fd);
    exit(0);

}