#include <stdio.h>
#include <malloc.h>
#include <string.h>
#include <time.h>
#include "demux.h"

// 具体看 https://developer.apple.com/library/archive/documentation/QuickTime/QTFF/QTFFChap2/qtff2.html

#pragma pack (4) 
typedef struct mvhd_box{
    uint32_t creation_time;
    uint32_t modification_time;
    uint32_t timescale;
    uint32_t duration;
    uint32_t preferred_rate;
    uint16_t preferred_volume;
    uint8_t reserved[10];
    uint8_t matrix[36];
    uint32_t preview_time;
    uint32_t preview_duration;
    uint32_t poster_time;
    uint32_t selection_time;
    uint32_t selection_duration;
    uint32_t current_time;
    uint32_t next_track_id;
}mvhd_box_t;

typedef struct mvhd_extend_box{
    uint64_t creation_time;
    uint64_t modification_time;
    uint32_t timescale;
    uint64_t duration;
    uint32_t rate;
    uint16_t volume;
    uint32_t reserved[2];
    uint32_t matrix[9];
    uint32_t pre_defined[6];
    uint32_t next_track_id;
}mvhd_extend_box_t;

typedef struct tkhd_box{
    uint32_t creation_time;
    uint32_t modification_time;
    uint32_t track_id;
    uint32_t reserved0;
    uint32_t duration;
    uint64_t reserved1;
    uint16_t layer;
    uint16_t alternate_group;
    uint16_t volume;
    uint16_t reserved2;
    uint8_t matrix[36];
    uint32_t track_width;
    uint32_t track_height;
}tkhd_box_t;

typedef struct hdlr_box{
    uint8_t component_type[4];
    uint8_t component_subtype[4];
    uint32_t component_manufacturer;
    uint32_t component_flags;
    uint32_t component_flags_mask;
    uint8_t* component_name;
}hdlr_box_t;

typedef struct avc1_box{
    uint8_t reserved[6];
    uint16_t data_reference_index;
    uint16_t version;
    uint16_t revidion_level;
    uint32_t vendor;
    uint32_t temporal_quality;
    uint32_t spatial_quality;
    uint16_t width;
    uint16_t height;
    uint32_t horizonta_resolution;
    uint32_t vertical_resolution;
    uint32_t data_size;
    uint16_t frame_count;
    uint8_t compressor_name[32];
    uint16_t depth;
    uint16_t color_table_id;
}avc1_box_t;

typedef struct avcC_box{
    uint8_t configuration_version;
    uint8_t avc_profile_indication;
    uint8_t profile_compatibility;
    uint8_t avc_level_indication;
    uint8_t length_size_minusOne; // // | reserved bit(6) | bit(2) |, bit(6) must be ‘111111’
    uint8_t num_of_sequence_parameter_sets; // | reserved bit(3) | bit(5) |, bit(3) must be ‘111’
    uint16_t sequence_parameter_set_length;
    uint8_t *sequence_parameter_set_nal_unit; //bit(8*sequence_parameter_set_length)
    uint8_t num_of_picture_parameter_sets;
    uint16_t picture_parameter_set_length;
    uint8_t *picture_parameter_set_nal_unit; // bit(8*pictureParameterSetLength) 
}avcC_box_t;

typedef struct stsc_box{
    uint32_t first_chunk;
    uint32_t samples_per_chunk; 
    uint32_t sample_description_index;
}stsc_box_t;


typedef struct  video_ctrl{
    uint32_t i_frame_count;
    uint32_t* i_frame_num_buf;

    uint32_t chunk_count;
    uint32_t* chunk_offset_buf;
    
    uint32_t stsc_entry_count;
    stsc_box_t* stsc_box;
    
    uint32_t sample_count;
    uint32_t sample_size;
    uint32_t* sample_size_buf;

    uint32_t sps_len;
    int8_t* sps;
    uint32_t pps_len;
    int8_t* pps;
}video_ctrl_t;

video_ctrl_t video_ctrl;

#pragma pack ()

#define DEMUX_MVHD_CREATETIME_OFFSET 2082844800

