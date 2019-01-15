#include <stdio.h>
#include <stdlib.h>

// Mmap and stat
#include <sys/stat.h>
#include <sys/mman.h>

// Open file 
#include <fcntl.h>

// uintptr_t 
#include <stdint.h>

#include "fs.h"

// memmove
#include<string.h>

// lseek
#include <sys/types.h>
#include <unistd.h>


#define T_DIR 1 // Directory
#define T_FILE 2 // File
#define T_DEV 3 // Device


// Dirent per block.
#define DPB           (BSIZE / sizeof(struct dirent))

typedef unsigned char uchar;

const char* IMAGE_NOT_FOUND = "image not found.";
const char* BAD_INODE = "ERROR: bad inode.";
const char* BAD_DIRECT_ADDR = "ERROR: bad direct address in inode.";
const char* BAD_INDIRECT_ADDR = "ERROR: bad indirect address in inode.";
const char* ROOT_DIR_NOT_EXIST = "ERROR: root directory does not exist.";
const char* DIR_FORMAT_ERROR = "ERROR: directory not properly formatted.";
const char* ADDR_FREE_IN_BITMAP_ERROR = "ERROR: address used by inode but marked free in bitmap.";
const char* BITMAP_MARK_BLOCK_NOT_IN_USE = "ERROR: bitmap marks block in use but it is not in use.";
const char* DIRECT_ADDR_USED_MORE_THAN_ONCE = "ERROR: direct address used more than once.";
const char* INDIRECT_ADDR_USED_MORE_THAN_ONCE = "ERROR: indirect address used more than once.";
const char* INODE_MARKED_USE_NOT_FOUND_IN_DIR = "ERROR: inode marked use but not found in a directory.";
const char* INODE_REFFERED_BUT_MARKED_FREE = "ERROR: inode referred to in directory but marked free.";
const char* BAD_REF_COUNT_FOR_FILE= "ERROR: bad reference count for file.";
const char* DIR_APPEAR_MORE_THAN_ONCE= "ERROR: directory appears more than once in file system.";


int fsfd;
struct superblock *sBlock = NULL;

void rinode(uint inum, struct dinode *ip);
void rsect(uint sec, void *buf);
uint i2b(uint inum);
void printErrorAndExit(const char *message);
uint getBitState(uchar *buf, int index);
void setBitState(uchar *buf, int index);
void createDirentArray(int blockNum, struct dirent *dirContent);



void cleanUp(){
	if(fsfd != -1){
		close(fsfd);
	}
	if(sBlock != NULL){
		free(sBlock);
	}
}


// Each inode is either unallocated or one of the 
// valid types (T_FILE, T_DIR, T_DEV). If not, print ERROR: bad inode.
void check1(){
	uint i = 0;
	for( ;i < sBlock->ninodes; i++){
		
		struct dinode in;
		rinode(i, &in);
		if(in.type < 0 || in.type > 3){	
			printErrorAndExit(BAD_INODE);
		}
	}
}

// For in-use inodes, each address that is used by inode is valid 
// (points to a valid datablock address within the image). 
// If the direct block is used and is invalid, print 
// ERROR: bad direct address in inode.; if the indirect block is in use 
// and is invalid, print ERROR: bad indirect address in inode.
void check2(){
	uint i = 0;
	for( ;i < sBlock->ninodes; i++){
		
		struct dinode in;
		rinode(i, &in);
		if(in.type != 0){	
			uint *dAddress = in.addrs;
			// Checking Direct
			for (int j=0; j< NDIRECT; j++){
				uint cAddr = dAddress[j];
//				printf("Address %ud\n", cAddr);
				if(cAddr != 0){ // TODO Need to check		; can also break ??
					if(cAddr >= sBlock->size || cAddr < 0){
						printErrorAndExit(BAD_DIRECT_ADDR);
					}
				}
			}

			// Checking Indirect
			uint inDirAddr = dAddress[NDIRECT];
			if(inDirAddr != 0){
				char buf[BSIZE];
			  	rsect(inDirAddr, buf);
			  	uint *addrList;
			  	addrList = ((uint *)buf);
			  	for(int j = 0; j< NINDIRECT; j++){
			  		if(addrList[j]!=0){
			  			if(addrList[j] >= sBlock->size || addrList[j] < 0){
							printErrorAndExit(BAD_INDIRECT_ADDR);
						}
					}
				}
			}

		}
	}
}

