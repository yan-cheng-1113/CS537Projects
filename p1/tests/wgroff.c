#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include <sys/stat.h>

#define MAX_LINE_LENGTH 1024

void convert_line(char *line){//convert the format of a line
    char *pos;
    while ((pos = strstr(line, "/fB")) != NULL){
        char *ori = "/fB";
        char *sub = "\033[1m";
        int oriLen = strlen(ori);
        int subLen = strlen(sub);
        memmove(pos + subLen, pos + oriLen, strlen(pos + oriLen) + 1);
        memmove(pos, sub, subLen);
        pos += subLen;
    }

    while ((pos = strstr(line, "/fI")) != NULL){
        char *ori = "/fI";
        char *sub = "\033[3m";
        int oriLen = strlen(ori);
        int subLen = strlen(sub);
        memmove(pos + subLen, pos + oriLen, strlen(pos + oriLen) + 1);
        memmove(pos, sub, subLen);
        pos += subLen;
    }

    while ((pos = strstr(line, "/fU")) != NULL){
        char *ori = "/fU";
        char *sub = "\033[4m";
        int oriLen = strlen(ori);
        int subLen = strlen(sub);
        memmove(pos + subLen, pos + oriLen, strlen(pos + oriLen) + 1);
        memmove(pos, sub, subLen);
        pos += subLen;
    }

    while ((pos = strstr(line, "/fP")) != NULL){
        char *ori = "/fP";
        char *sub = "\033[0m";
        int oriLen = strlen(ori);
        int subLen = strlen(sub);
        memmove(pos + subLen, pos + oriLen, strlen(pos + oriLen) + 1);
        memmove(pos, sub, subLen);
        pos += subLen;
    }

    while ((pos = strstr(line, "//")) != NULL){
        char *ori = "//";
        char *sub = "/";
        int oriLen = strlen(ori);
        int subLen = strlen(sub);
        memmove(pos + subLen, pos + oriLen, strlen(pos + oriLen) + 1);
        memmove(pos, sub, subLen);
        pos += subLen;
    }
}

int main(int argc, char *argv[]) {
	struct stat file_info;	
    regex_t regex;
    const char *pattern = "^[0-9]{4}-[0-9]{2}-[0-9]{2}$";
    int reti = regcomp(&regex, pattern, REG_EXTENDED);//compile a regex to verify the format of date
    if (reti) {
        printf("Could not compile regex\n");
        exit(1);
    }

    if (argc != 2) {//check the number of arguments
        printf("Improper number of arguments\nUsage: ./wgroff <file>\n");
        return 0;
    }

    FILE *input_file = fopen(argv[1], "r");
    if(stat(argv[1], &file_info) != 0){//check if the file exits in the path
		printf("File doesn't exist\n");
        exit(0);
	}

    if (input_file == NULL) {
        printf("can't open the file\n");
        exit(1);
    }

    char output_filename[MAX_LINE_LENGTH];
    char line[MAX_LINE_LENGTH];
    char *command, *section, *date;
    char *date_copy;
    int line_number = 0;
    FILE *output_file = NULL;

    while (fgets(line, sizeof(line), input_file) != NULL) {
        line_number++;
        if (line[0] == '#') {//ignore lines that start with a #
            continue;
        }

        if (line_number == 1) {//checkthe format of the first line
            if(strncmp(line, ".TH ", 4) != 0){
                printf("Improper formatting on line %d\n", line_number);
                exit(0);
            }
            command = strtok(line + 4, " ");
            section = strtok(NULL, " ");
            date = strtok(NULL, " \n");
            
            if(command == NULL || section == NULL || date == NULL){
                printf("Improper formatting on line %d\n", line_number);
                exit(0); 
            }
            if(atoi(section) < 1 ||atoi(section) > 9){
                printf("Improper formatting on line %d\n", line_number);
                exit(0);  
            }

            reti = regexec(&regex, date, 0, NULL, 0);
            if(reti == REG_NOMATCH){
                printf("Improper formatting on line %d\n", line_number);
                exit(0);  
            }

            date_copy = strdup(date);
            
            snprintf(output_filename, sizeof(output_filename), "%s.%s", command, section);
            
            output_file = fopen(output_filename, "w");
            if (output_file == NULL) {
                printf("error creating output file\n");
                fclose(input_file);
                exit(1);
            }

            fprintf(output_file, "%s(%s)", command, section);
            
            for (int i = 0; i < 80 - 2 * (strlen(command) + strlen(section) + 2); i++) {
                fputc(' ', output_file);
            }
            fprintf(output_file, "%s(%s)\n", command, section);
            continue;
        }
         
        if (strncmp(line, ".SH ", 4) == 0) {//check format of section header
            fputs("\n", output_file); 
            char *section_name = line + 4;
            if(strcmp(section_name, "\n") == 0){
                printf("Improper formatting on line %d\n", line_number);
                exit(0);   
            }
            
            section_name[strlen(section_name) - 1] = '\0';
            for (int i = 0; i < strlen(section_name); i++) {
                section_name[i] = toupper(section_name[i]);//change section name to upper-case
            }
            fprintf(output_file, "\033[1m%s\033[0m\n",section_name);//make section name in bold
        } 
        else {
            convert_line(line);
            fputs("       ", output_file);//indent the content 7 spaces
            fputs(line, output_file);
        }
    }
    for (int i = 0; i < 35; i++){
       fputc(' ', output_file); 
    }
    fputs(date_copy, output_file);

    for(int i = 45; i < 80; i++){
       fputc(' ', output_file);  
    } 

    fputc('\n', output_file);

    fclose(input_file);
    if (output_file) {
        fclose(output_file);
    }
    return 0;
}