static int demux_open_file(char* file_path, FILE** fp){
    if(file_path == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    *fp = fopen(file_path, "r+");
    if(*fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    return 0;
}

static int demux_parse_func_regsistor(demux_ctrl_t* demux_ctrl, const char* box_type, DEMUX_BOX_PARSE func){
    demux_parse_func_info_t* demux_parse_func_info =
        (demux_parse_func_info_t*)calloc(sizeof(demux_parse_func_info_t), 1);
    if (NULL == demux_parse_func_info) {
        printf("demux_parse_func NULL\n");
        return -1;
    }

    INIT_LIST_HEAD(&demux_parse_func_info->node);
    strncpy(demux_parse_func_info->box_type, box_type, BOX_TYPE_BYTE);
    demux_parse_func_info->func = func;

    pthread_mutex_lock(&demux_ctrl->parse_func_lock);
    list_add_tail(&demux_parse_func_info->node, &demux_ctrl->parse_func_list);
    pthread_mutex_unlock(&demux_ctrl->parse_func_lock);

    return 0;
}

static int demux_read_big_endian_data(FILE* fp, uint8_t* dest, uint32_t size){
    if(fp == NULL || size == 0){
        printf("file_path [%p], body_size[%d]\n", fp, size);
        return -1;
    }
    
    int32_t read_size = 0;
    int32_t i = 0;

    for(i = (size - 1);i >= 0;i--){
        read_size = fread(&dest[i], sizeof(uint8_t), 1, fp);
        if(read_size < 1){
            printf("read big endian data failed, read_size[%d]\n", read_size);
            return -1;
        }
    }

    return 0;
}

static int demux_read_small_endian_data(FILE* fp, uint8_t* dest, uint32_t size){
    if(fp == NULL || size == 0){
        printf("file_path [%p], body_size[%d]\n", fp, size);
        return -1;
    }
    
    int32_t read_size = 0;
    int32_t i = 0;

    read_size = fread(dest, sizeof(uint8_t), size, fp);
    if(read_size < size){
        printf("read small endian data failed, read_size[%d]\n", read_size);
        return -1;
    }

    return 0;
}

static int demux_parse_ftyp_box(FILE* fp, uint64_t body_size){
    int read_size = 0;
    char major_brand[4+1] = {0};
    uint32_t minor_version = 0;
    char* compatible_brands = NULL;
    int compatible_brands_len = 0;

    printf("start parse ftyp box\n");

    if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    read_size = fread((uint8_t*)major_brand, sizeof(uint8_t), FYTP_BOX_MAJOR_BRAND_BYTE, fp);
    if(read_size < FYTP_BOX_MAJOR_BRAND_BYTE){
        printf("read major_brand failed, read_size[%d]\n", read_size);
        return -1;
    }
    printf("major_brand[%s]\n", major_brand);

    read_size = fread((uint8_t*)&minor_version, sizeof(uint8_t), FYTP_BOX_MINOR_VERSION_BYTE, fp);
    if(read_size < FYTP_BOX_MINOR_VERSION_BYTE){
        printf("read minor_version failed, read_size[%d]\n", read_size);
        return -1;
    }
    printf("minor_version[%d]\n", minor_version);

    compatible_brands_len = FYTP_BOX_COMPATIBLE_BRANDS_BYTE(body_size);
    compatible_brands = (char*)calloc(sizeof(uint8_t), compatible_brands_len);
    if(compatible_brands == NULL){
        printf("compatible_brands NULL\n");
        return -1;
    }
    read_size = fread((uint8_t*)compatible_brands, sizeof(uint8_t), compatible_brands_len, fp);
    if(read_size < compatible_brands_len){
        printf("read compatible_brands failed, read_size[%d]\n", read_size);
        return -1;
    }
    printf("compatible_brands[%s] %d\n", compatible_brands, compatible_brands_len);
    free(compatible_brands);
    compatible_brands = NULL;

    return 0;
}

static int demux_parse_free_box(FILE* fp, uint64_t body_size){
    if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    return 0;
}

static int demux_parse_mdat_box(FILE* fp, uint64_t body_size){
    if(fp == NULL){
        printf("file_path [%p]\n", fp);
        return -1;
    }

    int read_size = 0;
    uint8_t* mdat_body_data = NULL;

    printf("start parse mdat box\n");

    if(body_size > 0){
#if 0
        mdat_body_data = (uint8_t*)calloc(body_size, 1);
        read_size = fread((uint8_t*)mdat_body_data, sizeof(uint8_t), body_size, fp);
        if(read_size < body_size){
            printf("read mdat failed, read_size[%d]\n", read_size);
            return -1;
        }

        free(mdat_body_data);
#else
    fseek(fp, body_size, SEEK_CUR);
#endif
    }

    return 0;
}

static int demux_parse_moov_box(FILE* fp, uint64_t body_size){
    if(fp == NULL || body_size == 0){
        printf("file_path [%p], body_size[%lu]\n", fp, body_size);
        return -1;
    }

    printf("start parse moov box\n");

    return 0;
}

// 当前媒体文件信息
static int demux_parse_mvhd_box(FILE* fp, uint64_t body_size){
    if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    int read_size = 0;
    uint64_t large_size = 0;
    uint8_t version = 0;
    uint8_t flags[3] = {0};

    if(body_size > 1){
        // full box
        read_size = fread((uint8_t*)&version, sizeof(uint8_t), 1, fp);
        if(read_size < sizeof(uint8_t)){
            printf("read version failed, read_size[%d]\n", read_size);
            return -1;
        }

        read_size = fread(flags, sizeof(uint8_t), sizeof(flags), fp);
        if(read_size < sizeof(flags)){
            printf("read flags failed, read_size[%d]\n", read_size);
            return -1;
        }
        
        if(version == 0){
            mvhd_box_t box;
            memset(&box, 0, sizeof(mvhd_box_t));

            demux_read_big_endian_data(fp, (uint8_t*)&box.creation_time, sizeof(box.creation_time));
            demux_read_big_endian_data(fp, (uint8_t*)&box.modification_time, sizeof(box.modification_time));
            demux_read_big_endian_data(fp, (uint8_t*)&box.timescale, sizeof(box.timescale));
            demux_read_big_endian_data(fp, (uint8_t*)&box.duration, sizeof(box.duration));
            demux_read_big_endian_data(fp, (uint8_t*)&box.preferred_rate, sizeof(box.preferred_rate));
            demux_read_big_endian_data(fp, (uint8_t*)&box.preferred_volume, sizeof(box.preferred_volume));

            printf("#body_size: %lu\n", body_size);
            
            // creation_time为"in seconds since midnight, January 1, 1904" 即 从1904/01/01/00:00:00算起 2082844800
            // utc时间从1970/01/01/00:00:00算起，故 creation_time的utc时间为:creation_time_utc = creation_time - (66年时间差) = creation_time - 2082844800
            box.creation_time = box.creation_time - DEMUX_MVHD_CREATETIME_OFFSET;
            printf("#creation_time: %u\n", box.creation_time);

            box.modification_time = box.modification_time - DEMUX_MVHD_CREATETIME_OFFSET;
            printf("#modification_time: %u\n", box.modification_time);

            printf("#timescale: %u\n", box.timescale);
            printf("#duration: %u\n", box.duration);
            printf("#rate: %u.%u\n", ((box.preferred_rate&0xffff0000) >> 16), (box.preferred_rate&0x0000ffff));
            printf("#volume: %u.%u\n", ((box.preferred_volume&0xff00) >> 8), (box.preferred_volume&0x00ff));

            fseek(fp,( 96 - 22), SEEK_CUR); // 跳过后面的字段
        }else if(version == 1){
            
        }
    }

    return 0;
}

static int demux_parse_trak_box(FILE* fp, uint64_t body_size){
    if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    return 0;
}

static int demux_parse_tkhd_box(FILE* fp, uint64_t body_size){
    if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    int read_size = 0;
    uint64_t large_size = 0;
    uint8_t version = 0;
    uint32_t flags = {0};

    if(body_size > 1){
        // full box
        demux_read_big_endian_data(fp, (uint8_t*)&version, 1);

        demux_read_big_endian_data(fp, (uint8_t*)&flags, 3);

        printf("#version: %u flags:%x\n", version, flags);

        if(version == 0){
            tkhd_box_t box;
            memset(&box, 0, sizeof(tkhd_box_t));
            demux_read_big_endian_data(fp, (uint8_t*)&box.creation_time, sizeof(box.creation_time));
            demux_read_big_endian_data(fp, (uint8_t*)&box.modification_time, sizeof(box.modification_time));
            demux_read_big_endian_data(fp, (uint8_t*)&box.track_id, sizeof(box.track_id));
            demux_read_big_endian_data(fp, (uint8_t*)&box.reserved0, sizeof(box.reserved0));
            demux_read_big_endian_data(fp, (uint8_t*)&box.duration, sizeof(box.duration));
            demux_read_big_endian_data(fp, (uint8_t*)&box.reserved1, sizeof(box.reserved1));
            demux_read_big_endian_data(fp, (uint8_t*)&box.layer, sizeof(box.layer));
            demux_read_big_endian_data(fp, (uint8_t*)&box.alternate_group, sizeof(box.alternate_group));
            demux_read_big_endian_data(fp, (uint8_t*)&box.volume, sizeof(box.volume));
            demux_read_big_endian_data(fp, (uint8_t*)&box.reserved2, sizeof(box.reserved2));
            demux_read_big_endian_data(fp, (uint8_t*)box.matrix, sizeof(box.matrix));
            demux_read_big_endian_data(fp, (uint8_t*)&box.track_width, sizeof(box.track_width));
            demux_read_big_endian_data(fp, (uint8_t*)&box.track_height, sizeof(box.track_height));
            
            printf("#body_size: %lu\n", body_size);
            
            // creation_time为"in seconds since midnight, January 1, 1904" 即 从1904/01/01/00:00:00算起 2082844800
            // utc时间从1970/01/01/00:00:00算起，故 creation_time的utc时间为:creation_time_utc = creation_time - (66年时间差) = creation_time - 2082844800
            box.creation_time = box.creation_time - DEMUX_MVHD_CREATETIME_OFFSET;
            printf("#creation_time: %u\n", box.creation_time);

            box.modification_time = box.modification_time - DEMUX_MVHD_CREATETIME_OFFSET;
            printf("#modification_time: %u\n", box.modification_time);

            printf("#track_id: %u\n", box.track_id);
            printf("#duration: %u\n", box.duration);
            printf("#layer: %u\n", box.layer);
            printf("#alternate_group: %u\n", box.alternate_group);
            printf("#volume: %u.%u\n", ((box.volume&0xff00) >> 8), (box.volume&0x00ff));
            printf("#track_width: %u.%u\n", ((box.track_width&0xffff0000) >> 16), (box.track_width&0x0000ffff));
            printf("#track_height: %u.%u\n", ((box.track_height&0xffff0000) >> 16), (box.track_height&0x0000ffff));
        }else if(version == 1){
                
        }
    }

    return 0;
}

static int demux_parse_edts_box(FILE* fp, uint64_t body_size){
    if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    fseek(fp, body_size, SEEK_CUR);

    return 0;
}

static int demux_parse_mdia_box(FILE* fp, uint64_t body_size){
    if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    return 0;
}

static int demux_parse_mdhd_box(FILE* fp, uint64_t body_size){
    if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    fseek(fp, body_size, SEEK_CUR);

    return 0;
}

static int demux_parse_hdlr_box(FILE* fp, uint64_t body_size){
    if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    int read_size = 0;
    uint64_t large_size = 0;
    uint8_t version = 0;
    uint32_t flags = {0};

    if(body_size > 1){
        // full box
        demux_read_big_endian_data(fp, (uint8_t*)&version, 1);

        demux_read_big_endian_data(fp, (uint8_t*)&flags, 3);

        printf("#version: %u flags:%x\n", version, flags);

        if(version == 0){
            hdlr_box_t box;
            memset(&box, 0, sizeof(hdlr_box_t));
            demux_read_small_endian_data(fp, (uint8_t*)box.component_type, sizeof(box.component_type));
            demux_read_small_endian_data(fp, (uint8_t*)box.component_subtype, sizeof(box.component_type));
            demux_read_big_endian_data(fp, (uint8_t*)&box.component_manufacturer, sizeof(box.component_manufacturer));
            demux_read_big_endian_data(fp, (uint8_t*)&box.component_flags, sizeof(box.component_flags));
            demux_read_big_endian_data(fp, (uint8_t*)&box.component_flags_mask, sizeof(box.component_flags_mask));
            
            uint32_t component_name_len = body_size - 4 - sizeof(hdlr_box_t) + sizeof(box.component_name);
            box.component_name = (uint8_t*)calloc(component_name_len, sizeof(uint8_t));
            if(!box.component_name){
                printf("component_name NUL\n");
                return -1;
            }
            printf("component_name_len:%d\n", component_name_len);
            demux_read_small_endian_data(fp, (uint8_t*)box.component_name, component_name_len);

            printf("#body_size: %lu\n", body_size);
            
            // 打印子串指定长度
            printf("#component_type: %.*s\n", (int)sizeof(box.component_type), box.component_type);
            printf("#component_subtype: %.*s\n", (int)sizeof(box.component_subtype), box.component_subtype);
            printf("#component_name: %.*s\n", (int)component_name_len, box.component_name);

            if(box.component_name){
                free(box.component_name);
            }
        }else if(version == 1){
                
        }
    }


    return 0;
}

static int demux_parse_minf_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    return 0;
}

static int demux_parse_vmhd_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    fseek(fp, body_size, SEEK_CUR);

    return 0;
}

