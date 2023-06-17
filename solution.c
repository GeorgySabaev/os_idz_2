#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

struct letter {
  int wealth;
  char name[50];
  char reply[20];
};

struct shmem {
  sem_t semaphore;
  int letters_sent;
  int replies_sent;
  int replies_read;
  struct letter letters[0];
};

void student_process(struct shmem *shared_memory, int index) {
  // create name table
  char **names = (char **) malloc(sizeof(char *) * 4);
  names[0] = "John ";
  names[1] = "Jake ";
  names[2] = "Dirk ";
  names[3] = "Dave ";
  char **surnames = (char **) malloc(sizeof(char *) * 3);
  surnames[0] = "Egbert";
  surnames[1] = "English";
  surnames[2] = "Strider";

  // write a letter
  sem_wait(&shared_memory->semaphore);
  srand(index * 1024);
  printf("[STUDENT %d] Writing letter:\n", index);
  shared_memory->letters[index].wealth = rand() % 1000;
  strncpy(shared_memory->letters[index].name, names[rand() % 4], 25);
  strncat(shared_memory->letters[index].name, surnames[rand() % 3], 25);
  ++(shared_memory->letters_sent);
  printf("|Name: %s\n", shared_memory->letters[index].name);
  printf("|Wealth: %d\n", shared_memory->letters[index].wealth);
  sem_post(&shared_memory->semaphore);
  printf("[STUDENT %d] Waiting for reply...\n", index);

  free(names);
  free(surnames);

  // wait until replies are sent
  sem_wait(&shared_memory->semaphore);
  while (1 != shared_memory->replies_sent) {
    sem_post(&shared_memory->semaphore);
    usleep(10);
    sem_wait(&shared_memory->semaphore);
  }
  printf("[STUDENT %d] Reply received.\n", index);

  // read the reply
  printf("Student %d received: %s\n", index, shared_memory->letters[index].reply);
  ++(shared_memory->replies_read);
  sem_post(&shared_memory->semaphore);
}

void lady_process(struct shmem *shared_memory, int n) {
  printf("[LADY] Waiting for letters...\n");

  // wait until letters are sent
  sem_wait(&shared_memory->semaphore);
  while (n != shared_memory->letters_sent) {
    sem_post(&shared_memory->semaphore);
    usleep(10);
    sem_wait(&shared_memory->semaphore);
  }
  printf("[LADY] All letters received.\n");
  printf("[LADY] Processing letters...\n");

  // check all letters
  int max_i = 0;
  int max_wealth = 0;
  for (size_t i = 0; i < n; i++) {
    if (max_wealth < shared_memory->letters[i].wealth) {
      max_i = i;
      max_wealth = shared_memory->letters[i].wealth;
    }
  }
  printf("[LADY] Student %d (%s) chosen.\n", max_i, shared_memory->letters[max_i].name);
  printf("[LADY] Sending replies...\n");

  // send replies
  for (size_t i = 0; i < n; i++) {
    if (i == max_i) {
      strncpy(shared_memory->letters[i].reply, "OF COURSE", 20);
    } else {
      strncpy(shared_memory->letters[i].reply, "NOT TODAY", 20);
    }
  }
  shared_memory->replies_sent = 1;
  sem_post(&shared_memory->semaphore);
  printf("[LADY] Replies sent.\n");

  // wait until all student processes finish
  sem_wait(&shared_memory->semaphore);
  while (n != shared_memory->replies_read) {
    sem_post(&shared_memory->semaphore);
    usleep(10);
    sem_wait(&shared_memory->semaphore);
  }
  sem_post(&shared_memory->semaphore);
}

int main(int argc, char *argv[]) {
  // process command line arguments
  if (argc != 2) {
    printf("Usage: %s <N>\n", argv[0]);
    return 0;
  }

  int n = atoi(argv[1]);

  // make shared memory
  int shmemsize = sizeof(struct shmem) + n * sizeof(struct letter);
  char shm_name_lsem[] = "shared_memory_idz_2";
  int shm_id_lsem = shm_open(shm_name_lsem, O_CREAT | O_RDWR, S_IRWXU | S_IRWXG);
  if (shm_id_lsem < 0) {
    perror("During shared memory initialisation:");
    exit(1);
  }
  ftruncate(shm_id_lsem, shmemsize);

  // attach shared memory
  struct shmem *shared_memory = (struct shmem *) mmap(0, shmemsize, PROT_WRITE | PROT_READ, MAP_SHARED, shm_id_lsem, 0);

  // initialise counters
  shared_memory->letters_sent = 0;
  shared_memory->replies_read = 0;
  shared_memory->replies_sent = 0;

  // initialise semaphor
  sem_init(&shared_memory->semaphore, 1, 1);

  // split into multiple processes
  for (int i = 0; i < n; ++i) {
    if (0 == fork()) {
      // run student code
      student_process(shared_memory, i);

      // detach shared memory
      munmap(shared_memory, shmemsize);
      return 0;
    }
  }
  lady_process(shared_memory, n);
  // detach and delete shared memory
  munmap(shared_memory, shmemsize);
  shmctl(shm_id_lsem, IPC_RMID, NULL);
}