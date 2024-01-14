#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
 
int main(int argc, char *argv[]) {
     if (argc == 1){//check if the keyword is provided
        printf("wapropos what?\n");
        exit(0);
    }

    struct Manual//create a struct to store the manual information
    {
        char name[512];
        char section;
        char name_one_liner[512];
    };

    struct dirent *entry;//use dirent to read directories
    int found = 0;//check if the keyword is found
    char subds[9][512];

    for(int i = 0; i < 9; i++){//iteratively store paths of subdirectories
       snprintf(subds[i], sizeof(subds[i]), "./man_pages/man%d/", i+1); 
    }

    for(int i = 0; i < 9; i++){
        DIR *subdir = opendir(subds[i]);
        if (subdir == NULL) {
            printf("Can't open directory\n");
            exit(1);
        } 
        while((entry = readdir(subdir)) != NULL) {
            char filename[1024];
            if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || strcmp(entry -> d_name, ".gitignore") == 0){
                continue;
            }
            snprintf(filename, sizeof(filename), "%s%s", subds[i], entry->d_name);
            FILE *fp = fopen(filename, "r");
            if (fp == NULL) {
                printf("can't open file\n");
                exit(1);
            } 
    
            struct Manual manual;
            manual.name[0] = '\0';
            manual.section = entry->d_name[strlen(entry->d_name) - 1];
            manual.name_one_liner[0] = '\0';

            char line[512];
            int inName = 0;
            int liner = -1;
            int inDescription = 0;
            char *token;
            
            while (fgets(line, sizeof(line), fp) != NULL){
                if(strcmp(line, "\033[1mNAME\033[0m\n") == 0){//check if in Name section now
                    inName = 1;
                }
                else if(strcmp(line, "\033[1mDESCRIPTION\033[0m\n") == 0){//check if in Description section
                    inDescription = 1;
                }
                else if (strncmp(line, "\033[1m", 4) == 0){//in neither sections
					inName = 0;
					liner = 0;
                    inDescription = 0;
                }

                if(inName == 1){
                    liner ++;
                }
                if (liner == 1){//save name_one_liner
                    token = strtok(line, "-");
                    if(token != NULL){
                        strcpy(manual.name, token);
                    }
                    token = strtok(NULL, "-");
                    if(token != NULL){
                        strcpy(manual.name_one_liner, token);
                    }
                }
        
                if(inDescription == 1 || inName == 1){
                    if (strstr(line, argv[1]) != NULL){
                        printf("%s(%c) -%s", &manual.name[7], manual.section, manual.name_one_liner);
                        found ++;
                        break;
                    }
                }
            }
            fclose(fp);
        }        
        closedir(subdir);
    }
    if (found == 0){
        printf("nothing appropriate\n");
    }
    return 0;
}    
