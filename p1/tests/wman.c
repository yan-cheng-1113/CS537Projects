#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
	FILE *fp;
	struct stat file_info;	
	
	if (argc == 1) { //Check the number of arguments
        printf("What manual page do you want?\nFor example, try 'wman wman'\n");
        return 0;
    }

	if (argc == 2){//single-argument usage
		char path[256];
		for (int i = 1; i <= 9; i++) {
			sprintf(path, "./man_pages/man%d/%s.%d", i, argv[1], i);
			if(stat(path, &file_info) != 0){//check if the file exits in the path
				continue;
			}
			fp = fopen(path, "r");
			if (fp != NULL) {//check if the file can be opened correctly
				char ch;
				while((ch = fgetc(fp)) != EOF){
					putchar(ch);
				}
				fclose(fp);
				return 0;
			}else{
				printf("cannot open file\n");
				exit(1);
			}
		}
		printf("No manual entry for %s\n", argv[1]);//no such file can be found
	}
	else{//two-argument usage
		int sec;
		char path[256];
		sec = atoi(argv[1]) ;
		if(sec < 1 || sec > 9){//check if section is a decimal number in the proper range
			printf("invalid section\n");
			exit(1);
		}
		sprintf(path, "./man_pages/man%d/%s.%d", sec, argv[2], sec);
		fp = fopen(path, "r");
		if(stat(path, &file_info) != 0){//check if the file exits in the path
            printf("No manual entry for %s in section %s\n", argv[2], argv[1]);//no such file can be found
			return 0;
        }
		if (fp != NULL) {//check if the file can be opened
			char ch;
			while((ch = fgetc(fp)) != EOF){
				putchar(ch);
			}
			fclose(fp);
			return 0;
		}else{
			printf("cannot open file\n");
			exit(1);
		}	
	}
	return 0;
}
