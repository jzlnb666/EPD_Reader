#include <rtthread.h>
#include "epd_configs.h"
#include "mem_section.h"
#include "string.h"
#ifdef LCD_USING_EPD_CUSTOM
#include "mem_map.h"
#ifndef CUSTOM_EPD_WAVE_TABLE_START_ADDR
#error "CUSTOM_EPD_WAVE_TABLE_START_ADDR is not defined!!!"
#endif

/*
  8bit lookup table format description
  
  length: 64 bytes for each lookup table.

  input:   8bit (high 4 bits: old data, low 4 bits: new data).
  
  ouput 2bit:  and 4 output bits saved in 1 byte, 

               for example, the first byte is for pixel0, pixel1, pixel2, pixel3:
                bit7-6 for pixel0, 
                bit5-4 for pixel1, 
                bit3-2 for pixel2, 
                bit1-0 for pixel3.
*/
typedef struct
{
    int min_temp;          
    int max_temp;        
    uint32_t frame_count;   
    uint32_t wave_table_offset;  
} EpdWaveTableEntry;

typedef struct
{
    uint32_t magic_number;   //magic number for validation, 0xE9D7AB1E
    uint32_t version_num;    //version number for future extension, 0x00000001

    uint32_t full_table_entry_count;   //number of entries in full update table
    uint32_t full_table_offset;        //offset to the full update table

    uint32_t partial_table_entry_count; //number of entries in partial update table
    uint32_t partial_table_offset;      //offset to the partial update table

} EpdWaveTableIndex;




#define PART_DISP_TIMES       10        // After PART_DISP_TIMES-1 partial refreshes, perform a full refresh once
static int reflesh_times = 0;


static const uint8_t *p_current_wave_from = NULL;
static const EpdWaveTableIndex *p_wave_table_index = NULL;

#define GET_ABSOLUTE_ADDR(offset) ((const void *)((uint32_t)CUSTOM_EPD_WAVE_TABLE_START_ADDR + (offset)))

void epd_wave_table(void)
{
    p_wave_table_index = GET_ABSOLUTE_ADDR(0);

    //Validate the wave table index data
    if (p_wave_table_index->magic_number != 0xE9D7AB1E || p_wave_table_index->version_num != 0x00000001) {
        rt_kprintf("Invalid wave table index data!, magic:0x%08X, version:0x%08X\n", 
                p_wave_table_index->magic_number, p_wave_table_index->version_num);
        p_wave_table_index = NULL;
        return;
    }

    //Vailidate the full table offset
    if(p_wave_table_index->full_table_offset > CUSTOM_EPD_WAVE_TABLE_SIZE)
    {
        rt_kprintf("Invalid full_table_offset, 0x%08X \n", p_wave_table_index->full_table_offset);
    }
    else
    {
        //Validate the offset of each table entry
        const EpdWaveTableEntry *selected_table = GET_ABSOLUTE_ADDR(p_wave_table_index->full_table_offset);;
        uint32_t selected_table_entry_count = p_wave_table_index->full_table_entry_count;
        for (size_t i = 0; i < selected_table_entry_count; i++) {
            if (selected_table[i].wave_table_offset > CUSTOM_EPD_WAVE_TABLE_SIZE) {
                
                rt_kprintf("Invalid wave_table_offset in full update table, index %d, 0x%08X \n", i, selected_table[i].wave_table_offset);
                p_wave_table_index = NULL;
                return;
            }
        }
    }

    //Vailidate the partial table offset
    if(p_wave_table_index->partial_table_offset > CUSTOM_EPD_WAVE_TABLE_SIZE)
    {
        rt_kprintf("Invalid partial_table_offset, 0x%08X \n", p_wave_table_index->partial_table_offset);
    }
    else
    {
        //Validate the offset of each table entry
        const EpdWaveTableEntry *selected_table = GET_ABSOLUTE_ADDR(p_wave_table_index->partial_table_offset);;
        uint32_t selected_table_entry_count = p_wave_table_index->partial_table_entry_count;
        for (size_t i = 0; i < selected_table_entry_count; i++) {
            if (selected_table[i].wave_table_offset > CUSTOM_EPD_WAVE_TABLE_SIZE) {
                
                rt_kprintf("Invalid wave_table_offset in partial update table, index %d, 0x%08X \n", i, selected_table[i].wave_table_offset);
                p_wave_table_index = NULL;
                return;
            }
        }
    }


    rt_kprintf("Custom EPD wave table validation OK, full entries: %d, partial entries: %d\n", 
            p_wave_table_index->full_table_entry_count, p_wave_table_index->partial_table_entry_count);
}

uint32_t epd_wave_table_get_frames(int temperature, EpdDrawMode mode)
{
    if (p_wave_table_index == NULL) {
        return 0;
    }


    const EpdWaveTableEntry *selected_table = NULL;
    uint32_t selected_table_entry_count = 0;

    if (reflesh_times % PART_DISP_TIMES == 0) {

        selected_table = GET_ABSOLUTE_ADDR(p_wave_table_index->full_table_offset);
        selected_table_entry_count = p_wave_table_index->full_table_entry_count;
        reflesh_times = 0;
    } else {
        selected_table = GET_ABSOLUTE_ADDR(p_wave_table_index->partial_table_offset);
        selected_table_entry_count = p_wave_table_index->partial_table_entry_count;
    }
    reflesh_times++;
    

    // Find the interval corresponding to the temperature
    for (size_t i = 0; i < selected_table_entry_count; i++) {
        if (temperature >= selected_table[i].min_temp && temperature < selected_table[i].max_temp) {
            p_current_wave_from = GET_ABSOLUTE_ADDR(selected_table[i].wave_table_offset);
            return selected_table[i].frame_count;
        }
    }

    p_current_wave_from = GET_ABSOLUTE_ADDR(selected_table[0].wave_table_offset);
    return selected_table[0].frame_count;
}

void epd_wave_table_fill_lut(uint32_t *p_epic_lut, uint32_t frame_num)
{
    if(p_current_wave_from)
    {
        const uint8_t *p_frame_wave = p_current_wave_from + (frame_num * 64);
    
        //Convert the 8-bit waveforms to 32-bit epic LUT values
        for (uint16_t i = 0; i < 64; i++)
        {
            uint8_t v = p_frame_wave[i];
            uint8_t idx = i << 2;
            p_epic_lut[idx]   = (v & 0xC0) >> 3; //pixel0, EPIC LUT bit4~3 is bit1~0 of RGB565
            p_epic_lut[idx+1] = (v & 0x30) >> 1; //pixel1
            p_epic_lut[idx+2] = (v & 0x0C) << 1; //pixel2
            p_epic_lut[idx+3] = (v & 0x03) << 3; //pixel3
        }
    }
}


uint16_t epd_get_vcom_voltage(void)
{
    return 2100;
}


const EPD_TimingConfig *epd_get_timing_config(void)
{
    static const EPD_TimingConfig timing_config = {
        .sclk_freq = 24,
        .SDMODE = 1,
        .LSL = 0,
        .LBL = 0,
        .LDL = LCD_HOR_RES_MAX/4,
        .LEL = 1,

        .fclk_freq = 83,
        .FSL = 1,
        .FBL = 3,
        .FDL = LCD_VER_RES_MAX,
        .FEL = 5,
    };

    return &timing_config;
}

#endif /*LCD_USING_EPD_CUSTOM*/