static int demux_parse_dinf_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    return 0;
}

static int demux_parse_dref_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    fseek(fp, body_size, SEEK_CUR);

    return 0;
}

static int demux_parse_stbl_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    return 0;
}

static int demux_parse_stsd_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    uint8_t version = 0;
    uint32_t flags = 0;
    uint32_t entries = {0};

    if(body_size > 1){
        demux_read_big_endian_data(fp, (uint8_t*)&version, 1);
        demux_read_big_endian_data(fp, (uint8_t*)&flags, 3);
        demux_read_big_endian_data(fp, (uint8_t*)&entries, 4);
        printf("#version: %u flags:%x entries:%u\n", version, flags, entries);
    }

    return 0;
}

static int demux_parse_avc1_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    if(body_size > 1){
        avc1_box_t box;
        memset(&box, 0, sizeof(avc1_box_t));

        demux_read_big_endian_data(fp, (uint8_t*)&box.reserved, sizeof(box.reserved));
        demux_read_big_endian_data(fp, (uint8_t*)&box.data_reference_index, sizeof(box.data_reference_index));

        demux_read_big_endian_data(fp, (uint8_t*)&box.version, sizeof(box.version));
        demux_read_big_endian_data(fp, (uint8_t*)&box.revidion_level, sizeof(box.revidion_level));
        demux_read_big_endian_data(fp, (uint8_t*)&box.vendor, sizeof(box.vendor));
        demux_read_big_endian_data(fp, (uint8_t*)&box.temporal_quality, sizeof(box.temporal_quality));
        demux_read_big_endian_data(fp, (uint8_t*)&box.spatial_quality, sizeof(box.spatial_quality));
        demux_read_big_endian_data(fp, (uint8_t*)&box.width, sizeof(box.width));
        demux_read_big_endian_data(fp, (uint8_t*)&box.height, sizeof(box.height));
        demux_read_big_endian_data(fp, (uint8_t*)&box.horizonta_resolution, sizeof(box.horizonta_resolution));
        demux_read_big_endian_data(fp, (uint8_t*)&box.vertical_resolution, sizeof(box.vertical_resolution));
        demux_read_big_endian_data(fp, (uint8_t*)&box.data_size, sizeof(box.data_size));
        demux_read_big_endian_data(fp, (uint8_t*)&box.frame_count, sizeof(box.frame_count));
        demux_read_big_endian_data(fp, (uint8_t*)box.compressor_name, sizeof(box.compressor_name));
        demux_read_big_endian_data(fp, (uint8_t*)&box.depth, sizeof(box.depth));
        demux_read_big_endian_data(fp, (uint8_t*)&box.color_table_id, sizeof(box.color_table_id));

        printf("#avc1_box_t size:%ld, body_size:%lu\n", sizeof(avc1_box_t), body_size);
        printf("#version:%u\n", box.version);
        printf("#revidion_level:%u\n", box.revidion_level);
        printf("#vendor:%u\n", box.vendor);
        printf("#temporal_quality:%u\n", box.temporal_quality);
        printf("#spatial_quality:%u\n", box.spatial_quality);
        printf("#width:%u\n", box.width);
        printf("#height:%u\n", box.height);
        printf("#horizonta_resolution:%u\n", box.horizonta_resolution);
        printf("#vertical_resolution:%u\n", box.vertical_resolution);
    }

    return 0;
}


