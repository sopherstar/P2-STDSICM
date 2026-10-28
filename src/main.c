//sets up the program, kicks off the engine, and cleans up when finished

#include <stdio.h>
#include <stdlib.h>
#include "lfg.h"  

int main (){
    int n, t, h, d, t1, t2;

    //input 
    FILE *file;
    file = fopen("../config.txt", "r");       

    //error 
    if (file == NULL) {
        printf("Error: Could not open file.\n");
        return 1; 
    }

    fscanf(file, "%d %d %d %d %d %d", &n, &t, &h, &d, &t1, &t2);
    fclose(file); 

    /*
    printf("Max Dungeon Instances (n) : %d\n", n);
    printf("Tank Queue Count (t)      : %d\n", t);
    printf("Healer Queue Count (h)    : %d\n", h);
    printf("DPS Queue Count (d)       : %d\n", d);
    printf("Min Clear Time (t1)       : %d sec\n", t1);
    printf("Max Clear Time (t2)       : %d sec\n", t2);
    */

    engine(n, t, h, d, t1, t2);
    return 0;
}
