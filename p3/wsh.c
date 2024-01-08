#include <sys/types.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>

int ids[256]={0};
int num_arg = 0;
int shell_pgid;
int curr_fg_id = -1;
size_t input_size = 256;
//int pipe_pgid = -1;

struct Job {
    int id;
    char command[256];
    int job_status; //0 for fg, 1 for bg, 2 for stopped
    int pid;
    int pgid;
};

struct Job *job_arr[256];

void add_job(int id, char *argument, int job_status, int pid) {
    job_arr[id - 1] = malloc(sizeof(struct Job));
    job_arr[id - 1]->id = id;
    strcpy(job_arr[id-1]->command, argument);
    job_arr[id - 1]->job_status = job_status;
    job_arr[id - 1]->pid = pid;
    job_arr[id - 1]-> pgid = pid;
}

void remove_jobs() {
    for(int i = 0; i < 256; i ++){
        if(ids[i] == 1){
            int status;
            pid_t result = waitpid(job_arr[i]->pid, &status, WNOHANG | WUNTRACED);
            if(result > 0){
                job_arr[i] = NULL;
                free(job_arr[i]);
                ids[i] = 0;
            }
        }
    }
}

void fg_remove(){
    if(curr_fg_id != -1 && job_arr[curr_fg_id - 1] != NULL){
        if(job_arr[curr_fg_id - 1]->job_status == 0){
            //printf("From fgremove: %s\n", job_arr[curr_fg_id - 1]->command);
            int status;
            waitpid(job_arr[curr_fg_id - 1]->pgid, &status, WUNTRACED);
            if(WIFEXITED(status)){
                job_arr[curr_fg_id - 1] = NULL;
                free(job_arr[curr_fg_id - 1]);
                //job_arr[curr_fg_id - 1] = NULL;
                ids[curr_fg_id - 1]= 0;
            }
        }   
    }
    curr_fg_id = -1;
}

int setjobid(){
    int newid = 0;
    for(int i = 0; i < 256; i++){
        if (ids[i] == 0){
            newid = i + 1;
            ids[i] = 1;
            break;
        }
    }  
    return newid;
}
void put_background(int id){
    if(job_arr[id - 1] != NULL){
        if(job_arr[id - 1]->job_status == 2){
            job_arr[id - 1]->job_status = 1;
            kill(job_arr[id - 1] -> pgid, SIGCONT);
        }
    }else{
        printf("bg error\n");
    }
}

void put_foreground(int id){
    if(job_arr[id - 1] != NULL){
        job_arr[id - 1]->job_status = 0;
        tcsetpgrp(STDIN_FILENO, job_arr[id - 1]->pgid);
        kill(-job_arr[id - 1]->pgid, SIGCONT);
        int status;
        waitpid(job_arr[id - 1]->pgid, &status, WUNTRACED);
        if(WIFSTOPPED(status)){
            job_arr[id - 1]->job_status = 2;
        }
    }
    tcsetpgrp(STDIN_FILENO, shell_pgid);
}

void sigint_handler(int sig)
{
    // Handle CTRL-C here
    
    if (curr_fg_id != -1 && job_arr[curr_fg_id - 1] != NULL)
    {
        kill(job_arr[curr_fg_id - 1]->pgid, SIGINT);
        job_arr[curr_fg_id - 1]->job_status = 0;
        //printf("%s\n", job_arr[curr_fg_id - 1]->command);
    }
}
void sigtstp_handler(int sig)
{
    // Handle CTRL-Z here
    //printf("%d\n", curr_fg_id);
    if (curr_fg_id != -1 && job_arr[curr_fg_id - 1] != NULL)
    {
        kill(job_arr[curr_fg_id - 1]->pgid, SIGSTOP);
        job_arr[curr_fg_id - 1]->job_status = 2;
    }
}