static int demux_parse_avcC_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    if(body_size > 1){
        int i = 0;
        avcC_box_t box;
        memset(&box, 0, sizeof(avcC_box_t));

        demux_read_big_endian_data(fp, (uint8_t*)&box.configuration_version, sizeof(box.configuration_version));
        demux_read_big_endian_data(fp, (uint8_t*)&box.avc_profile_indication, sizeof(box.avc_profile_indication));
        demux_read_big_endian_data(fp, (uint8_t*)&box.profile_compatibility, sizeof(box.profile_compatibility));
        demux_read_big_endian_data(fp, (uint8_t*)&box.avc_level_indication, sizeof(box.avc_level_indication));
        
        demux_read_big_endian_data(fp, (uint8_t*)&box.length_size_minusOne, sizeof(box.length_size_minusOne));
        box.length_size_minusOne = box.length_size_minusOne & 0x02;
        
        demux_read_big_endian_data(fp, (uint8_t*)&box.num_of_sequence_parameter_sets, sizeof(box.num_of_sequence_parameter_sets));
        box.num_of_sequence_parameter_sets = box.num_of_sequence_parameter_sets & 0x1f;

        demux_read_big_endian_data(fp, (uint8_t*)&box.sequence_parameter_set_length, sizeof(box.sequence_parameter_set_length));
        box.sequence_parameter_set_nal_unit = (uint8_t*)calloc(sizeof(uint8_t), box.sequence_parameter_set_length);
        if(box.sequence_parameter_set_nal_unit)
            demux_read_small_endian_data(fp, (uint8_t*)box.sequence_parameter_set_nal_unit, box.sequence_parameter_set_length);

        demux_read_big_endian_data(fp, (uint8_t*)&box.num_of_picture_parameter_sets, sizeof(box.num_of_picture_parameter_sets));
        demux_read_big_endian_data(fp, (uint8_t*)&box.picture_parameter_set_length, sizeof(box.picture_parameter_set_length));
        box.picture_parameter_set_nal_unit = (uint8_t*)calloc(sizeof(uint8_t), box.picture_parameter_set_length);
        if(box.picture_parameter_set_nal_unit)
            demux_read_small_endian_data(fp, (uint8_t*)box.picture_parameter_set_nal_unit, box.picture_parameter_set_length);

        printf("#num_of_sequence_parameter_sets %u\n", box.num_of_sequence_parameter_sets);
        printf("#sequence_parameter_set_length %u\n", box.sequence_parameter_set_length);
        printf("#sequence_parameter_set_nal_unit:");
        for(i = 0;i < box.sequence_parameter_set_length;i++){
            printf("%x", box.sequence_parameter_set_nal_unit[i]);
        }
        printf("\n");
        video_ctrl.sps = box.sequence_parameter_set_nal_unit;
        video_ctrl.sps_len = box.sequence_parameter_set_length;
        
        printf("#num_of_picture_parameter_sets %u\n", box.num_of_picture_parameter_sets);
        printf("#picture_parameter_set_length %u\n", box.picture_parameter_set_length);
        printf("#picture_parameter_set_nal_unit:");
        for(i = 0;i < box.picture_parameter_set_length;i++){
            printf("%x", box.picture_parameter_set_nal_unit[i]);
        }
        printf("\n");
        video_ctrl.pps = box.picture_parameter_set_nal_unit;
        video_ctrl.pps_len = box.picture_parameter_set_length;
    }

    return 0;
}

