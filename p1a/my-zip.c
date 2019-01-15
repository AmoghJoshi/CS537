#include<stdio.h>
#include<stdlib.h>
#include<string.h>
int main(int argc, char *argv[]){
  int numargs = 1, lastCount;
  char lastChar;
  size_t writecount = sizeof(int), writechar = sizeof(char);
  if(argc ==1){
    printf("my-zip: file1 [file2 ...]\n");
    return 1;
  }
    while(numargs < argc){
      FILE *fp = fopen(argv[numargs++],"r"); 
      if(fp == NULL){
        printf("my-zip: cannot open file\n");
        return 1;  
      }
      size_t length;
      char* line;
      int count, i, isFirstChar = 1; 
      while(getline(&line,&length,fp) != -1){
        if(lastChar == line[0]) count++;
        else{
          if(isFirstChar !=1){
            fwrite(&count,writecount,1,stdout);
            fwrite(&lastChar,writechar,1,stdout);
          }
          count = 1;
        }
        isFirstChar =0;
        for(i=1; line[i] != '\0' ;i++){
          if(line[i] == line[i-1]) count++;
          else{
            fwrite(&count,writecount,1,stdout);
            fwrite(&line[i-1],writechar,1,stdout);
            count =1;
          } 
        }
        lastChar = line[i-1];
        lastCount = count;
      }
    } 
      fwrite(&lastCount,writecount,1,stdout);
      fwrite(&lastChar,writechar,1,stdout);
      return 0;
 }