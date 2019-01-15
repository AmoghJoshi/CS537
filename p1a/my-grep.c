#include<stdio.h>
#include<stdlib.h>
#include<string.h>
int main(int argc, char *argv[]){
  int BUFFER_SIZE = 512;
  if(argc < 2){
   printf("my-grep: searchterm [file ...]\n");
   return 1;
  }
   
  char *searchWord;
  searchWord = argv[1];
  char * line = malloc(BUFFER_SIZE * sizeof(char));
  size_t length = 0;
  //printf("%d",argc);
  if(argc ==2){
    // read from stdin
    while(getline(&line,&length,stdin)!= -1){
      if(strstr(line, searchWord) != NULL){
        printf("%s",line);
        } 
    }
    return 0;
  }
  else {
    int count = 2;
    ssize_t read;
    while(count < argc){
      FILE *fp = fopen(argv[count++],"r"); 
      if(fp == NULL){
        printf("my-grep: cannot open file\n");
        exit(1);  
      }
      if(fp!= NULL){
        while((read = getline(&line,&length,fp)) != -1){
          if(strstr(line, searchWord) != NULL){
            printf("%s",line);
          }
        } 
      }
      fclose(fp);  
    }
  }
  free(line);
  return 0;
}