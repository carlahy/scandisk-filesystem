# scandisk-filesystem
OS Coursework 2: Scandisk for FAT12 file system

The objective of this coursework is to write the equivalent of scandisk for a DOS (FAT12) filesystem on a floppy disk. Scandisk is a filesystem checker for DOS filesystems. It can check that a filesystem is internally consistent, and correct any errors found.

The code should:
1. Print out a list of clusters that are not referenced from any file. Print these out separated by spaces in a single line, preceded by “Unreferenced: ”. For example:
     Unreferenced: 5 6 7 8
2. From the unreferenced blocks your code found in part 1., print out the files that make up these blocks. Print out the starting block of the file and the number of blocks in the file in the following format:
     Lost File: 58 3
This indicates that a missing file starts at block 58 and is three blocks long.
3. Create a directory entry in the root directory for any unreferenced files. These should be named “found1.dat”, “found2.dat”, etc.
4. Print out a list of files whose length in the directory entry is inconsistent with their length in the FAT. Print out the filename, its length in the dirent and its length in the FAT. For example:
     foo.txt 23567 8192
bar.txt 4721 4096
5. Free any clusters that are beyond the end of a file (as indicated by the directory entry for that file). Make sure you terminate the file correctly in the FAT.
If your program has succeeded in fixing the filesystem with 3 and 5, re-running on the fixed filesystem should produce no output for 1, 2 or 4.
