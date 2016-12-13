
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

//Returns the number of clusters for a file, excluding EOF cluster
uint32_t get_file_fat_length(uint16_t cluster, uint8_t *image_buf, struct bpb33* bpb) {

	uint32_t cluster_len = 0;
	while(!is_end_of_file(cluster) ) {
		cluster_len++;
		cluster = get_fat_entry(cluster, image_buf, bpb);
	}
	return cluster_len;
}

//Iterates through the array of clusters and lists the unreferenced clusters
void find_unreferenced(int referencedClusters[], int num_clusters) {
	//The first cluster is at index 2
    int shown = 0;
    int i;
    for(i = 2; i < num_clusters; i++) {
    	if(referencedClusters[i] != 1) {
    		if(!shown) {
    			printf("Unreferenced:");
    			shown = 1;
    		}
    		printf(" %i", i);
    	}
    }
    if(shown) {
	    printf("\n");    	
    }
    return;
}

//Iterates through the array of clusters not referenced by any file, and checks if they are free in the FAT 
void check_cluster_free(int referencedClusters[], uint8_t* image_buf, struct bpb33* bpb) {
	int i;
	for(i=0; i < bpb->bpbSectors / bpb->bpbSecPerClust; i++) {
		if(get_fat_entry(i, image_buf, bpb) == CLUST_FREE) {
			referencedClusters[i] = 1;
		}
	}
	return;
}

//Traverses the FAT to follow the linked list for a file
void traverse_fat(int referencedClusters[], uint16_t cluster, uint8_t* image_buf, struct bpb33* bpb) {

	while(!is_end_of_file(cluster) ) {
		//Mark cluster as referenced
		referencedClusters[cluster] = 1;
		cluster = get_fat_entry(cluster, image_buf, bpb);
	} 
	return;
}

