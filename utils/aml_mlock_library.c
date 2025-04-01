#define LOG_TAG "audio_hw_utils_mlock"
//#define LOG_NDEBUG 0

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include "aml_malloc_debug.h"
#include "aml_mlock_library.h"


#define VM_MAX_FILEPATH_LEN  136
#define VM_MAX_RANGE_NUM     8

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif


typedef struct _aml_vm_range {
    unsigned long start; // 4 bytes on 32 bit platform; 8 bytes on 64 bit platform
    unsigned long end;
} aml_vm_range;

typedef struct _aml_vm_item {
    bool b_anon_bss;
    aml_vm_range vm_range[VM_MAX_RANGE_NUM];
    char filepath[VM_MAX_FILEPATH_LEN];
} aml_vm_item;

typedef struct _aml_vm_lock_item {
    aml_vm_range *p_range;
    int range_num;
    char *p_filepath;
} aml_vm_lock_item;


static const char *g_hal_common_so[] = {
    "/vendor/lib/hw/android.hardware.audio@7.1-impl.so",
    "/vendor/lib/android.hardware.audio@7.0.so",
    "/vendor/lib/android.hardware.audio@7.0-util.so",
    "/vendor/lib/android.hardware.audio@7.1.so",
    "/vendor/lib/android.hardware.audio@7.1-util.so",

    "/vendor/lib/hw/audio.primary.amlogic.so",
    "/vendor/lib/libamaudioutils.so",
    "/vendor/lib/libamlspeed.so",
    "/vendor/lib/libsonic_ext.so",
    "/vendor/lib/libamltinyalsa.so",
    NULL,
};

static const char *g_hal_tv_effect_so[] = {
    "/vendor/lib/soundfx/libhpeqwrapper.so",
    NULL,
};

static const char *g_hal_ms12_so[] = {
    "/vendor/lib/libms12api_v24.so",
    NULL,
};


static bool aml_parse_vm_line(const char *buf, aml_vm_item *p_item)
{
    int ret = 0;
    unsigned long vm_start;
    unsigned long vm_end;
    char vm_flags[24];
    char vm_pgoff[24];
    char vm_dev_id[24];
    char vm_node_id[24];
    char filepath[VM_MAX_FILEPATH_LEN];

    ret = sscanf(buf, "%lx-%lx %s %s %s %s %s", &vm_start, &vm_end, vm_flags, vm_pgoff, vm_dev_id, vm_node_id, filepath);

    if (ret >= 7) {
        filepath[sizeof(filepath)-1] = 0;
        p_item->filepath[sizeof(filepath)-1] = 0;

        if (strncmp(filepath, "[anon:.bss]", 11) == 0) {
            p_item->b_anon_bss = true;
        } else  if ((filepath[0] != '/') || (strncmp(filepath, "/dev/", 5) == 0)) {
            ALOGV("invalid filepath %s, skip it", filepath);
            return false;
        }

        //ALOGI("ret %d, %x %x, %s\n\n", ret, vm_start, vm_end, filepath);
        p_item->vm_range[0].start = vm_start;
        p_item->vm_range[0].end = vm_end;
        strncpy(p_item->filepath, filepath, sizeof(p_item->filepath)-1);
        return true;
    }
    return false;
}