static int demux_parse_stts_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }
    fseek(fp, body_size, SEEK_CUR);

    return 0;
}

static int demux_parse_stss_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }
    
    uint8_t version = 0;
    uint32_t flags = 0;
    uint32_t i = 0;

    if(body_size > 1){
        demux_read_big_endian_data(fp, (uint8_t*)&version, 1);
        demux_read_big_endian_data(fp, (uint8_t*)&flags, 3);
        printf("#version: %u flags:%x\n", version, flags);

        demux_read_big_endian_data(fp, (uint8_t*)&video_ctrl.i_frame_count, sizeof(video_ctrl.i_frame_count));
        video_ctrl.i_frame_num_buf = (uint32_t*)calloc(sizeof(uint32_t), video_ctrl.i_frame_count);

        if(video_ctrl.i_frame_num_buf){
            printf("# i_frame_num[%u]:", video_ctrl.i_frame_count);
            for(i = 0;i < video_ctrl.i_frame_count;i++){
                demux_read_big_endian_data(fp, (uint8_t*)&video_ctrl.i_frame_num_buf[i], sizeof(uint32_t));
                printf("%u ", video_ctrl.i_frame_num_buf[i]);
            }
            printf("\n");
        }
    }


    return 0;
}

static int demux_parse_stsc_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    stsc_box_t* stsc_box = (stsc_box_t*)calloc(sizeof(stsc_box_t), video_ctrl.stsc_entry_count);
    stsc_box_t* p_stsc_box = NULL;
    uint8_t version = 0;
    uint32_t flags = 0;
    uint32_t i = 0;

    demux_read_big_endian_data(fp, (uint8_t*)&version, 1);
    demux_read_big_endian_data(fp, (uint8_t*)&flags, 3);
    printf("#version: %u flags:%x\n", version, flags);

    demux_read_big_endian_data(fp, (uint8_t*)&video_ctrl.stsc_entry_count, sizeof(video_ctrl.stsc_entry_count));
    if(stsc_box == NULL){
        printf("stsc_box NULL\n");
        return -1;
    }

    for (i = 0; i < video_ctrl.stsc_entry_count; i++) {
        p_stsc_box = stsc_box+i;
        demux_read_big_endian_data(fp, (uint8_t*)&p_stsc_box->first_chunk, sizeof(p_stsc_box->first_chunk));
        demux_read_big_endian_data(fp, (uint8_t*)&p_stsc_box->samples_per_chunk, sizeof(p_stsc_box->samples_per_chunk));
        demux_read_big_endian_data(fp, (uint8_t*)&p_stsc_box->sample_description_index, sizeof(p_stsc_box->sample_description_index));
    }
    video_ctrl.stsc_box = stsc_box;

    printf("#first_chunk:%u\n", stsc_box->first_chunk);
    printf("#samples_per_chunk:%u\n", stsc_box->samples_per_chunk);
    printf("#sample_description_index:%u\n", stsc_box->sample_description_index);

    return 0;
}