// Root directory exists, its inode number is 1, and the parent of the 
// root directory is itself. If not, print ERROR: root directory does not exist.
void check3(){
	
	struct dinode in ;
	rinode(ROOTINO, &in);
	if(in.type == T_DIR){
		uint *dAddress = in.addrs;
		if(dAddress != NULL){
			// TODO Assuming dAddress[0] has self and parent dirent
			char buf[BSIZE];
			rsect(dAddress[0], buf);
			struct dirent *dirContent;
			dirContent = ((struct dirent *)buf);
			if(dirContent[1].inum == ROOTINO){
				return;
			}		
	 	}	
	}
	printErrorAndExit(ROOT_DIR_NOT_EXIST);
}

// Each directory contains . and .. entries, and the . entry points to the
//  directory itself. If not, print ERROR: directory not properly formatted.
void check4(){
	
	uint i = 0;
	for( ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		if(in.type == T_DIR){	
			uint *dAddress = in.addrs;
			if(dAddress != NULL){
				// TODO Assuming dAddress[0] has self and parent dirent
				char buf[BSIZE];
				rsect(dAddress[0], buf);
				struct dirent *dirContent;
				dirContent = ((struct dirent *)buf);
				if(dirContent[0].inum == i && strcmp(dirContent[0].name,".") == 0 && strcmp(dirContent[1].name,"..") == 0){
					continue;
				}else{
					printErrorAndExit(DIR_FORMAT_ERROR);
				}			
		 	}else{
				printErrorAndExit(DIR_FORMAT_ERROR);
			 }
		}
	}
	
}

uint getBitState(uchar *buf, int index){
	uint bit = buf[index/8] & (0x1 << (index%8));  
	return (bit > 0);
}

void setBitState(uchar *buf, int index){
	//printf("Setting for index %d \n", index);
	buf[index/8] = buf[index/8] | (0x1 << (index%8));  
}

// For in-use inodes, each address in use is also marked in use in the bitmap. 
// If not, print ERROR: address used by inode but marked free in bitmap.
void check5(){
	uint i = 0;
	
	for( ;i < sBlock->ninodes; i++){
		uint bitMapBlock = BBLOCK(i, sBlock->ninodes);
		uchar bitBuff[BSIZE];
		rsect(bitMapBlock, bitBuff);
		
		struct dinode in;
		rinode(i, &in);
		if(in.type != 0){	
			uint *dAddress = in.addrs;
			// Checking Direct
			for (int j=0; j< NDIRECT; j++){
				uint cAddr = dAddress[j];
				if(cAddr != 0){ // TODO Need to check		; can also break ??
					if(getBitState(bitBuff, cAddr) == 1){
						continue;
					}else{
						printErrorAndExit(ADDR_FREE_IN_BITMAP_ERROR);
					}
				}
			}
			// Checking Indirect
			uint inDirAddr = dAddress[NDIRECT];
			if(inDirAddr != 0){
				char buf[BSIZE];
			  	rsect(inDirAddr, buf);
			  	uint *addrList;
			  	addrList = ((uint *)buf);
			  	for(int j = 0; j< NINDIRECT; j++){
			  		if(addrList[j]!=0){
			  			if(getBitState(bitBuff, addrList[j]) == 1){
							continue;
						}else{
							printErrorAndExit(ADDR_FREE_IN_BITMAP_ERROR);
						}
					}
				}
			}
		}
	}
}