static bool aml_parse_proc_maps(const char *map_path, aml_vm_item *vm_table, int vm_table_num)
{
    char line[384];
    aml_vm_item *p_item = NULL;
    FILE *fp = NULL;
    const char *last_so_filepath = NULL;

    fp = fopen(map_path, "r");
    if (fp == NULL) {
        printf("open %s failed\n", map_path);
        return false;
    }

    /* For example :
     *
        97890000-97892000 r--p 00000000 fd:01 2568413    /vendor/lib/libalsautils.so
        97892000-97894000 r-xp 00001000 fd:01 2568413    /vendor/lib/libalsautils.so
        97894000-97896000 r--p 00002000 fd:01 2568413    /vendor/lib/libalsautils.so
        978c9000-97938000 r--p 00000000 fd:08 81         /vendor/lib/hw/audio.primary.amlogic.so
        97938000-979f8000 r-xp 0006e000 fd:08 81         /vendor/lib/hw/audio.primary.amlogic.so
        979f8000-979fc000 r--p 0012d000 fd:08 81         /vendor/lib/hw/audio.primary.amlogic.so
        979fc000-979fd000 rw-p 00130000 fd:08 81         /vendor/lib/hw/audio.primary.amlogic.so
        979fd000-979ff000 rw-p 00000000 00:00 0          [anon:.bss]
        97a0f000-97a19000 r--p 00000000 fd:01 2734099    /vendor/lib/libaudiofoundation.so
     *
     *  for /vendor/lib/hw/audio.primary.amlogic.so : from 978c9000 to 979ff000 will be locked(with bss segment)
    */

    while (!feof(fp)) {
        fgets(line, sizeof(line)-1, fp);
        line[sizeof(line)-1] = 0;
        //ALOGI("%s : %s", __func__, line);
        aml_vm_item vm_item;

        memset(&vm_item, 0, sizeof(vm_item));
        if (aml_parse_vm_line(line, &vm_item) != true) {
            last_so_filepath = NULL;
            continue;
        }
        if (strlen(vm_item.filepath) <= 0) {
            last_so_filepath = NULL;
            continue;
        }

        int index = 0;
        int found_index = -1;
        int empty_index = -1;
        while (index < vm_table_num) {
            p_item = &vm_table[index];
            if (strlen(p_item->filepath) > 0) {
                const char *item_filepath = vm_item.filepath;
                if (vm_item.b_anon_bss && last_so_filepath) {
                    item_filepath = last_so_filepath;
                }
                if (strcmp(p_item->filepath, item_filepath) == 0) {
                    found_index = index;
                    break;
                }
            } else if (empty_index < 0) {
                empty_index = index;
            }
            index += 1;
        }

        if (found_index >= 0) {
            p_item = &vm_table[found_index];
            //ALOGI("%s : found_index %d\n", vm_item.filepath, found_index);
        } else if (empty_index >= 0) {
            p_item = &vm_table[empty_index];
            //ALOGI("%s : empty_index %d\n", vm_item.filepath, empty_index);
            strncpy(p_item->filepath, vm_item.filepath, sizeof(vm_item.filepath));
        } else {
            ALOGE("%s can not store this vm item %s\n", __func__, vm_item.filepath);
            last_so_filepath = NULL;
            continue;
        }
        last_so_filepath = p_item->filepath;

        int j = 0;
        while (j < VM_MAX_RANGE_NUM) {
            aml_vm_range *p_range = &p_item->vm_range[j];
            if (p_range->start == 0) {
                p_range->start = vm_item.vm_range[0].start;
                p_range->end = vm_item.vm_range[0].end;
                break;
            }
            j += 1;
        }
    }
    fclose(fp);

    return true;
}

static bool check_mlock_path(const char *path, const char *libs_path[])
{
    int i = 0;
    bool should_lock = false;

    while (libs_path[i] != NULL) {
        if (strcmp(path, libs_path[i]) == 0) {
            should_lock = true;
            break;
        }
        i += 1;
    }
    return should_lock;
}

static aml_vm_lock_item *aml_alloc_vm_lock_item(aml_vm_item *p_item)
{
    int i = 0;
    int j = 0;
    int range_num = 0;
    char info_buf[256];
    int offset = 0;
    int bytes = 0;
    int path_len = 0;
    aml_vm_range *p_src_range = NULL;
    aml_vm_range *p_dst_range = NULL;
    aml_vm_range temp_vm_range[VM_MAX_RANGE_NUM];

    if (p_item == NULL) {
        return NULL;
    }

    aml_vm_lock_item *p_lock_item = aml_audio_calloc(1, sizeof(aml_vm_lock_item));
    if (p_lock_item == NULL) {
        ALOGE("%s calloc %zu bytes fail !", __func__, sizeof(aml_vm_lock_item));
        return NULL;
    }
    memset(temp_vm_range, 0, sizeof(temp_vm_range));

    path_len = strlen(p_item->filepath) + 1;
    char *filepath = aml_audio_calloc(1, path_len);
    if (filepath == NULL) {
        ALOGE("%s calloc %d bytes fail !", __func__, path_len);
        aml_audio_free(p_lock_item);
        p_lock_item = NULL;
        return NULL;
    }
    strcpy(filepath, p_item->filepath);
    p_lock_item->p_filepath = filepath;

    // Try to merge the vm address ranges
    i = 0;
    offset = 0;
    memset(info_buf, 0, sizeof(info_buf));
    while (i < VM_MAX_RANGE_NUM && j < VM_MAX_RANGE_NUM-1) {
        p_src_range = &p_item->vm_range[i];
        if (p_src_range->start != 0) {
            if (sizeof(info_buf)-offset-1 > 0) {
                bytes = snprintf(info_buf+offset, sizeof(info_buf)-offset-1, " %lx-%lx", p_src_range->start, p_src_range->end);
                offset += bytes;
            }

            if (temp_vm_range[j].start == 0) {
                temp_vm_range[j].start = p_src_range->start;
                temp_vm_range[j].end = p_src_range->end;
            } else if (p_src_range->start <= temp_vm_range[j].end) {
                temp_vm_range[j].end = p_src_range->end;
            } else {
                j = j + 1;
                temp_vm_range[j].start = p_src_range->start;
                temp_vm_range[j].end = p_src_range->end;
            }
        } else {
            break;
        }
        i += 1;
    }
    range_num = j+1;

    p_src_range = &temp_vm_range[0];
    p_dst_range = aml_audio_calloc(range_num, sizeof(aml_vm_range));
    if (p_dst_range == NULL) {
        aml_audio_free(p_lock_item);
        aml_audio_free(filepath);
        p_lock_item = NULL;
        filepath = NULL;
        ALOGE("%s calloc %zu bytes fail !", __func__, range_num * sizeof(aml_vm_range));
        return NULL;
    }
    memcpy(p_dst_range, p_src_range, range_num*sizeof(aml_vm_range));
    p_lock_item->p_range = p_dst_range;
    p_lock_item->range_num = range_num;

    ALOGI("%s : %s %s", __func__, p_lock_item->p_filepath, info_buf);
    return p_lock_item;
}

