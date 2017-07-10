#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>
#include <limits.h>
#include <malloc.h>
#include <errno.h>

#define NOTES_NUM 0x100
#define REQUEST_LEN 0x400
#define MAX_THREADS 0x6
#define SIZE_MASK 0xffff
#define MALLOC_HOOK 0x0
#define FREE_HOOK 0x0

void *initial_memalign_hook;
void *initial_realloc_hook;
sem_t thread_creation;
struct notepad notes;


struct notepad {
 unsigned long nxt_note;
 char *text[NOTES_NUM];
 unsigned long size_hash[NOTES_NUM];
};
struct request {
 char content[REQUEST_LEN];
 long id;
};
struct command {
 char id;
 unsigned long arg1;
 char *arg2;
};
void check_hooks(); // Maybe there's a way to trick this... but this is not intended
int parse_command(char *, struct command *);
unsigned long calc_hash(char *); // hash is actually a lie... it's not really deterministic...
unsigned long exponentiate(unsigned long, unsigned long);


// This function does the job of this awesome powerful notepad.
void *thread_func(void *request) {
 struct request *req = (struct request *)request;
 struct command cmd;
 long id = req->id, i;
 char input[REQUEST_LEN] = { 0 };
 strcpy(input, req->content);

 if(sem_post(&thread_creation)) {
  return 0;
 }
 printf("\033[31mThread %ld\033[0m #STARTED\n", id);

 check_hooks();

 if(!parse_command(input, &cmd)) {

  printf("\033[31mThread %ld\033[0m #command = %c\n", id, cmd.id);

  if(cmd.id == 'a') {
   if(cmd.arg1 > 0 && cmd.arg1 < REQUEST_LEN) {
    unsigned long allowed_len_with_null = cmd.arg1;
    if(strlen(cmd.arg2) < allowed_len_with_null) {
     unsigned long new_hash = calc_hash(cmd.arg2);
     int cur_note = notes.nxt_note;
     notes.nxt_note = (notes.nxt_note + 1) % NOTES_NUM;
     if(notes.size_hash[cur_note] != 0) {
       free(notes.text[cur_note]);
       notes.size_hash[cur_note] = 0;
     }
     notes.text[cur_note] = malloc(allowed_len_with_null);
     strcpy(notes.text[cur_note], cmd.arg2);
     notes.size_hash[cur_note] = (new_hash & ~SIZE_MASK) + (unsigned long)allowed_len_with_null;
    }
   }
  }
  else if(cmd.id == 'e') {
   for(i = (notes.nxt_note+1)%NOTES_NUM; i != notes.nxt_note; i = (i+1)%NOTES_NUM) {
    if(notes.size_hash[i] != 0 && notes.size_hash[i] == cmd.arg1) {
     unsigned long allowed_len_with_null = (notes.size_hash[i] & SIZE_MASK);
     if(strlen(cmd.arg2) < allowed_len_with_null) {
      unsigned long new_hash = calc_hash(cmd.arg2);
      strcpy(notes.text[i], cmd.arg2);
      notes.size_hash[i] = (new_hash & ~SIZE_MASK) + allowed_len_with_null;
     }
     break;
    }
   }
  }
  else if(cmd.id == 'd') {
   for(i = (notes.nxt_note+1)%NOTES_NUM; i != notes.nxt_note; i = (i+1)%NOTES_NUM) {
    if(notes.size_hash[i] != 0 && notes.size_hash[i] == cmd.arg1) {
     free(notes.text[i]);
     notes.size_hash[i] = 0;
     break;
    }
   }
  }
  else if(cmd.id == 'p') {
   for(i = (notes.nxt_note+1)%NOTES_NUM; i != notes.nxt_note; i = (i+1)%NOTES_NUM) {
    if(notes.size_hash[i] != 0) {
     printf("\033[31mThread %ld\033[0m #0x%016lx: %s\n", id, notes.size_hash[i], notes.text[i]);
    }
   }
  }
  else if(cmd.id == 'q') {
   exit(0);
  }
 }

 check_hooks();

 printf("\033[31mThread %ld\033[0m #FINISHED\n", id);
 return 0;
}


// No intended vulnerability in the functions declared below

int main(int argc, char *argv[]) {
 long thread_counter = 0;
 pthread_t threads[MAX_THREADS];
 void *(*routine)(void*) = &thread_func;
 struct request req;

 notes.nxt_note = 0;
 initial_memalign_hook = __memalign_hook;
 initial_realloc_hook = __realloc_hook;
 memset(threads, 0x00, sizeof(threads));

 if(sem_init(&thread_creation, 0, 1)) {
  return -1;
 }

 setbuf(stdout, NULL);

 printf("\033[36mWelcome to our note making service ");
 printf("with multithreading for high performance note taking!\033[0m\n");

 while(1) {
  if(sem_wait(&thread_creation)) {
   return -1;
  }

  pthread_join(threads[thread_counter], NULL);

  printf("\033[32m[a]dd | [e]dit | [d]elete | [p]rint | [q]uit $ \033[0m");

  if(!fgets(req.content, sizeof(req.content), stdin)) {
   return -1;
  }
  if(strlen(req.content) >= 1 && req.content[strlen(req.content) - 1] == '\n') {
   req.content[strlen(req.content) - 1] = 0;
  }
  req.id = thread_counter;
  if(pthread_create(&threads[thread_counter], NULL, routine, (void *)&req)) {
   return -1;
  }
  thread_counter = (thread_counter+1)%MAX_THREADS;
 }

 return 0;
}


void check_hooks() {
 if(!(MALLOC_HOOK == __malloc_hook && FREE_HOOK == __free_hook &&
      initial_realloc_hook == __realloc_hook && initial_memalign_hook == __memalign_hook)) {
  printf("\033[30m\033[41mO.o\033[0m\n");
  exit(-1);
 }
 return;
}


int parse_command(char *input, struct command *dest) {
 char id = input[0], *parsing_end;
 unsigned long parsed, text_offset;
 if(id != 'a' && id != 'e' && id !='d' && id != 'p' && id != 'q') {
  return -1;
 }
 if(id == 'a' || id == 'e' || id == 'd') {
  if(strlen(&input[1]) <= 0) {
   return -1;
  }
  parsed = strtoul(&input[1], &parsing_end, 0);
  if(id == 'a' || id == 'e') {
   text_offset = (unsigned long)parsing_end - (unsigned long)input + 1;
   if(input[text_offset - 1] != ' ' || strlen(input + text_offset) <= 0) {
    return - 1;
   }
   dest->arg2 = input + text_offset;
  }
  dest->arg1 = parsed;
 }
 dest->id = id;
 return 0;
}


unsigned long calc_hash(char *text) {
 struct timeval tv;
 unsigned long ms_epoch, hash = 99907;
 int i = strlen(text);
 for(; i >= 0; i--) {
  hash += exponentiate((unsigned long)text[i], 499);
 }
 // We wanted to use 599999983 instead of 83... however we want to mitigate DoS attacks
 hash = hash ^ exponentiate(hash, 83);
 // Let's simulate the 599999983:
 sleep(1);
 gettimeofday(&tv, NULL);
 ms_epoch = tv.tv_sec*1000 + tv.tv_usec/1000; 
 return hash ^ (ms_epoch<<16);
}


unsigned long exponentiate(unsigned long a, unsigned long b) {
 unsigned long i;
 unsigned long result = 1;
 for(i = 0; i < b; i++) {
  result *= a;
 }
 return result;
}

