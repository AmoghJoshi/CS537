#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/sysinfo.h>
#include <pthread.h>
#include "mapreduce.h"

#define Pthread_create(thread, attr, start_routine, arg)		assert(pthread_create(thread, attr, start_routine, arg) == 0);
#define Pthread_join(thread, value_ptr)        					assert(pthread_join(thread, value_ptr) == 0);
#define Pthread_mutex_lock(m)                  					assert(pthread_mutex_lock(m) == 0);
#define Pthread_mutex_unlock(m)                					assert(pthread_mutex_unlock(m) == 0);
#define Pthread_cond_wait(c,m)                 					assert(pthread_cond_wait(c,m) == 0);
#define Pthread_cond_signal(c)                 					assert(pthread_cond_signal(c) == 0);


#define DEFAULT_BUCKET_SIZE 100
pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;

Mapper Map();
Reducer Reduce();

typedef struct node {
    char* value;
    struct node * next;
} node_t;

typedef struct __list_t {
	char* key;
	node_t *head;
	pthread_mutex_t lock;
} list_t;

typedef struct __hash_t {
	pthread_mutex_t ht_lock;
	int ht_size;
	list_t lists[200000];
	node_t* curr;
} hash_t;

hash_t * ht[DEFAULT_BUCKET_SIZE];
void List_Init(list_t *L) {
	L->key = NULL;
	L->head = NULL;
	pthread_mutex_init(&L->lock, NULL);
}

int List_Insert(list_t *L, char* key, char* value, int partition) {

	node_t *new = malloc(sizeof(node_t));
	if (new == NULL) {
		perror("Unable to allocate memory. Malloc failed\n");
//		pthread_mutex_unlock(&L->lock);
		return -1; // fail
	}
//	pthread_mutex_lock(&L->lock);

	if(L->head == NULL){
		L->key = strdup(key);                 // insert key in the list only once.
	}

	new->value = strdup(value);
	new->next = L->head;
	L->head = new;
//	pthread_mutex_unlock(&L->lock);
	return 0; // success
}

int List_Lookup(list_t *L, char* key, char* value, int partition) {

	pthread_mutex_lock(&L->lock);
	int rv = -1;
	if((*L).key == NULL || strcmp((*L).key, key) == 0 ){
		rv =  1;
		List_Insert(L, key, value, partition);

	}
	pthread_mutex_unlock(&L->lock);
	return rv;
}

//**************************************************************
int HASHTABLE_SIZE = 200000;

void Hash_Init(hash_t *H, int size) {
	int i;
	(*H).ht_size = size;
	pthread_mutex_init(&H->ht_lock, NULL);
	for (i = 0; i < (*H).ht_size; i++) {
		List_Init(&H->lists[i]);
	}
}


unsigned long hash(unsigned char *key, hash_t * H){
        unsigned char* str = (unsigned char *)key;
        unsigned long hash = 0;
        int c;
        while ((c = *str++))
        	hash = c + (hash << 6) + (hash << 16) - hash;
        return hash % (*H).ht_size;
}

//unsigned long hash(unsigned char *str, hash_t * H){
//	unsigned long hash = 5381;
//	int c;
//	while ((c = *str++)){
//		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
//	}
//	return hash % (unsigned long )(*H).ht_size;
//}
/*
unsigned long hash(unsigned char *key, hash_t *H) {
  unsigned long h = 0;
  for (int i = 0; i < strlen((const char *)key); i++) {
    h = 31 * h + key[i];
  }
  return h% (*H).ht_size;
}
*/

int Hash_Insert(hash_t *H, char* key, char* value, int partition) {

	int bucket = hash((unsigned char *)key, H);
	int b = bucket;
//	pthread_mutex_lock(&(&H->lists[b])->lock);
	while(1){

		if(List_Lookup(&H->lists[b],key, value, partition) == 1){
//			int ret = List_Insert((&H->lists[b]), key, value, partition);
//			pthread_mutex_unlock(&(&H->lists[b])->lock);
			return 1;
		}
		else{
//			if ((*H).ht_size == 0)printf("ht size: %d \n", (*H).ht_size);
			b = (b +1) % (*H).ht_size;
			if(b == bucket){
				printf("Hashtable is full\n");
//				pthread_mutex_unlock(&(&H->lists[b])->lock);
				return -1;
			}
		}
	}
}

//int Hash_Lookup(hash_t *H, char *key, char * value) {
//	int bucket = hash((unsigned char *)key, H);
//	return List_Lookup(&H->lists[bucket], key);
//}

//*************************************************************