static bool enable_mlock_specific_lib(void)
{
    char buf[PROPERTY_VALUE_MAX] = {'\0'};
    int ret = 0;

    ret = property_get("persist.vendor.audio.service.mlock", buf, NULL);
    ALOGI("%s : persist.vendor.audio.service.mlock %s", __func__, buf);
    if (ret > 0) {
        if (strcasecmp(buf, "false") == 0 || strcmp(buf, "0") == 0) {
            return true;
        }
    }

    // By default mlock all the audiohal library since android S.
    // Not need to mlock specific library if audiohal has mlocked all.
    return false;
}


static aml_vm_lock_item **aml_mlock_table = NULL;

void aml_load_lock_lib_address(bool is_ms12, bool is_stb)
{
    bool ret = false;
    int mlock_so_num = 0;
    const int MAX_VM_NUM = 320;
    aml_vm_item *p_item = NULL;
    aml_vm_item *map_vm_table = NULL;

    if (enable_mlock_specific_lib() == false) {
        ALOGI("%s mlock_specific_lib is disabled", __func__);
        return;
    }

    if (aml_mlock_table != NULL) {
        ALOGI("%s aml_mlock_table %p, already load !", __func__, aml_mlock_table);
        return;
    }

    map_vm_table = aml_audio_calloc(MAX_VM_NUM, sizeof(aml_vm_item));
    if (map_vm_table == NULL) {
        ALOGE("%s %d : calloc %zu bytes fail !", __func__, __LINE__, MAX_VM_NUM * sizeof(aml_vm_item));
        return;
    }
    ret = aml_parse_proc_maps("/proc/self/maps", map_vm_table, MAX_VM_NUM);
    if (ret != true) {
        ALOGE("%s %d : aml_parse_proc_maps /proc/self/maps fail !", __func__, __LINE__);
        return;
    }

    mlock_so_num  = (ARRAY_SIZE(g_hal_common_so) - 1);
    if (!is_stb) {
        mlock_so_num += (ARRAY_SIZE(g_hal_tv_effect_so) - 1);
    }
    if (is_ms12) {
        mlock_so_num += (ARRAY_SIZE(g_hal_ms12_so) - 1);
    }
    ALOGI("%s mlock_so_num %d", __func__, mlock_so_num);

    aml_mlock_table = aml_audio_calloc(mlock_so_num, sizeof(aml_vm_lock_item *));
    if (aml_mlock_table == NULL) {
        ALOGE("%s %d : calloc %zu bytes fail !", __func__, __LINE__, mlock_so_num * sizeof(aml_vm_lock_item *));
        aml_audio_free(map_vm_table);
        map_vm_table = NULL;
        return;
    }

    int index = 0;
    int empty_index = 0;
    bool should_lock = false;

    while (index < MAX_VM_NUM) {
        p_item = &map_vm_table[index];
        //if (strlen(p_item->filepath) > 0)
        //    AM_LOGI("%d %s", index, p_item->filepath);

        should_lock = check_mlock_path(p_item->filepath, g_hal_common_so);
        if (!should_lock && is_ms12) {
            should_lock = check_mlock_path(p_item->filepath, g_hal_ms12_so);
        }
        if (!should_lock && !is_stb) {
            should_lock = check_mlock_path(p_item->filepath, g_hal_tv_effect_so);
        }

        if (should_lock) {
            if (empty_index < mlock_so_num-1) {
                aml_mlock_table[empty_index] = aml_alloc_vm_lock_item(p_item);
            }
            empty_index++;
        }
        index += 1;
    }

    aml_audio_free(map_vm_table);
    map_vm_table = NULL;
}


