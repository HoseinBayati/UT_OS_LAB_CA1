#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
void compare_strings(int fd, const char* str1, const char* str2){ 
    int len1 = strlen(str1);
    int len2 = strlen(str2);
    int max_len;
    if(len1 > len2){
        max_len = len1;
    }
    else{
        max_len = len2;
    }
    for(int i = 0; i < max_len; i++){
        int tmp1, tmp2;
        if(i < len1){
            tmp1 = ((int)str1[i] - 64) % 32;
        }
        else{
            tmp1 = -1;
        }
        if(i < len2){
            tmp2 = ((int)str2[i] - 64) % 32;
        }
        else{
            tmp2 = -1;
        }
        if(tmp1 < tmp2){
            printf(fd, "%d", 1);
        }
        else{
            printf(fd, "%d", 0);
        }   
    }
    printf(fd, "\n");
    return;
}
int main(int argc, char *argv[]) {
    unlink("strdiff_result.txt");
    int fd = open("strdiff_result.txt", O_CREATE | O_WRONLY);
    if(fd < 0){
        printf(1, "Can;t create the file");
    }
    compare_strings(fd, argv[1], argv[2]);
    close(fd);
    exit();
}