int num_partitions;
int rem_files;
int global_argc;
char **global_argv;
int current_file_index;
int current_partition;
int num_mappers;
int num_reducers;

Partitioner global_partitioner;


unsigned long MR_DefaultHashPartition(char *key, int num_partitions) {
    unsigned long hash = 5381;
    int c;
    while ((c = *key++) != '\0')
        hash = hash * 33 + c;
    return hash % num_partitions;
}

char * get_next (char *key, int partition_number){
    node_t* current = ht[partition_number]->curr;
    if(current==NULL){
    	return NULL;
    }
    char* val = current->value;
    ht[partition_number]->curr = current->next;
	return val;
}

int compare1(const void *elem1, const void *elem2){
	list_t * a = (list_t *)elem1;
	list_t * b = (list_t *)elem2;
	if( (*a).key == NULL &&  (*b).key == NULL) return 0;
	if ((*a).key == NULL) return 1;
	if ((*b).key == NULL) return -1;
	return strcmp((*a).key,(*b).key);
}

void *MapWrapper(void * arg){
	while(1){
	    Pthread_mutex_lock(&m);
		if(current_file_index > global_argc -1 ){
		    Pthread_mutex_unlock(&m);
			return 0;
		}
		char* file_name = global_argv[current_file_index];
		current_file_index++;
	    Pthread_mutex_unlock(&m);
		(Map)(file_name);
	}
}

void * ReduceWrapper(void *arg){
	Pthread_mutex_lock(&m);
	if(current_partition >= num_partitions){
		Pthread_mutex_unlock(&m);
		return 0;
	}
	qsort(ht[current_partition]->lists, (*ht[current_partition]).ht_size, sizeof(list_t), &compare1);
	int my_partition = current_partition;
	current_partition++;
	Pthread_mutex_unlock(&m);
	hash_t *current_ht = ht[my_partition];
	for(int i =0;i<(*current_ht).ht_size;i++){
		list_t current_list = current_ht->lists[i];
		if(current_list.key == NULL) break;
		current_ht->curr = current_list.head;
		Reduce(current_list.key, get_next, my_partition);
	}
	return 0;
}



void freemem(){
	for(int i =0;i<num_partitions;i++){
		hash_t * curr_ht = ht[i];
		for(int j =0;j<(*ht[i]).ht_size;j++){
			list_t curr_list = curr_ht->lists[j];
			free(curr_list.key);
			node_t * curr_head = curr_list.head;
			while(curr_head != NULL){
				node_t * temp = curr_head;
				free(temp->value);
				curr_head = curr_head->next;
				free(temp);
			}
		}
		free(ht[i]);
	}
}

void MR_Run(int argc, char *argv[],Mapper Map, int mapnum, Reducer Reduce,
  int reducenum, Partitioner partitioner){


	current_partition =0;
	num_mappers = mapnum;
	num_reducers = reducenum;
	num_partitions = num_reducers;

	for(int i =0;i<num_partitions;i++){
		ht[i] = malloc(sizeof(hash_t));
		Hash_Init(ht[i], 200000);
	}
	current_file_index = 1;
	global_argc = argc;
	global_argv = argv;
	global_partitioner = partitioner;

//**************    Mapper stage ********************
	//Create consumer threads
  pthread_t *mapper_threads = (pthread_t *)malloc(num_mappers * sizeof(pthread_t));
  for(int i = 0; i <  num_mappers; i++){
	  Pthread_create(&mapper_threads[i], NULL, MapWrapper, NULL);
  }

  for(int i = 0; i <  num_mappers; i++){
//	  printf("Waiting for mapper threads\n");
 	Pthread_join(mapper_threads[i], NULL);
  }

// ************** Explicit sort *****************************
//    for(int i =0;i<num_partitions;i++){
//  	  qsort(ht[i]->lists, (*ht[i]).ht_size, sizeof(list_t), &compare1);
//    }

  //

// ************** Reducer stage    ***********************************
  pthread_t *reducer_threads = (pthread_t *)malloc(num_reducers * sizeof(pthread_t));
  for(int i = 0; i <  num_reducers; i++){
  	  Pthread_create(&reducer_threads[i], NULL, ReduceWrapper, NULL);
  }

  for(int i = 0; i <  num_reducers; i++){
//  	  printf("Waiting for reducer threads\n");
  	  Pthread_join(reducer_threads[i], NULL);
  }

// ***************** Free malloc'd memory ****************************
  free(mapper_threads);
  free(reducer_threads);
  freemem();

}


void MR_Emit(char *key, char *value){
	unsigned long bucket_index = global_partitioner(key, num_partitions);
	Hash_Insert(ht[bucket_index], key, value, bucket_index);

}

