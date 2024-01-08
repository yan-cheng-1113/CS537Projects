#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char **argv)
{
    if(argc != 2){
        printf(2, "usage: nice nice_value\n");
        exit();
    }
    nice(atoi(argv[1]));
    exit();
}
