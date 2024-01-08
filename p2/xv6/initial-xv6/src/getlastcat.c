#include "types.h"
#include "stat.h"
#include "user.h"

int main(void){
    char buf[1024];
    if(getlastcat(buf) == 0){
        printf(1, "XV6_TEST_OUTPUT Last catted filename: %s\n", buf);
    }else{
        return -1;
    }
    exit();
}