// For blocks marked in-use in bitmap, the block should actually be in-use in 
// an inode or indirect block somewhere. If not, print ERROR: bitmap marks 
// block in use but it is not in use.
void check6(){
	uchar bitBuff[(sBlock->size)/8]; // Need 1024 bits
	memset(bitBuff, 0, sizeof(bitBuff));
	
	uint i = 0;
	
	for( ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		if(in.type != 0){	
			uint *dAddress = in.addrs;
			// Checking Direct
			for (int j=0; j< NDIRECT; j++){
				uint cAddr = dAddress[j];
				if(cAddr != 0){ // TODO Need to check		; can also break ??
					setBitState(bitBuff, cAddr);
					//printf("checking direct set bit %u \n", getBitState(bitBuff, cAddr));
				}
			}
			// Checking Indirect
			uint inDirAddr = dAddress[NDIRECT];
			if(inDirAddr != 0){
				setBitState(bitBuff, inDirAddr);
				char buf[BSIZE];
			  	rsect(inDirAddr, buf);
			  	uint *addrList;
			  	addrList = ((uint *)buf);
			  	for(int j = 0; j< NINDIRECT; j++){
			  		if(addrList[j]!=0){
			  			setBitState(bitBuff, addrList[j]);
					//printf("checking indirect set bit %u \n", getBitState(bitBuff, addrList[j]));
					}
				}
			}
		}
	}
	
	uint startIndex = BBLOCK(sBlock->size, sBlock->ninodes); 
	//printf("BIT MAP BLOCK %u \n", startIndex);
	
	for(i = startIndex+1; i<sBlock->size;i++){
		int bitMapBlock = BBLOCK(i, sBlock->ninodes); // will be 29 in standard case
		//printf("BIT MAP BLOCK %d \n", bitMapBlock);
		
		uchar bitMapReceivedBuffer[BSIZE];
		rsect(bitMapBlock, bitMapReceivedBuffer);
		
		uint receivedBitState = getBitState(bitMapReceivedBuffer, i);
		if(receivedBitState == 1){
			if(getBitState(bitBuff, i) != 1){
				//printf("BLOCK IS %u \n", i);
				printErrorAndExit(BITMAP_MARK_BLOCK_NOT_IN_USE);
			}
		}
	}
}

// For in-use inodes, each direct address in use is only used once. 
// If not, print ERROR: direct address used more than once.
void check7(){
	
	uchar bitBuff[(sBlock->size)/8]; // Need 1024 bits
	memset(bitBuff, 0, sizeof(bitBuff));
	
	uint i = 0;
	
	for( ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		if(in.type != 0){	
			uint *dAddress = in.addrs;
			// Checking Direct
			for (int j=0; j< NDIRECT; j++){
				uint cAddr = dAddress[j];
				if(cAddr != 0){ // TODO Need to check		; can also break ??
					if(getBitState(bitBuff, cAddr) == 1){
						printErrorAndExit(DIRECT_ADDR_USED_MORE_THAN_ONCE);
					}else{
						setBitState(bitBuff, cAddr);
					}
					//printf("checking direct set bit %u \n", getBitState(bitBuff, cAddr));
				}
			}
		}
	}
	
	for(i=0 ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		if(in.type != 0){	
			uint *dAddress = in.addrs;
			// Checking Indirect
			uint inDirAddr = dAddress[NDIRECT];
			if(inDirAddr != 0){
				if(getBitState(bitBuff, inDirAddr) == 1){
					printErrorAndExit(DIRECT_ADDR_USED_MORE_THAN_ONCE);
				}
				char buf[BSIZE];
			  	rsect(inDirAddr, buf);
			  	uint *addrList;
			  	addrList = ((uint *)buf);
			  	for(int j = 0; j< NINDIRECT; j++){
			  		if(addrList[j]!=0){
			  			if(getBitState(bitBuff, addrList[j]) == 1){
			  				printErrorAndExit(DIRECT_ADDR_USED_MORE_THAN_ONCE);
						  }
					//printf("checking indirect set bit %u \n", getBitState(bitBuff, addrList[j]));
					}
				}
			}
		}
	}
}

