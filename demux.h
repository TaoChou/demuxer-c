#include <stdint.h>
#include <pthread.h>
#include "list.h"

#define BOX_HEAD_BYTE 8
#define FULL_BOX_HEAD_BYTE 20
#define BOX_SIZE_BYTE 4
#define BOX_TYPE_BYTE 4
#define BOX_LARGE_SZIE_BYTE 8
#define BOX_VERSION_BYTE 1
#define BOX_FLAGS_BYTE 3

#define FILE_PATH_MAX_LENGTH 256

#define FYTP_BOX_MAJOR_BRAND_BYTE 4
#define FYTP_BOX_MINOR_VERSION_BYTE 4
#define FYTP_BOX_COMPATIBLE_BRANDS_BYTE(x) (x - FYTP_BOX_MAJOR_BRAND_BYTE - FYTP_BOX_MINOR_VERSION_BYTE)

enum DEMUX_MP4_BOX_TYPE{
    DEMUX_MP4_DEFAULT,
    DEMUX_MP4_FTYPE_BOX,
    DEMUX_MP4_MOOV_BOX,
    DEMUX_MP4_MVHD_BOX
};

typedef struct demux_parse_func_info
{
    char box_type[8];
    void* func;
    struct list_head node;
}demux_parse_func_info_t;

typedef struct demux_ctrl
{
    void* fp;
    char file_path[256];
    int file_path_len;
    pthread_mutex_t parse_func_lock;
    struct list_head parse_func_list;
}demux_ctrl_t;

typedef int (*DEMUX_BOX_PARSE)(FILE* fp, uint64_t body_size);

extern int demux_init(demux_ctrl_t* demux_ctrl, char* file_path, int file_path_len);
extern int demux_close(demux_ctrl_t* demux_ctrl);
extern int demux_handle_box_body(demux_ctrl_t* demux_ctrl);