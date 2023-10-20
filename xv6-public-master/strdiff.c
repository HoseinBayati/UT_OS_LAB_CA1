#include "types.h"
#include "user.h"

int main(int argc, char *argv[])
{
    for(uint i = 1 ; i < argc ; i++){
        printf(1, "%s ", argv[i]);
    }
    printf(1, "\n");

    printf(1, "%d \n", strcmp(argv[1], argv[2]));



    // char str1[15] = "";
    // // int str1_len = 0;
    // char str2[15] = "";
    // // int str2_len = 0;
    // int i = 0;
    // while (1)
    // {
    //     char curr_char[1];

    //     read(0, curr_char, sizeof(char));
    //     if (curr_char[0] == ' ')
    //         break;

    //     str1[i] = curr_char[0];
    //     i++;
    // }
    // // str1_len = i + 1;

    // i = 0;
    // while (1)
    // {
    //     char curr_char[1];

    //     read(0, curr_char, sizeof(char));
    //     if (curr_char[0] == '\n' || curr_char[0] == ' ')
    //         break;

    //     str2[i] = curr_char[0];
    //     i++;
    // }
    // str2_len = i + 1;

    // printf(1, "%d \n", strcmp(str1, str2));
}