static int demux_parse_stsz_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }
    
    uint8_t version = 0;
    uint32_t flags = 0;
    uint32_t i = 0;

    demux_read_big_endian_data(fp, (uint8_t*)&version, 1);
    demux_read_big_endian_data(fp, (uint8_t*)&flags, 3);
    printf("#version: %u flags:%x\n", version, flags);
    
    demux_read_big_endian_data(fp, (uint8_t*)&video_ctrl.sample_size, sizeof(video_ctrl.sample_size));
    demux_read_big_endian_data(fp, (uint8_t*)&video_ctrl.sample_count, sizeof(video_ctrl.sample_count));

    if(video_ctrl.sample_size == 0){
        video_ctrl.sample_size_buf = (uint32_t*)calloc(sizeof(uint32_t), video_ctrl.sample_count);
        printf("#sample_count:%u\n", video_ctrl.sample_count);
        for (i = 0; i < video_ctrl.sample_count; i++) {
            demux_read_big_endian_data(fp, (uint8_t*)&video_ctrl.sample_size_buf[i], sizeof(uint32_t));
            printf("#sample_size_buf[%u]:%u\n", i, video_ctrl.sample_size_buf[i]);
        }
    }

    return 0;
}

static int demux_output_video_stream(FILE* fp){
    uint32_t cur_offset = ftell(fp);
    char* file_path = "out.h264";
    FILE* out_fp = fopen(file_path, "wb+");
    uint32_t i = 0;

    if(out_fp == NULL){
        printf("out_fp NULL\n");
        return -1;
    }

    /* 获取chunk位置 */
    uint32_t chunk_count = video_ctrl.chunk_count;
    uint32_t* chunk_offset_buf = video_ctrl.chunk_offset_buf;
    uint32_t* sample_size_buf = video_ctrl.sample_size_buf;
    uint32_t sample_count = video_ctrl.sample_count;
    uint8_t* video_data = NULL;
    uint8_t start_code[4] = {0x00, 0x00, 0x00, 0x01};
    for(i = 0;i < video_ctrl.chunk_count;i++){
        /* 读取sample */
        fseek(fp, chunk_offset_buf[i], SEEK_SET);
        video_data = (uint8_t*)calloc(sizeof(uint32_t), sample_size_buf[i]);
        fread(video_data, sizeof(uint8_t), sample_size_buf[i], fp);
        /* 判断是否为关键帧 */
        if(i == 0){
            /* 写入sps pps */
            fwrite(start_code, sizeof(uint8_t), sizeof(start_code), out_fp);
            fwrite(video_ctrl.sps, sizeof(uint8_t), video_ctrl.sps_len, out_fp);
            fwrite(start_code, sizeof(uint8_t), sizeof(start_code), out_fp);
            fwrite(video_ctrl.pps, sizeof(uint8_t), video_ctrl.pps_len, out_fp);
            /* 写入视频数据 */
            fwrite(start_code, sizeof(uint8_t), sizeof(start_code), out_fp);
            fwrite(video_data + 4, sizeof(uint8_t), sample_size_buf[i], out_fp); //前四个字节为数据的长度
        }else{
            /* 写入视频数据 */
            fwrite(start_code, sizeof(uint8_t), sizeof(start_code), out_fp);
            fwrite(video_data + 4, sizeof(uint8_t), sample_size_buf[i], out_fp); //前四个字节为数据的长度
            
        }
        free(video_data);
    }

    fclose(out_fp);
    fseek(fp, cur_offset, SEEK_SET);
}

