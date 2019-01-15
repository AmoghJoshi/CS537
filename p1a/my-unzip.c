#include<stdio.h>
#include<stdlib.h>
#include<string.h>
int main(int argc, char *argv[]){
  int numargs = 1;
  if(argc ==1){
    printf("my-unzip: file1 [file2 ...]\n");
    return 1;
  }
  while(numargs < argc){
    FILE *fp = fopen(argv[numargs++],"r"); 
    if(fp == NULL){
      printf("my-unzip: cannot open file\n");
      return 1;  
    }
    char letter;
    int count;
    while(1){
      if(fread(&count, sizeof(int),1, fp) != 1) break;
      if(fread(&letter,sizeof(char) ,1, fp)!= 1) break;
      for(int k = count;k>0;k--) printf("%c", letter);
    }
    fclose(fp);
  }
    return 0;
 }
 
 