// For in-use inodes, each indirect address in use is only used once. 
// If not, print ERROR: indirect address used more than once.
void check8(){
	uchar bitBuff[(sBlock->size)/8]; // Need 1024 bits
	memset(bitBuff, 0, sizeof(bitBuff));
	
	uint i = 0;
	
	for( ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		if(in.type != 0){	
			uint *dAddress = in.addrs;
			// Checking Indirect
			uint inDirAddr = dAddress[NDIRECT];
			if(inDirAddr != 0){
				if(getBitState(bitBuff, inDirAddr) == 1){
					printErrorAndExit(INDIRECT_ADDR_USED_MORE_THAN_ONCE);
				}else{
					setBitState(bitBuff, inDirAddr); // Considering this to be indirect
				}
				char buf[BSIZE];
			  	rsect(inDirAddr, buf);
			  	uint *addrList;
			  	addrList = ((uint *)buf);
			  	for(int j = 0; j< NINDIRECT; j++){
			  		if(addrList[j]!=0){
			  			if(getBitState(bitBuff, addrList[j]) == 1){
			  				printErrorAndExit(INDIRECT_ADDR_USED_MORE_THAN_ONCE);
						  }else{
						  	setBitState(bitBuff, addrList[j]);
						  }
					//printf("checking indirect set bit %u \n", getBitState(bitBuff, addrList[j]));
					}
				}
			}
		}
	}
	
	
	for(i=0 ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		if(in.type != 0){	
			uint *dAddress = in.addrs;
			// Checking Direct
			for (int j=0; j< NDIRECT; j++){
				uint cAddr = dAddress[j];
				if(cAddr != 0){ // TODO Need to check		; can also break ??
					if(getBitState(bitBuff, cAddr) == 1){
						printErrorAndExit(INDIRECT_ADDR_USED_MORE_THAN_ONCE);
					}
					//printf("checking direct set bit %u \n", getBitState(bitBuff, cAddr));
				}
			}
		}
	}
}

void createDirentArray(int blockNum, struct dirent *dirContent){
   	char buf[BSIZE];
	rsect(blockNum, buf);
	dirContent = ((struct dirent *)buf);
}

// For all inodes marked in use, each must be referred to in at least one directory. 
// If not, print ERROR: inode marked use but not found in a directory.
void check9(){
	
	//printf("Here 0 \n");
	//fflush(stdout);
	uchar inUseInodebitMap[((sBlock->ninodes)/8)+1]; 
	memset(inUseInodebitMap, 0, sizeof(inUseInodebitMap));
	
	uint i = 0;
	
	for( ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		if(in.type != 0){
			printf("Inodes in use: %d\n",i);
			setBitState(inUseInodebitMap,i);
		}
	}
	//printf("Here 1\n");
	//fflush(stdout);
	uchar refInodebitMap[((sBlock->ninodes)/8)+1]; 
	memset(refInodebitMap, 0, sizeof(refInodebitMap));
	
	for(i=0 ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		//printf("Here 2\n");
		//fflush(stdout);
		if(in.type == T_DIR){	
			uint *dAddress = in.addrs;// Checking Direct
			for (int j=0; j< NDIRECT; j++){
				uint cAddr = dAddress[j];
				
				//printf("Here 3\n");
				//fflush(stdout);
				if(cAddr != 0){ 	
					struct dirent *dirContent = NULL;
					//printf("here 4 \n");
					//fflush(stdout);
					//*******
					
					char dirBuf[BSIZE];
					rsect(cAddr, dirBuf);
					dirContent = ((struct dirent *)dirBuf);
					//*******
					
					//createDirentArray(cAddr, dirContent);
					//printf("here 5 \n");
					//fflush(stdout);
					uint k = 0;
					for(; k< DPB; k++){
						
						//printf("here 6 \n");
						//fflush(stdout);
						//printf("Direct K %u , Name is %s \n", k, dirContent[k].name);
						
						//fflush(stdout);
						if(dirContent[k].inum != 0){ // Assuming unassigned dirent would be NULL
							printf("Inode referred: : %d \n",dirContent[k].inum);
							setBitState(refInodebitMap, dirContent[k].inum);
						}
					}
				}
			}
			
			
			// Checking Indirect
			uint inDirAddr = dAddress[NDIRECT];
			if(inDirAddr != 0){
				char buf[BSIZE];
			  	rsect(inDirAddr, buf);
			  	uint *addrList;
			  	addrList = ((uint *)buf);
			  	for(int j = 0; j< NINDIRECT; j++){
			  		if(addrList[j]!=0){
			  			struct dirent *dirContent = NULL;
						char dirBuf[BSIZE];
						rsect(addrList[j], dirBuf);
						dirContent = ((struct dirent *)dirBuf);
				  			
						//createDirentArray(addrList[j], dirContent);
						uint k = 0;
						for(; k< DPB; k++){
							if(dirContent[k].inum != 0){
								setBitState(refInodebitMap, dirContent[k].inum);
							}
						}
					}
				}
			}
		}
	}
	
	for(i=0 ;i < sBlock->ninodes; i++){
		if(getBitState(inUseInodebitMap,i) == 1){
			if(getBitState(refInodebitMap,i) !=1 ){
				printErrorAndExit(INODE_MARKED_USE_NOT_FOUND_IN_DIR);
			}
		}
	}
}