static int _aml_audio_do_so_page_fault(const int *vm_start, const int *vm_end)
{
    int summary = 0;
    const int *addr = NULL;

    for (addr = vm_start; addr < vm_end; addr += 512) {
        summary += *addr;
    }
    ALOGI("so sample summary : %d", summary);
    return summary;
}

static int aml_mlock_set_thread_sched_priority(char *pName, pthread_t threadId, int sched_priority)
{
    struct sched_param  params = {0};
    int                 ret = 0;
    int                 policy = SCHED_FIFO; /* value:1 [pthread.h] */
    params.sched_priority = sched_priority;
    ret = pthread_setschedparam(threadId, SCHED_FIFO, &params);
    if (ret != 0) {
        ALOGW("[%s:%d] set scheduled param error, ret:%#x", __func__, __LINE__, ret);
    }
    ret = pthread_getschedparam(threadId, &policy, &params);
    ALOGD("[%s:%d] thread:%s set priority, ret:%d policy:%d priority:%d",
        __func__, __LINE__, pName, ret, policy, params.sched_priority);
    return ret;
}

static void _aml_lock_lib_address(bool is_lock)
{
    int i = 0;
    int j = 0;
    int ret = 0;
    uint32_t length = 0;
    uint32_t total_length = 0;
    aml_vm_range *p_range = NULL;
    aml_vm_lock_item *p_lock_item = NULL;

    char info_buf[256];
    int offset = 0;
    int bytes = 0;

    if (enable_mlock_specific_lib() == false) {
        ALOGI("%s mlock_specific_lib is disabled", __func__);
        return;
    }

    if (aml_mlock_table == NULL) {
        ALOGE("%s %d : mlock_table is NULL!", __func__, __LINE__);
        return;
    }

    while (aml_mlock_table[i] != NULL) {
        p_lock_item = aml_mlock_table[i];
        i += 1;

        j = 0;
        offset = 0;
        memset(info_buf, 0, sizeof(info_buf));
        while (j < p_lock_item->range_num) {
            p_range = &p_lock_item->p_range[j];
            if ((p_range->start != 0) && (p_range->end > p_range->start)) {
                length = p_range->end - p_range->start;
                if (sizeof(info_buf)-offset-1 > 0) {
                    bytes = snprintf(info_buf+offset, sizeof(info_buf)-offset-1, " %lx-%lx", p_range->start, p_range->end);
                    offset += bytes;
                }

                if (is_lock) {
                    ret = mlock((const void *)p_range->start, length);
                    _aml_audio_do_so_page_fault((const int *)p_range->start, (const int *)p_range->end);
                } else {
                    ret = munlock((const void *)p_range->start, length);
                }
                total_length += length;
                if (ret != 0) {
                    ALOGE("%s : %s %s %lx-%lx fail (%s) !", __func__, is_lock ? "lock" : "unlock",
                        p_lock_item->p_filepath, p_range->start, p_range->end, strerror(errno));
                }
            }
            j += 1;
        }
        ALOGI("%s : %s %-50s %s", __func__, is_lock ? "lock" : "unlock", p_lock_item->p_filepath, info_buf);
    }
    ALOGI("%s total %s %d KByte", __func__, is_lock ? "lock" : "unlock", total_length/1024);
}

/*
 * If mlock thread is RT, kernel will not optimise these memory.
*/
static void* aml_mlock_rt_thread(void *arg)
{
    int ret = 0;
    bool enable = false;

    if (arg == NULL) {
        ALOGE("%s arg is NULL", __func__);
        return NULL;
    }
    enable = (*(int *)arg != 0);

    ret = aml_mlock_set_thread_sched_priority("audio_mlock_task", pthread_self(), 3);
    if (ret != 0) {
        ALOGE("%s aml_mlock_set_thread_sched_priority fail", __func__);
    }

    // 1ms to wait kernel get rt parameters ?
    usleep(1*1000);

    _aml_lock_lib_address(enable);
    return NULL;
}


void aml_lock_lib_address()
{
    int ret = 0;
    pthread_t threadId = 0;
    int enable = true;

    ret = pthread_create(&threadId, NULL, &aml_mlock_rt_thread, &enable);
    if (ret != 0 || threadId == 0) {
        ALOGE("%s create aml_mlock_rt_thread failed !", __func__);
        _aml_lock_lib_address(true);
    } else {
        ALOGI("%s create aml_mlock_rt_thread success !", __func__);
        pthread_join(threadId, NULL);
        ALOGI("%s aml_mlock_rt_thread job finished !", __func__);
    }
}

void aml_unlock_lib_address(void)
{
    _aml_lock_lib_address(false);
}
