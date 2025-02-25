#ifndef _AUDIO_DVB_ES_H_
#define _AUDIO_DVB_ES_H_
#define VALID_PID(_pid_) ((_pid_)>0 && (_pid_)<0x1fff)
#define VALID_AD_FMT(fmt)  ((fmt == ACODEC_FMT_EAC3) || (fmt == ACODEC_FMT_AC3) || \
    (fmt == ACODEC_FMT_MPEG) || (fmt == ACODEC_FMT_MPEG1) || \
    (fmt == ACODEC_FMT_MPEG2) || (fmt == ACODEC_FMT_AAC) || (fmt == ACODEC_FMT_AAC_LATM))

#define VALID_AD_FMT_UK(fmt)  ((fmt == ACODEC_FMT_MPEG) || (fmt == ACODEC_FMT_MPEG1) || \
    (fmt == ACODEC_FMT_MPEG2) || (fmt == ACODEC_FMT_AAC) || (fmt == ACODEC_FMT_AAC_LATM))

#define  DVB_AUDIO_ES_PTS_VALID 0x02
#define  DVB_AUDIO_ES_PTS_INVALID 0x04

typedef enum
{
    AM_AUDIO_Dmx_SUCCESS,
    AM_AUDIO_Dmx_ERROR,
    AM_AUDIO_Dmx_DEVOPENFAIL,
    AM_AUDIO_Dmx_SETSOURCEFAIL,
    AM_AUDIO_Dmx_NOT_SUPPORTED,
    AM_AUDIO_Dmx_CANNOT_OPEN_FILE,
    AM_AUDIO_Dmx_ERR_SYS,
    AM_AUDIO_Dmx_MAX,
} AM_Dmx_Audio_ErrorCode_t;

struct mAudioEsDataInfo {
    uint8_t *data;
    int size;
    int64_t pts;
    uint8_t pts_dts_flag;
    int used_size;
    uint8_t adfade;
    uint8_t adpan;
};
AM_Dmx_Audio_ErrorCode_t Open_Dmx_Audio (void **demux_handle, int  demux_id, int security_mem_level);
AM_Dmx_Audio_ErrorCode_t Init_Dmx_Main_Audio(void *demux_handle, int fmt, int pid);
AM_Dmx_Audio_ErrorCode_t Stop_Dmx_Main_Audio(void *demux_handle);
AM_Dmx_Audio_ErrorCode_t Start_Dmx_Main_Audio(void *demux_handle);
AM_Dmx_Audio_ErrorCode_t Destroy_Dmx_Main_Audio(void *demux_handle);
AM_Dmx_Audio_ErrorCode_t Init_Dmx_AD_Audio(void *demux_handle, int fmt, int pid, int pesmode);
AM_Dmx_Audio_ErrorCode_t Stop_Dmx_AD_Audio(void *demux_handle);
AM_Dmx_Audio_ErrorCode_t Start_Dmx_AD_Audio(void *demux_handle);
AM_Dmx_Audio_ErrorCode_t Destroy_Dmx_AD_Audio(void *demux_handle);
AM_Dmx_Audio_ErrorCode_t Close_Dmx_Audio(void *demux_handle);
AM_Dmx_Audio_ErrorCode_t Get_MainAudio_Es(void *demux_handle, struct mAudioEsDataInfo  **mAudioEsData);
AM_Dmx_Audio_ErrorCode_t Get_ADAudio_Es(void *demux_handle, struct mAudioEsDataInfo  **mAudioEsData);
AM_Dmx_Audio_ErrorCode_t Get_Audio_LastES_Apts(void *demux_handle , int64_t *last_queue_es_apts);
AM_Dmx_Audio_ErrorCode_t Flush_Dmx_Audio(void *demux_handle);
#endif
