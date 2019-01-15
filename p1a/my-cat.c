#include<stdio.h>
#include<stdlib.h>
int main(int argc, char *argv[]){
  int BUFFER_SIZE = 512;
  int count =1;
  char buffer[BUFFER_SIZE];
  while(count< argc){
    FILE *fp = fopen(argv[count++],"r"); 
    if(fp == NULL){
      printf("my-cat: cannot open file\n");
      exit(1);  
    }
    if(fp!= NULL){
      while(fgets(buffer, BUFFER_SIZE,fp) != NULL)
        printf("%s",buffer);  
    }
    fclose(fp);
  }
  
  
  return 0;

}