void check_referenced_clusters(int referencedClusters[], uint16_t cluster, uint8_t* image_buf, struct bpb33* bpb) {
	struct direntry *dirent;
    int d,i;
    dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    int cluster_bytes = bpb->bpbBytesPerSec * bpb->bpbSecPerClust;

    //Mark cluster as referenced
    referencedClusters[cluster] = 1;

    while (1) {

		for (d = 0; d < cluster_bytes; d += sizeof(struct direntry)) {
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
		    	//Entry is name of a directory, mark cluster as referenced and keep looking for a file
				file_cluster = getushort(dirent->deStartCluster);
				check_referenced_clusters(referencedClusters, file_cluster, image_buf, bpb);
		    } else {
		    	//Entry is start of a file, follow FAT for this file and mark all clusters as referenced
				uint16_t file_start_cluster = getushort(dirent->deStartCluster);
				traverse_fat(referencedClusters, file_start_cluster, image_buf, bpb);
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

/* get_name retrieves the filename from a directory entry */

void get_name(char *fullname, struct direntry *dirent) 
{
    char name[9];
    char extension[4];
    int i;

    name[8] = ' ';
    extension[3] = ' ';
    memcpy(name, &(dirent->deName[0]), 8);
    memcpy(extension, dirent->deExtension, 3);

    /* names are space padded - remove the padding */
    for (i = 8; i > 0; i--) {
		if (name[i] == ' ') 
		    name[i] = '\0';
		else 
		    break;
    }

    /* extensions aren't normally space padded - but remove the
       padding anyway if it's there */
    for (i = 3; i > 0; i--) {
		if (extension[i] == ' ') 
		    extension[i] = '\0';
		else 
		    break;
    }
    fullname[0]='\0';
    strcat(fullname, name);

    /* append the extension if it's not a directory */
    if ((dirent->deAttributes & ATTR_DIRECTORY) == 0) {
		strcat(fullname, ".");
		strcat(fullname, extension);
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
    return;
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
    return;
}

void recover_lost_files(int referencedClusters[], int num_clusters, uint8_t *image_buf, struct bpb33* bpb) {
	//Keep track of how many files are lost for file naming
	int lost_count = 0;

	//The first cluster is at index 2
	int i;
   	for(i = 2; i < num_clusters; i++) {

    	//Find lost file
    	if(referencedClusters[i] != 1) {

    		lost_count++;

    	//Print lost file
    		uint32_t file_length = get_file_fat_length(i, image_buf, bpb);
    		printf("Lost File: %i %i\n", i, file_length);

    	//Recover lost file
    		//Mark recovered file clusters as referenced
    		traverse_fat(referencedClusters, i, image_buf, bpb);

    		//Set file name
    		char prefix[] = "found";
    		char extension[] = ".dat";
    		char file_name[13];
    		sprintf(file_name, "%s%i%s", prefix, lost_count, extension);

    		int start_cluster = i;
    		int cluster_bytes = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;
    		uint32_t file_size = file_length * cluster_bytes;

    		//Create a new directory entry
    		struct direntry *dirent = (struct direntry*) cluster_to_addr(0, image_buf, bpb);
    		create_dirent(dirent, file_name, start_cluster, file_size, image_buf, bpb);
    	}
    }
    return;
}

void check_length_consistency(uint16_t cluster, uint8_t* image_buf, struct bpb33* bpb) {
	struct direntry *dirent;
    int d,i;
    dirent = (struct direntry*)cluster_to_addr(cluster, image_buf, bpb);
    uint32_t cluster_bytes = bpb->bpbSecPerClust * bpb->bpbBytesPerSec;

    while (1) {

		for (d = 0; d < cluster_bytes; d += sizeof(struct direntry)) {
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
		    	//Entry is name of a directory, keep looking for a file
				file_cluster = getushort(dirent->deStartCluster);
				check_length_consistency(file_cluster, image_buf, bpb);
		    } else {
		    	//Entry is start of a file, check for length consistency between entry and FAT
		    	file_cluster = getushort(dirent->deStartCluster);

		    	uint32_t file_fat_clusters = get_file_fat_length(file_cluster, image_buf, bpb);
		    	uint32_t file_fat_bytes = file_fat_clusters * cluster_bytes;

		    	uint32_t file_dir_bytes = getulong(dirent->deFileSize);
		    	uint32_t file_dir_clusters = (file_dir_bytes + (cluster_bytes-1)) / cluster_bytes;

		    	char file_name[13];
		    	get_name(file_name, dirent);

				if(file_dir_clusters != file_fat_clusters) {
					printf("%s %u %u\n", file_name, file_dir_bytes, file_fat_bytes);

				//Free clusters
					//Find correct end of file cluster as specified in directory
					int i;
					for(i = 0; i < file_dir_clusters-1; i++) {
						file_cluster = get_fat_entry(file_cluster, image_buf, bpb);
					}

					//Terminate the file correctly in the FAT
					uint16_t eof_cluster = file_cluster;
					file_cluster = get_fat_entry(file_cluster, image_buf, bpb);
					set_fat_entry(eof_cluster, FAT12_MASK & CLUST_EOFS, image_buf, bpb);

					//Free clusters of a file that come after EOF cluster in the FAT
					uint16_t next;
					for(i = 0; i < file_fat_clusters - file_dir_clusters; i++) {
						next = get_fat_entry(file_cluster, image_buf, bpb);
						set_fat_entry(file_cluster, FAT12_MASK & CLUST_FREE, image_buf, bpb);
						file_cluster = next;
					}
				}
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

    int num_clusters = bpb->bpbSectors / bpb->bpbSecPerClust;

    int referencedClusters[num_clusters];
    //Initialise as 0 for unreferenced
    int i;
    for(i = 0; i < num_clusters; i++) {
    	referencedClusters[i] = 0;
    }

    //Mark referenced all clusters used in files
    check_referenced_clusters(referencedClusters, 0, image_buf, bpb);

    //Mark referenced all FREE clusters in FAT 
    check_cluster_free(referencedClusters, image_buf, bpb);

    //Print the unreferenced clusters
    find_unreferenced(referencedClusters, num_clusters);

    //Print the lost files and their size, and recover them by creating new entries in directory
   	recover_lost_files(referencedClusters, num_clusters, image_buf, bpb);

   	//Check consistency in file length between FAT and directory specification, and free clusters if necessary
   	check_length_consistency(0,image_buf, bpb);

    close(fd);
    exit(0);

}