void pipe_dealer(char* args[], int num_arg, int num_cmd, int bg_indicator){
    char *cmds[num_cmd][256];
    int cmd_i = 0;
    int j = 0;
    int pgid = -1;
    int pid;
    int pipefd[num_cmd - 1][2];

    for(int i = 0; i < num_arg; i++){
        if(strcmp(args[i], "|") != 0){
            cmds[cmd_i][j] = args[i];
            //printf("%s\n", cmds[cmd_i][j]);
            j++;
        }else{
            cmds[cmd_i][j] = NULL;
            //printf("%d\n", j);
            cmd_i++;
            j = 0;
        }
    }
    cmds[cmd_i][j] = NULL;

    for(int i = 0; i < num_cmd - 1; i++){
        if(pipe(pipefd[i]) == -1){
            exit(-1);
        }
    }

    for(int i = 0; i < num_cmd; i++){
        pid = fork();
        if(pid == -1){
            printf("fork fails\n");
            exit(-1); 
        }
        else if (pid == 0){
            if(pgid == -1){
                pgid = getpid();
            }
            setpgid(getpid(), pgid);
            if(i > 0){
                dup2(pipefd[i - 1][0], STDIN_FILENO);
            }
            if(i < num_cmd - 1){
                dup2(pipefd[i][1], STDOUT_FILENO);
            }
            for(int k = 0; k < num_cmd - 1; k++){
                close(pipefd[k][0]);
                close(pipefd[k][1]);
            }
            char p[256];
            sprintf(p, "/bin/%s", cmds[i][0]);
            //printf("<%s>\n", p);
            //printf("(%s %s)\n", cmds[i][0], cmds[i][1]);
            execvp(p, cmds[i]);
            exit(-1);
        }
    }
    for (int i = 0; i < num_cmd - 1; i++){
        close(pipefd[i][0]);
        close(pipefd[i][1]);
    }
    while(wait(NULL) > 1);
}

void exec_cmds(char *args[], int pipe_indicator, int background_indicator, int num_cmd, char *input){
    //printf("    %s\n", input);
    char path[256]; 
    if(strcmp(args[0], "exit") == 0){
        if(num_arg == 1){
            exit(0);
        }
        else{
            exit(-1);
        }
    }

    else if(strcmp(args[0], "cd") == 0){
        if(num_arg == 2){
            if (chdir(args[1]) != 0){
                printf("path DNE\n");
                exit(-1);
            }
        }else{
            //signal error
            printf("flag3\n");
            exit(-1);
        }
    }

    else if(strcmp(args[0], "jobs") == 0){
        if(num_arg == 1){
            for(int i = 0; i < 256; i++){
                if(ids[i] == 1){
                    if(job_arr[i]->job_status == 1 || job_arr[i]->job_status == 2){
                        printf("%d: ", job_arr[i]->id);
                        printf("%s\n", job_arr[i]->command);
                    }
                }
            }
        }
        else{
            //signal error
            printf("flag4\n");
            exit(-1);
        }
    }

    else if(strcmp(args[0], "fg") == 0){
        int target_id = -1;
        if(num_arg == 1){
            //fg_command(-1);
            for(int i = 255; i >=0; i--){
                if(ids[i] == 1 && job_arr[i] != NULL && (job_arr[i]->job_status == 1 || job_arr[i]->job_status == 2)){
                    target_id = job_arr[i]->id;
                    put_foreground(target_id);
                    break;
                }
            }
        }else if(num_arg == 2){
            target_id = atoi(args[1]);
            put_foreground(target_id);
            //fg_command(atoi(args[1]));
        }
        else{
            //signal error
            printf("flag5\n");
            exit(-1);
        }
        curr_fg_id = target_id;
        fg_remove();
    }
    else if(strcmp(args[0], "bg") == 0){
        if(num_arg == 1){
            //bg_command(-1);
            for(int i = 255; i >=0; i--){
                if(ids[i] == 1 && job_arr[i] != NULL && job_arr[i]->job_status == 2){
                    int max_id = job_arr[i]->id;
                    put_background(max_id);
                    break;
                }
            }
        }else if (num_arg == 2){
            put_background(atoi(args[1]));
            //bg_command(atoi(args[1]));
        }
        else{
            //signal error
            printf("flag6\n");
            exit(-1);
        }
    }else{
        if(pipe_indicator == 1){
            pipe_dealer(args, num_arg, num_cmd, background_indicator);
        }
        else{
            //printf("%s\n",args[0]);
            sprintf(path, "/bin/%s", args[0]);
            //signal(SIGTSTP, SIG_DFL);
            //signal(SIGCHLD, fg_remove);
            int f = fork();
            if(f == -1){
                printf("fork fails\n");
                exit(-1);
            }
            else if(f == 0){
                //printf("%s\n", path); 
                //pid_t pid = getpid();
                setpgid(0, 0);
                signal(SIGTSTP, SIG_DFL);
                signal(SIGINT, SIG_DFL);
                signal(SIGCHLD, SIG_DFL);
                execvp(path, args);
                printf("error executing\n");
                exit(-1);
            }
            else{
                int status;
                int id = setjobid();
                //setpgid(f, f);
                add_job(id, input, background_indicator, f);
                if(background_indicator == 0){
                    curr_fg_id = id;
                    //tcsetpgrp(STDIN_FILENO, f);
                    if(waitpid(f, &status, WUNTRACED) == -1){
                        exit(-1);
                    }
                    tcsetpgrp(STDIN_FILENO, shell_pgid);
                }else{
                    waitpid(f, &status, WNOHANG); 
                    //tcsetpgrp(STDIN_FILENO, shell_pgid); 
                }
            }
        }
    }
}