// For each inode number that is referred to in a valid directory, 
// it is actually marked in use. If not,
// print ERROR: inode referred to in directory but marked free.
void check10(){
	
	//printf("Here 0 \n");
	//fflush(stdout);
	uchar inUseInodebitMap[((sBlock->ninodes)/8)+1]; 
	memset(inUseInodebitMap, 0, sizeof(inUseInodebitMap));
	
	uint i = 0;
	
	for( ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		if(in.type != 0){
			setBitState(inUseInodebitMap,i);
		}
	}
	//printf("Here 1\n");
	//fflush(stdout);
	uchar refInodebitMap[((sBlock->ninodes)/8)+1]; 
	memset(refInodebitMap, 0, sizeof(refInodebitMap));
	
	for(i=0 ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		//printf("Here 2\n");
		//fflush(stdout);
		if(in.type == T_DIR){	
			uint *dAddress = in.addrs;// Checking Direct
			for (int j=0; j< NDIRECT; j++){
				uint cAddr = dAddress[j];
				
				//printf("Here 3\n");
				//fflush(stdout);
				if(cAddr != 0){ 	
					struct dirent *dirContent = NULL;
					//printf("here 4 \n");
					//fflush(stdout);
					//*******
					
					char dirBuf[BSIZE];
					rsect(cAddr, dirBuf);
					dirContent = ((struct dirent *)dirBuf);
					//*******
					
					//createDirentArray(cAddr, dirContent);
					//printf("here 5 \n");
					//fflush(stdout);
					uint k = 0;
					for(; k< DPB; k++){
						
						//printf("here 6 \n");
						//fflush(stdout);
						//printf("Direct K %u , Name is %s \n", k, dirContent[k].name);
						
						//fflush(stdout);
						if(dirContent[k].inum != 0){ // Assuming unassigned dirent would be NULL
							setBitState(refInodebitMap, dirContent[k].inum);
						}
					}
				}
			}
			
			
			// Checking Indirect
			uint inDirAddr = dAddress[NDIRECT];
			if(inDirAddr != 0){
				char buf[BSIZE];
			  	rsect(inDirAddr, buf);
			  	uint *addrList;
			  	addrList = ((uint *)buf);
			  	for(int j = 0; j< NINDIRECT; j++){
			  		if(addrList[j]!=0){
			  			struct dirent *dirContent = NULL;
						char dirBuf[BSIZE];
						rsect(addrList[j], dirBuf);
						dirContent = ((struct dirent *)dirBuf);
				  			
						//createDirentArray(addrList[j], dirContent);
						uint k = 0;
						for(; k< DPB; k++){
							if(dirContent[k].inum != 0){
								setBitState(refInodebitMap, dirContent[k].inum);
							}
						}
					}
				}
			}
		}
	}
	
	for(i=0 ;i < sBlock->ninodes; i++){
		if(getBitState(refInodebitMap,i) == 1){
			if(getBitState(inUseInodebitMap,i) != 1){
				printErrorAndExit(INODE_REFFERED_BUT_MARKED_FREE);
			}
		}
	}
	
}

