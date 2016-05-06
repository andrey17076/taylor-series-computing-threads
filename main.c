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
    double x;
    FILE* file;
} thread_params_t;

pthread_t *THREADS;
thread_status_t *THREADS_STATUS;

struct thread_info {    /* Used as argument to thread_start() */

};

void count_member(void*);
int wait_for_thread(int);
void print_err(const char*);
double get_x(double, double);
FILE *get_tmp_file();
void count_taylor_set_elements(double, double, FILE*);
FILE *get_result_file(const char*);
void print_result(int, FILE*, FILE*);

void print_err(const char *error_msg) {
    fprintf(stderr, "%s: %s\n", UTIL_NAME, error_msg);
    exit(-1);
}

double get_x(double index, double number_of_members) {
    double x =  2 * M_PI * index / number_of_members;
    return (x > M_PI) ? M_PI - x : x;
}

FILE *get_tmp_file() {
    const char *tmp_fname = "/tmp/tmp.txt";
    FILE *tmp_file;
    if ((tmp_file = fopen(tmp_fname, "w+r")) == NULL) {
        fprintf(stderr, "%s: %s: %s\n", UTIL_NAME, tmp_fname, strerror(errno));
        exit(-1);
    }
    return tmp_file;
}

void count_member(void *arg) {
    thread_params_t *thread_params = (thread_params_t *) arg;
    int i, j, k;
    double x, set_element;

    i = thread_params->member_index;
    j = thread_params->taylors_set_member_index;
    x = thread_params->x;
    FILE* tmp_file = thread_params->file;

    set_element = (j % 2) ? -1 : 1;
    for (k = 1; k <= 2 * j + 1; set_element *= x/k++);
    printf("%ld %d %f\n", syscall(SYS_gettid), i, set_element);
    fprintf(tmp_file, "%ld %d %f\n", syscall(SYS_gettid), i, set_element);

    *(thread_params->thread_status) = ST_FREE;

    while (*(thread_params->thread_status) != ST_NULL);

    free(thread_params);

    return;
}

void count_taylor_set_elements(double number_of_members,
    double taylor_set_length,
    FILE* tmp_file) {

    int i, j;
    pthread_t *pthread;
    double x;
    int running_processes = 0;

    for (i = 0; i < number_of_members; i++) {
        x = get_x(i, number_of_members);
        for (j = 0; j < taylor_set_length; j++) {

            thread_params_t *thread_params;
            pthread_t thread;
            pthread_attr_t pthread_attr;
            pthread_attr_init(&pthread_attr);
            pthread_attr_setdetachstate(&pthread_attr, PTHREAD_CREATE_JOINABLE);

            int thread_id = wait_for_thread(taylor_set_length);
            THREADS_STATUS[thread_id] = ST_NULL;
            if (pthread_join(THREADS[thread_id], NULL) == -1){
                print_err(strerror(errno));
            };

            thread_params = malloc(sizeof(thread_params_t));
            thread_params->thread_status = &(THREADS_STATUS[thread_id]);
            thread_params->member_index = i;
            thread_params->taylors_set_member_index = j;
            thread_params->x = x;
            thread_params->file = tmp_file;

            memset(&thread, 0, sizeof(pthread_t));
            THREADS_STATUS[thread_id] = ST_BUSY;

            if (pthread_create(&thread, &pthread_attr, &count_member, thread_params) == -1) {
                print_err(strerror(errno));
                return;
            };

            THREADS[thread_id] = thread;
        }
    }

}

FILE *get_result_file(const char *arg_param) {
    char res_fname[FILENAME_MAX];
    realpath(arg_param, res_fname);
    FILE *res_file;

    if ((res_file = fopen(res_fname, "w+")) == NULL) {
        fprintf(stderr, "%s: %s: %s\n", UTIL_NAME, res_fname, strerror(errno));
        exit(-1);
    }
    return res_file;
}

void print_result(int number_of_members, FILE* tmp_file, FILE* result_file) {
    int i;
    pid_t pid;
    double *members, set_element;
    members = malloc(sizeof(double)*number_of_members);
    rewind(tmp_file);

    while (!feof(tmp_file)) {
        fscanf(tmp_file, "%d %d %lf\n", &pid, &i, &set_element);
        members[i] += set_element;
    }

    //members[i] -= set_element; //because last line was read twice

    for (i = 0; i < number_of_members; i++) {
        fprintf(result_file, "y[%d] = %f\n", i, members[i]);
    }
    free(members);
}

int wait_for_thread(int threads_count) {
    int i = 0;
    while (THREADS_STATUS[i] == ST_BUSY) {
        i++;
        if (i == threads_count) {
            i = 0;
        }
    }
    return i;
}

char all_finished(int threads_count) {
    int i;
    for (i = 0; (i < threads_count); i++){
        if (THREADS_STATUS[i] != ST_FREE){
            return 0;
        }
    }
    return 1;
}

int main (int argc, char const *argv[]) {
    UTIL_NAME = (char*) basename(argv[0]);

    int i, number_of_members, taylor_set_length;

    if (argc != ARG_NUMB)
        print_err("Wrong number of arguments");
    if ((number_of_members = atoi(argv[1])) == 0)
        print_err("Number of members must be positive");
    if ((taylor_set_length = atoi(argv[2])) == 0)
        print_err("Taylor set length must be positive");

    FILE *tmp_file = get_tmp_file();

    THREADS_STATUS = calloc(sizeof(thread_status_t), (size_t) taylor_set_length);

    for (i = 0; i < taylor_set_length; i++) {
        THREADS_STATUS[i] = ST_NULL;
    }

    THREADS = calloc(sizeof(pthread_t), (size_t) taylor_set_length);

    count_taylor_set_elements(number_of_members, taylor_set_length, tmp_file);

    while (!all_finished(taylor_set_length));

    FILE *result_file = get_result_file(argv[3]);
    print_result(number_of_members, tmp_file, result_file);

    if (fclose(tmp_file) == -1) {
        fprintf(stderr, "%s: %s\n", UTIL_NAME, strerror(errno));
    };
    if (fclose(result_file) == -1) {
        fprintf(stderr, "%s: %s\n", UTIL_NAME, strerror(errno));
    };
}