int main(int argc, char *argv[]){
    signal (SIGINT, sigint_handler);
    signal (SIGTSTP, sigtstp_handler);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGCHLD, fg_remove);
    shell_pgid = tcgetpgrp(0);
    if(argc == 1){
        while(1){ 
            printf("wsh> ");
            char *args[256];
            char *input;
            char *input_bkup;
            char *token;
            int pipe_indicator = 0;
            int bg_indicator = 0;
            int num_cmd = 0;
            num_arg = 0;

            input = (char *)malloc(input_size * sizeof(char));
            if(input == NULL){
                exit(-1);
            }
            input_bkup = (char *)malloc(input_size * sizeof(char)); 
            if(input_bkup == NULL){
                exit(-1);
            }
            ssize_t line = getline(&input, &input_size, stdin);
            strcpy(input_bkup, input);
            input_bkup[strlen(input_bkup) - 1] = '\0';
            
            if(line == -1){
                free(input);
                free(input_bkup);
                exit(0);
            }
            while((token = strsep(&input," \t\n")) != NULL){
                if (*token) {
                //strncpy(args[index], token, sizeof(args[index]));
                //printf("%s", token);
                if(strcmp(token, "|") == 0){
                    pipe_indicator = 1;
                    num_cmd ++;
                }
                args[num_arg] = token;
                num_arg++;
                }
            }
            num_cmd++;
            args[num_arg] = NULL;
            if(strcmp(args[num_arg - 1], "&") == 0){
                args[num_arg - 1] = NULL;
                bg_indicator = 1;
            }
            remove_jobs();
            //fg_remove();
            exec_cmds(args, pipe_indicator, bg_indicator, num_cmd, input_bkup);
            free(input);
            free(input_bkup);
        }
    }
    else if(argc == 2){
        char *args[256];
        char *input;
        char *input_bkup;
        char *token;
        
        input = (char *)malloc(input_size * sizeof(char));
        if(input == NULL){
            exit(-1);
        }
        input_bkup = (char *)malloc(input_size * sizeof(char)); 
        if(input_bkup == NULL){
            exit(-1);
        }
        FILE *fp = fopen(argv[1], "r");
        if(fp == NULL){
            exit(-1);
        }
        ssize_t line = getline(&input, &input_size, fp);
        if(line == -1){
            free(input);
            free(input_bkup);
            exit(0);
        }
        strcpy(input_bkup, input);
        input_bkup[strlen(input_bkup) - 1] = '\0';
        while(line >= 0){
            int pipe_indicator = 0;
            int bg_indicator = 0;
            int num_cmd = 0;
            num_arg = 0;
           while((token = strsep(&input," \t\n")) != NULL){
                if (*token) {
                //strncpy(args[index], token, sizeof(args[index]));
                //printf("%s", token);
                if(strcmp(token, "|") == 0){
                    pipe_indicator = 1;
                    num_cmd ++;
                }
                args[num_arg] = token;
                num_arg++;
                }
            }
            num_cmd++;
            args[num_arg] = NULL;
            if(strcmp(args[num_arg - 1], "&") == 0){
                args[num_arg - 1] = NULL;
                bg_indicator = 1;
            }
            remove_jobs();
            exec_cmds(args, pipe_indicator, bg_indicator, num_cmd, input_bkup);
            free(input);
            free(input_bkup);
            input = (char *)malloc(input_size * sizeof(char));
            if(input == NULL){
                exit(-1);
            }
            input_bkup = (char *)malloc(input_size * sizeof(char)); 
            if(input_bkup == NULL){
                exit(-1);
            }
            line = getline(&input, &input_size, fp);
            strcpy(input_bkup, input);
            input_bkup[strlen(input_bkup) - 1] = '\0'; 
        }
        fclose(fp);
    }
    else{
        exit(-1);
    }
    return 0;
}
