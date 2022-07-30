#include <stdio.h>
#include <stdint.h>
#include <malloc.h>
#include <string.h>
#include "demux.h"

int main(int argc, char** argv){
    char* file_path = NULL;
    uint32_t path_len = 0;
    FILE* fp = NULL;
    char box_type[8] = {0};
    int ret = 0;

    if(argc < 2){
        printf("arg error\n");
        return -1;
    }
    
    path_len = strlen(argv[1]);
    file_path = (char*)calloc(1, path_len + 1);
    strcpy(file_path, argv[1]);

    printf("open %s\n", file_path);

    demux_ctrl_t* demux_ctrl = (demux_ctrl_t*)calloc(1, sizeof(demux_ctrl_t));
    if(demux_ctrl == NULL){
        printf("demux_ctrl NULL\n");
    }
    demux_init(demux_ctrl, file_path, strlen(file_path));

    while(ret >= 0){
        ret = demux_handle_box_body(demux_ctrl);
    }

    demux_close(demux_ctrl);
    free(file_path);

    return 0;
}