// Reference counts (number of links) for regular files match the number 
// of times file is referred to in directories (i.e., hard links work correctly).
// If not, print ERROR: bad reference count for file.
void check11(){
	
	short linkCount[sBlock->ninodes];
	short refCount[sBlock->ninodes];
	uint i; 
	for(i=0; i<sBlock->ninodes; i++){
		linkCount[i] = 0;
		refCount[i] = 0;
	}
	
	i = 0;
	
	for( ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		if(in.type == T_FILE){
			linkCount[i] = in.nlink;
		}
	}
	
	for(i=0 ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		
		if(in.type == T_DIR){	
			uint *dAddress = in.addrs;// Checking Direct
			for (int j=0; j< NDIRECT; j++){
				uint cAddr = dAddress[j];
				
				if(cAddr != 0){ 	
					struct dirent *dirContent = NULL;
					
					char dirBuf[BSIZE];
					rsect(cAddr, dirBuf);
					dirContent = ((struct dirent *)dirBuf);
					
					uint k = 0;
					for(; k< DPB; k++){
						if(dirContent[k].inum != 0){ // Assuming unassigned dirent would be NULL
							struct dinode subInode;
							rinode(dirContent[k].inum, &subInode);
							if(subInode.type == T_FILE){
								refCount[dirContent[k].inum]++;
							}
						}
					}
				}
			}
			
			
			// Checking Indirect
			uint inDirAddr = dAddress[NDIRECT];
			if(inDirAddr != 0){
				char buf[BSIZE];
			  	rsect(inDirAddr, buf);
			  	uint *addrList;
			  	addrList = ((uint *)buf);
			  	for(int j = 0; j< NINDIRECT; j++){
			  		if(addrList[j]!=0){
			  			struct dirent *dirContent = NULL;
						char dirBuf[BSIZE];
						rsect(addrList[j], dirBuf);
						dirContent = ((struct dirent *)dirBuf);
				  			
						//createDirentArray(addrList[j], dirContent);
						uint k = 0;
						for(; k< DPB; k++){
							if(dirContent[k].inum != 0){
								struct dinode subInode;
								rinode(dirContent[k].inum, &subInode);
								if(subInode.type == T_FILE){
									refCount[dirContent[k].inum]++;
								}
							}
						}
					}
				}
			}
		}
	}
	
	for(i=0 ;i < sBlock->ninodes; i++){
		if(linkCount[i] != refCount[i]){
			printErrorAndExit(BAD_REF_COUNT_FOR_FILE);
		}
	}
	
}