static int demux_parse_stco_box(FILE* fp, uint64_t body_size){
     if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }
    
    int read_size = 0;
    uint64_t large_size = 0;
    uint8_t version = 0;
    uint32_t flags = {0};

    if(body_size > 1){
        demux_read_big_endian_data(fp, (uint8_t*)&version, 1);
        demux_read_big_endian_data(fp, (uint8_t*)&flags, 3);
        printf("#version: %u flags:%x\n", version, flags);

        demux_read_big_endian_data(fp, (uint8_t*)&video_ctrl.chunk_count, sizeof(video_ctrl.chunk_count));
        if(video_ctrl.chunk_count > 0){
            video_ctrl.chunk_offset_buf = (uint32_t*)calloc(sizeof(uint32_t), video_ctrl.chunk_count);
            if(video_ctrl.chunk_offset_buf){
                int i = 0;
                for(i = 0;i < video_ctrl.chunk_count;i++){
                    demux_read_big_endian_data(fp, (uint8_t*)&video_ctrl.chunk_offset_buf[i], sizeof(uint32_t));
                    printf("chunk_offset_buf[%d]:%u\n", i, video_ctrl.chunk_offset_buf[i]);
                }
            }
        }
        demux_output_video_stream(fp);
    }

    return 0;
}

static int demux_regsistor_box(demux_ctrl_t* demux_ctrl){
    int ret = 0;
    INIT_LIST_HEAD(&demux_ctrl->parse_func_list);

    ret = demux_parse_func_regsistor(demux_ctrl, "ftyp", demux_parse_ftyp_box);
    if(ret < 0){
        printf("regsistor ftyp failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "free", demux_parse_free_box);
    if(ret < 0){
        printf("regsistor free failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "mdat", demux_parse_mdat_box);
    if(ret < 0){
        printf("regsistor mdat failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "moov", demux_parse_moov_box);
    if(ret < 0){
        printf("regsistor moov failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "mvhd", demux_parse_mvhd_box);
    if(ret < 0){
        printf("regsistor mvhd failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "trak", demux_parse_trak_box);
    if(ret < 0){
        printf("regsistor trak failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "tkhd", demux_parse_tkhd_box);
    if(ret < 0){
        printf("regsistor tkhd failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "edts", demux_parse_edts_box);
    if(ret < 0){
        printf("regsistor edts failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "mdia", demux_parse_mdia_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "mdhd", demux_parse_mdhd_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "hdlr", demux_parse_hdlr_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "minf", demux_parse_minf_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "vmhd", demux_parse_vmhd_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "dinf", demux_parse_dinf_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "dref", demux_parse_dref_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "stbl", demux_parse_stbl_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "stsd", demux_parse_stsd_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "avc1", demux_parse_avc1_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "avcC", demux_parse_avcC_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }
    
    ret = demux_parse_func_regsistor(demux_ctrl, "stts", demux_parse_stts_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "stss", demux_parse_stss_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "stsc", demux_parse_stsc_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "stsz", demux_parse_stsz_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }

    ret = demux_parse_func_regsistor(demux_ctrl, "stco", demux_parse_stco_box);
    if(ret < 0){
        printf("regsistor mdia failed\n");
        return -1;
    }


    return 0;
}

int demux_init(demux_ctrl_t* demux_ctrl, char* file_path, int file_path_len){
    int ret = -1;

    if(demux_ctrl == NULL || file_path == NULL){
        printf("demux_ctrl[%p] or file_path[%p] NULL\n", demux_ctrl, file_path);
        return -1;
    }
    
    if(file_path[0] == 0 || file_path_len >= FILE_PATH_MAX_LENGTH){
        printf("file path error [%s] [%d]\n", demux_ctrl->file_path, demux_ctrl->file_path_len);
        return -1;
    }
    strncpy(demux_ctrl->file_path, file_path, (FILE_PATH_MAX_LENGTH - 1));
    demux_ctrl->file_path_len = file_path_len;
    ret = demux_open_file(demux_ctrl->file_path, (FILE**)&demux_ctrl->fp);
    if(ret < 0){
        printf("open file failed\n");
        return -1;
    }

    ret = demux_regsistor_box(demux_ctrl);
    if(ret < 0){
        printf("demux regsistor failed\n");
        return -1;
    }

    pthread_mutex_init(&demux_ctrl->parse_func_lock, NULL);

    printf("init successful\n");
}

static int demux_free_parse_func_info(demux_ctrl_t* demux_ctrl){
    demux_parse_func_info_t* demux_parse_func_info = NULL, *tmp = NULL;

    if(demux_ctrl == NULL){
        return -1;
    }

    pthread_mutex_lock(&demux_ctrl->parse_func_lock);
    list_for_each_entry_safe(demux_parse_func_info, tmp, &demux_ctrl->parse_func_list, node) {
        if(demux_parse_func_info != NULL){
            list_del(&demux_parse_func_info->node);
            free(demux_parse_func_info);
        }
    }
    pthread_mutex_unlock(&demux_ctrl->parse_func_lock);

    return 0;
}

int demux_close(demux_ctrl_t* demux_ctrl){
    if(demux_ctrl == NULL){
        printf("demux_ctrl NULL\n");
        return -1;
    }

    demux_free_parse_func_info(demux_ctrl);

    if(demux_ctrl->fp != NULL){
        fclose((FILE*)demux_ctrl->fp);
        demux_ctrl->fp = NULL;
    }
    pthread_mutex_destroy(&demux_ctrl->parse_func_lock);

    free(demux_ctrl);
    demux_ctrl = NULL;

    printf("demux close success\n");
}

static int demux_read_box_size(FILE* fp, uint64_t* box_size){
    int32_t read_size = 0;
    int32_t ret = 0;

    if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    ret = demux_read_big_endian_data(fp, (uint8_t*)box_size, BOX_SIZE_BYTE);
    if(ret < 0){
        printf("read box size failed\n");
        return -1;
    }

    return 0;
}

int demux_read_a_box_head(FILE* fp, char* box_type, uint64_t* body_size){
    int read_size = 0;
    uint64_t box_size = 0;
    int ret = -1;

    if(fp == NULL){
        printf("file_path NULL\n");
        return -1;
    }

    ret = demux_read_box_size(fp, &box_size);
    if(ret < 0){
        printf("read end\n");
        return -1;
    }

    if(box_size == 0){
        /* large size */
        printf("the large size\n");

        read_size = fread(&box_size, 1, BOX_LARGE_SZIE_BYTE, fp);
        if(read_size < BOX_LARGE_SZIE_BYTE){
            printf("read box type failed, read_size[%d]\n", read_size);
            return -1;
        }
        *body_size = box_size - BOX_HEAD_BYTE;

        read_size = fread(box_type, 1, BOX_TYPE_BYTE, fp);
        if(read_size < BOX_TYPE_BYTE){
            printf("read box type failed, read_size[%d]\n", read_size);
            return -1;
        }

        // TODO
    }else if(box_size == 1){
        /* the last box */
        printf("the last box\n");
        return -1;
        // TODO
    }else if(box_size >= BOX_HEAD_BYTE){
        /* normal status */
        read_size = fread(box_type, 1, BOX_TYPE_BYTE, fp);
        if(read_size < BOX_TYPE_BYTE){
            printf("read box type failed, read_size[%d]\n", read_size);
            return -1;
        }

        *body_size = box_size - BOX_HEAD_BYTE;
    }else{
        /* error status*/
        // TODO
    }

    printf("\n##### box_type:%s #####\n\n", box_type);
    printf("%s box_size[%lu]\n", box_type, box_size);

    return 0;
}

static DEMUX_BOX_PARSE demux_get_parse_func(demux_ctrl_t* demux_ctrl, char* box_type){
    demux_parse_func_info_t* demux_parse_func_info = NULL, *tmp = NULL;
    DEMUX_BOX_PARSE func = NULL;
    
    pthread_mutex_lock(&demux_ctrl->parse_func_lock);
    list_for_each_entry_safe(demux_parse_func_info, tmp, &demux_ctrl->parse_func_list, node) {
        if (0 == strcmp(box_type, demux_parse_func_info->box_type)) {
            func = demux_parse_func_info->func;
            break;
        }
    }
    pthread_mutex_unlock(&demux_ctrl->parse_func_lock);

    return func;
}

int demux_handle_box_body(demux_ctrl_t* demux_ctrl){
    int ret = -1;
    char box_type[4 + 1] = {0};
    uint64_t body_size = 0;
    DEMUX_BOX_PARSE demux_parse_box_func = NULL;

    // 读一个box head
    ret = demux_read_a_box_head((FILE*)demux_ctrl->fp, box_type, &body_size);
    if(ret < 0){
        printf("read a box failed [%d]\n", ret);
        return -1;
    }

    // 获取处理box body的方法
    demux_parse_box_func = demux_get_parse_func(demux_ctrl, box_type);
    if(demux_parse_box_func == NULL){
        printf("get %s func error\n", box_type);
        return -1;
    }

    // 解析body
    ret = demux_parse_box_func((FILE*)demux_ctrl->fp, body_size);
    if(ret < 0){
        printf("demux_parse_box_func error %d\n", ret);
        return -1;
    }

    printf("\n###############################\n");

    return 0;
}
