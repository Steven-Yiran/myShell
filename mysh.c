/* CSCI 315 Assignment 5 mysh. Steven & Ethan */

#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<unistd.h>
#include<sys/wait.h>
#include<fcntl.h>
#include<errno.h>

#define BUFFER_MAX 4096
#define ARG_MAX 10

enum pipe_type{start, middle, end, nopipe};
enum pipe_end{READ, WRITE};
enum exit_type{progress, endShell, error};

typedef struct opt_info{
    char *input_command;
    char *exec_list[ARG_MAX];
    int exit_status;
    int pipe_state;
    int previous_read;
} opt_info;

void command_loop();
void fillcommand(struct opt_info *opt);
void init(struct opt_info *opt);
void execute(struct opt_info *opt);
void basic_run(struct opt_info *opt);
void pipe_run(struct opt_info *opt);
void redirect(struct opt_info *opt);
void redirect_in(struct opt_info *opt, int index);
void redirect_out(struct opt_info *opt, int index);
void redirect_append(struct opt_info *opt, int index);
void print_list(char **list);


int main(int argc, char *argv[]){

    command_loop();

    return 1;
}


/**
 * @brief keep asking user for shell input
 * 
 */
void command_loop(){

    struct opt_info curr_session;

    if ((curr_session.input_command = (char *)malloc(BUFFER_MAX)) == NULL){
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    while(1){
        //initialize pipe states
        curr_session.pipe_state = nopipe;
        curr_session.exit_status = progress;

        printf("$: ");

        fillcommand(&curr_session);

        if (feof(stdin)){
            printf("\n");
            break;
        }

        init(&curr_session);
        
        if (curr_session.exit_status == endShell)
            break;
    }

    free(curr_session.input_command);
}


/**
 * @brief get user input from stdin
 * 
 * @param opt struct with operation info
 * @return int 
 */
void fillcommand(struct opt_info *opt){
    fgets(opt->input_command, BUFFER_MAX, stdin);
    int end = strlen(opt->input_command);
    opt->input_command[end-1] = '\0';
}


/**
 * @brief loop through all commands, execuate along the way
 * 
 * @param opt struct
 */
void init(struct opt_info *opt){
    char *token;
    token = strtok(opt->input_command, " ");

    if (strcmp(token, "exit") == 0){
        opt->exit_status = endShell;
        return;
    }

    int index = 0;
    while (token != NULL){
        if (opt->exit_status == error)
            break;
        if (strcmp(token, "|") == 0){
            if (opt->pipe_state == nopipe){
                opt->pipe_state = start;
            } else {
                opt->pipe_state = middle;
            }
            opt->exec_list[index] = NULL;
            execute(opt);
            index = 0;
        } else {
            opt->exec_list[index] = token;
            index++;
        }
        token = strtok(NULL, " ");
    }
    opt->exec_list[index] = NULL;
    
    // the last exec chunk
    if (opt->pipe_state != nopipe){
        opt->pipe_state = end;
    }
    execute(opt);
}


/**
 * @brief execute program either with or without pipe
 * 
 * @param opt struct
 */
void execute(struct opt_info *opt){
    if (opt->pipe_state == nopipe){
        basic_run(opt);
    } else {
        pipe_run(opt);
    }
}


/**
 * @brief run program without pipe
 * 
 * @param opt struct
 */
void basic_run(struct opt_info *opt){
    pid_t cpid;
    cpid = fork();
    if (cpid < 0){
        perror("fork failure");
        opt->exit_status = error;
        return;
    } else if (cpid > 0) {
        int wstatus;
        waitpid(cpid, &wstatus, WUNTRACED);
    } else {
        redirect(opt);
        if (opt->exit_status == error)
            exit(EXIT_FAILURE);
        execvp(opt->exec_list[0], opt->exec_list);
        exit(EXIT_FAILURE);
    }
}


/**
 * @brief run program with pipe
 * 
 * @param opt struct
 */
void pipe_run(struct opt_info *opt){
    int cpid, curr_read;
    int pipe_source[2];
    pipe(pipe_source);
    if (opt->pipe_state != start)
        curr_read = opt->previous_read;
    opt->previous_read = pipe_source[READ];
    cpid = fork();
    if (cpid < 0){
        perror("fork failure");
        opt->exit_status = error;
        return;
    } else if (cpid > 0) {
        int wstatus;
        if (opt->pipe_state == start){
            close(pipe_source[WRITE]);
        } else if (opt->pipe_state == middle) {
            close(curr_read);
            close(pipe_source[WRITE]);
        } else if (opt->pipe_state == end) {
            close(curr_read);
        }
        waitpid(cpid, &wstatus, WUNTRACED);
    } else {
        if (opt->pipe_state == start){
            dup2(pipe_source[WRITE], 1);
        } else if (opt->pipe_state == middle) {
            dup2(curr_read, 0);
            dup2(pipe_source[WRITE], 1);
        } else if (opt->pipe_state == end) {
            dup2(curr_read, 0);
        }
        redirect(opt);
        if (opt->exit_status == error)
            exit(EXIT_FAILURE);
        execvp(opt->exec_list[0], opt->exec_list);
        exit(EXIT_FAILURE); // for safty
    }
}


/**
 * @brief manage redirection
 * 
 * @param opt struct
 */
void redirect(struct opt_info *opt){

    int index = 0;
    while(opt->exec_list[index] != NULL){
        if (opt->exec_list[index][0] == '<'){
            redirect_in(opt, index);
            index += 2; // check more redirection
            continue;
        } else if (opt->exec_list[index][0] == '>' && opt->exec_list[index][1] == '>') {
            redirect_append(opt, index);
            index += 2; // check more redirection
            continue;
        } else if (opt->exec_list[index][0] == '>') {
            redirect_out(opt, index);
            index += 2; // check more redirection
            continue;
        }
        index++;
    }
}


/**
 * @brief redirect input
 * 
 * @param opt struct
 * @param index index of redirection operator
 */
void redirect_in(struct opt_info *opt, int index){
    int fd;
    char *infile;

    opt->exec_list[index] = NULL;
    if ((infile = opt->exec_list[index+1]) == NULL){
        errno = EBADF;
        perror("missing redirection file");
        opt->exit_status = error;
        return;
    }
    opt->exec_list[index+1] = NULL;

    if ((fd = open(infile, O_RDONLY)) == -1){
        perror(infile);
        opt->exit_status = error;
        return;
    }
    if (dup2(fd, 0) == -1){
        perror(infile);
        opt->exit_status = error;
        return;
    }
    close(fd);
}


/**
 * @brief redirect output (truncate)
 * 
 * @param opt struct
 * @param index index of redirection operator
 */
void redirect_out(struct opt_info *opt, int index){
    int fd;
    char *outfile;

    opt->exec_list[index] = NULL;
    if ((outfile = opt->exec_list[index+1]) == NULL){
        errno = EBADF;
        perror("missing redirection file");
        opt->exit_status = error;
        return;
    }
    opt->exec_list[index+1] = NULL;
    if ((fd = open(outfile, O_TRUNC | O_WRONLY | O_CREAT, S_IRWXU)) == -1){
        perror(outfile);
        opt->exit_status = error;
        return;
    }
    if (dup2(fd, 1) == -1){
        perror(outfile);
        opt->exit_status = error;
        return;
    }
    close(fd);
}


/**
 * @brief redirect output (append)
 * 
 * @param opt struct
 * @param index index of redirection operator
 */
void redirect_append(struct opt_info *opt, int index){
    int fd;
    char *outfile;

    opt->exec_list[index] = NULL;
    if ((outfile = opt->exec_list[index+1]) == NULL){
        errno = EBADF;
        printf("missing redirection file");
        opt->exit_status = error;
        return;
    }
    opt->exec_list[index+1] = NULL;
    if ((fd = open(outfile, O_APPEND | O_WRONLY | O_CREAT, S_IRWXU)) == -1){
        perror(outfile);
        opt->exit_status = error;
        return;
    }
    if (dup2(fd, 1) == -1){
        perror(outfile);
        opt->exit_status = error;
        return;
    }
    close(fd);
}