// No extra links allowed for directories (each directory only appears
// in one other directory). 
// If not, print ERROR: directory appears more than once in file system.
void check12(){
	short refCount[sBlock->ninodes];
	uint i; 
	for(i=0; i<sBlock->ninodes; i++){
		refCount[i] = 0;
	}
	
	i = 0;
	
	
	for(i=0 ;i < sBlock->ninodes; i++){
		struct dinode in;
		rinode(i, &in);
		
		if(in.type == T_DIR){	
			uint *dAddress = in.addrs;// Checking Direct
			for (int j=0; j< NDIRECT; j++){
				uint cAddr = dAddress[j];
				
				if(cAddr != 0){ 	
					struct dirent *dirContent = NULL;
					
					char dirBuf[BSIZE];
					rsect(cAddr, dirBuf);
					dirContent = ((struct dirent *)dirBuf);
					
					uint k = 0;
					if(j == 0){
						k = 2;
					}
					for(; k< DPB; k++){
						if(dirContent[k].inum != 0){ // Assuming unassigned dirent would be NULL
							struct dinode subInode;
							rinode(dirContent[k].inum, &subInode);
							if(subInode.type == T_DIR){
								refCount[dirContent[k].inum]++;
							}
						}
					}
				}
			}
			
			
			// Checking Indirect
			uint inDirAddr = dAddress[NDIRECT];
			if(inDirAddr != 0){
				char buf[BSIZE];
			  	rsect(inDirAddr, buf);
			  	uint *addrList;
			  	addrList = ((uint *)buf);
			  	for(int j = 0; j< NINDIRECT; j++){
			  		if(addrList[j]!=0){
			  			struct dirent *dirContent = NULL;
						char dirBuf[BSIZE];
						rsect(addrList[j], dirBuf);
						dirContent = ((struct dirent *)dirBuf);
				  			
						//createDirentArray(addrList[j], dirContent);
						uint k = 0;
						for(; k< DPB; k++){
							if(dirContent[k].inum != 0){
								struct dinode subInode;
								rinode(dirContent[k].inum, &subInode);
								if(subInode.type == T_DIR){
									refCount[dirContent[k].inum]++;
								}
							}
						}
					}
				}
			}
		}
	}
	
	if(refCount[ROOTINO] != 0){
		//printf("ROOT");
		printErrorAndExit(DIR_APPEAR_MORE_THAN_ONCE);
	}
	for(i=2 ;i < sBlock->ninodes; i++){	
		struct dinode subInode;
		rinode(i, &subInode);
		if(subInode.type == T_DIR){
			if(refCount[i] != 1){
				printErrorAndExit(DIR_APPEAR_MORE_THAN_ONCE);
			}
		}
	}
}

uint
i2b(uint inum)
{
  return (inum / IPB) + 2;
}

void printErrorAndExit(const char *message){
	fprintf( stderr, "%s\n", message);
	cleanUp();
	exit(1);
}

void
rsect(uint sec, void *buf)
{
  if(lseek(fsfd, sec * 512L, 0) != sec * 512L){
    perror("lseek");
    exit(1);
  }
  if(read(fsfd, buf, 512) != 512){
    perror("read");
    exit(1);
  }
}

void readSuperBlock(){
  	
  	char buf[BSIZE];
  	rsect(1, buf);
  	struct superblock *sb;
  	sb = ((struct superblock*)buf);
  	
	sBlock = (struct superblock *)malloc(sizeof(struct superblock));
  	sBlock->size = sb->size;
  	sBlock->nblocks = sb->nblocks;
  	sBlock->ninodes = sb->ninodes;
  	//printf("Size is %u, nblocks is %u , ninodes is %u \n", sBlock->size, sBlock->nblocks, sBlock->ninodes);
  	
}

void
rinode(uint inum, struct dinode *ip)
{
  char buf[512];
  uint bn;
  struct dinode *dip;

  bn = i2b(inum);
  rsect(bn, buf);
  dip = ((struct dinode*)buf) + (inum % IPB);
  *ip = *dip;
}


int main(int argc, char *argv[]){
	if(argc < 2){
		printErrorAndExit(IMAGE_NOT_FOUND);
	}else{
		/* Open the file for reading. */
    	fsfd = open (argv[1], O_RDONLY);
    	if(fsfd < 0){
			printErrorAndExit(IMAGE_NOT_FOUND);
		}
		
		readSuperBlock();
		
		if(sBlock == NULL)
			exit(1);
			
		check1(); 
		check2();
		check3();
		check4();
		check5();
		check6();
		check7();
		check8();
		check9();
		check10();
		check11();
		check12();
		
		cleanUp();
	}
	return 0;
}
