
#include "epd_configs.h"

typedef enum{
    EPD_WAVEFORM_ERR_OK = 0,
    EPD_WAVEFORM_ERR_INVALID_MAGIC_NUMBER = -1,
    EPD_WAVEFORM_ERR_INVALID_VERSION = -2,
    EPD_WAVEFORM_ERR_INVALID_FULL_TABLE_OFFSET = -3,

    EPD_WAVEFORM_ERR_INVALID_PARTIAL_TABLE_OFFSET = -4,
}EPD_WAVEFORM_ERR_TYPEDEF;


int waveform_bin_reader_init(uint32_t addr, uint32_t max_size);
uint32_t waveform_bin_reader_get_frames(int temperature, EpdDrawMode mode);
void waveform_bin_reader_fill_lut(uint32_t *p_epic_lut, uint32_t frame_num);