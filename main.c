#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <syscall.h>
#include <math.h>

#define ARG_NUMB 4

char *UTIL_NAME = NULL;

typedef enum {
    ST_NULL,
    ST_BUSY,
    ST_FREE
} thread_status_t;

typedef struct {
    thread_status_t *thread_status;
    int member_index;
    int taylors_set_member_index;
    FILE* file;
} thread_params_t;

pthread_t *THREADS;
int NUMBER_OF_MEMBERS;
int TAYLOR_SET_LENGTH;
thread_status_t *THREADS_STATUS;

void count_member(void*);
int wait_for_thread(int);
void print_err(const char*, const char*);
double get_x(double);
FILE *get_tmp_file();
void print_taylor_set_elements(FILE*);
FILE *get_result_file(const char*);
void print_result(FILE*, FILE*);

void print_err(const char *error_msg, const char *file_name) {
    if (file_name != NULL) {
        fprintf(stderr, "%s: %s\n", UTIL_NAME, error_msg);
    } else {
        fprintf(stderr, "%s: %s: %s\n", UTIL_NAME, file_name, error_msg);
    }
    exit(-1);
}

double get_x(double index) {
    double x =  2 * M_PI * index / NUMBER_OF_MEMBERS;
    if (x != 0) {
        x = M_PI - x;
    }
    return x;
}

FILE *get_tmp_file() {
    const char *tmp_fname = "/tmp/tmp.txt";
    FILE *tmp_file;
    if ((tmp_file = fopen(tmp_fname, "w+r")) == NULL) {
        print_err(strerror(errno), tmp_fname);
    }
    return tmp_file;
}

void count_member(void *arg) {
    thread_params_t *thread_params = (thread_params_t *) arg;
    int i, j, k;
    double x, set_element;

    i = thread_params->member_index;
    j = thread_params->taylors_set_member_index;
    FILE* tmp_file = thread_params->file;

    x = get_x(i);
    set_element = (j % 2) ? -1 : 1;
    for (k = 1; k <= 2 * j + 1; set_element *= x/k++);
    printf("%ld %d %f\n", syscall(SYS_gettid), i, set_element);
    fprintf(tmp_file, "%ld %d %f\n", syscall(SYS_gettid), i, set_element);

    *(thread_params->thread_status) = ST_FREE;

    free(thread_params);
    return;
}

void print_taylor_set_elements(FILE* tmp_file) {

    int i, j;
    thread_params_t *thread_params;
    pthread_t thread;
    pthread_attr_t pthread_attr;
    pthread_attr_init(&pthread_attr);
    pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_JOINABLE);

    for (i = 0; i < NUMBER_OF_MEMBERS; i++) {
        for (j = 0; j < TAYLOR_SET_LENGTH; j++) {

            int thread_id = wait_for_thread(TAYLOR_SET_LENGTH);

            if (THREADS_STATUS[thread_id] == ST_FREE) {
                if (pthread_join(THREADS[thread_id], NULL) == -1){
                    print_err(strerror(errno), NULL);
                };
            }

            THREADS_STATUS[thread_id] = ST_BUSY;

            thread_params = malloc(sizeof(thread_params_t));
            thread_params->thread_status = &(THREADS_STATUS[thread_id]);
            thread_params->member_index = i;
            thread_params->taylors_set_member_index = j;
            thread_params->file = tmp_file;

            if (pthread_create(&THREADS[thread_id], &pthread_attr, &count_member, thread_params) == -1) {
                print_err(strerror(errno), NULL);
                return;
            };
        }
    }

}

FILE *get_result_file(const char *arg_param) {
    char res_fname[FILENAME_MAX];
    realpath(arg_param, res_fname);
    FILE *res_file;

    if ((res_file = fopen(res_fname, "w+")) == NULL) {
        print_err(strerror(errno), res_fname);
    }
    return res_file;
}

void print_result(FILE* tmp_file, FILE* result_file) {
    int i, id;
    double *members, set_element;
    members = malloc(sizeof(double)*NUMBER_OF_MEMBERS);
    rewind(tmp_file);

    while (!feof(tmp_file)) {
        fscanf(tmp_file, "%d %d %lf\n", &id, &i, &set_element);
        members[i] += set_element;
    }

    //members[i] -= set_element; //because last line was read twice

    for (i = 0; i < NUMBER_OF_MEMBERS; i++) {
        fprintf(result_file, "y[%d] = %f\n", i, members[i]);
    }
    free(members);
}

int wait_for_thread(int threads_count) {
    int i = 0;
    while (THREADS_STATUS[i] == ST_BUSY) {
        i = (++i == threads_count) ? 0 : i;
    }
    return i;
}

char all_finished(int threads_count) {
    int i = 0;
    while ((i != threads_count) && (THREADS_STATUS[i++] != ST_BUSY));
    return i == threads_count;
}

int main (int argc, char const *argv[]) {
    UTIL_NAME = (char*) basename(argv[0]);

    if (argc != ARG_NUMB)
        print_err("Wrong number of arguments", NULL);
    if ((NUMBER_OF_MEMBERS = atoi(argv[1])) == 0)
        print_err("Number of members must be positive", NULL);
    if ((TAYLOR_SET_LENGTH = atoi(argv[2])) == 0)
        print_err("Taylor set length must be positive", NULL);

    FILE *tmp_file = get_tmp_file();

    THREADS_STATUS = calloc(sizeof(thread_status_t), (size_t) TAYLOR_SET_LENGTH);

    int i;
    for (i = 0; i < TAYLOR_SET_LENGTH; i++) {
        THREADS_STATUS[i] = ST_NULL;
    }

    THREADS = calloc(sizeof(pthread_t), (size_t) TAYLOR_SET_LENGTH);

    print_taylor_set_elements(tmp_file);

    while (!all_finished(TAYLOR_SET_LENGTH));

    FILE *result_file = get_result_file(argv[3]);
    print_result(tmp_file, result_file);

    if (fclose(tmp_file) == -1) {
        print_err(strerror(errno), NULL);
    };
    if (fclose(result_file) == -1) {
        print_err(strerror(errno), NULL);
    };
}
