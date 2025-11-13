#include "battery/ADCBattery.h"
#include "Battery.h"
#include "boards/controls/TouchControls.h"
#include <math.h>
extern "C" {
#include "rtdevice.h"
#include "board.h"
}

static void chg_status_irq_callback(void* args)
{
    ADCBattery* battery = static_cast<ADCBattery*>(args);
    if (battery && battery->ui_queue) 
    {
        // 发送刷新充电状态的消息
        UIAction msg = MSG_UPDATE_CHARGE_STATUS;
        rt_mq_send(battery->ui_queue, &msg, sizeof(UIAction));
        rt_kprintf("charge status changed\n");

    }
}
ADCBattery::ADCBattery(rt_mq_t ui_queue)
{
    this->ui_queue = ui_queue;  
    s_adc_dev = rt_device_find("bat1");
    channel = 7;
    low_power = 0; // 初始化low_power为0
    if (s_adc_dev)
    {
        rt_adc_enable((rt_adc_device_t)s_adc_dev, channel);
    }
    battery_check_timer = RT_NULL;

     // 启用中断
    rt_pin_mode(CHG_STATUS, PIN_MODE_INPUT_PULLUP);
    rt_pin_attach_irq(CHG_STATUS, PIN_IRQ_MODE_RISING_FALLING, chg_status_irq_callback, this);
    rt_pin_irq_enable(CHG_STATUS, PIN_IRQ_ENABLE);
}



void ADCBattery::start_battery_monitor()
{
    battery_check_timer = rt_timer_create("battery_check", 
                                        battery_check_callback, 
                                        this,  
                                        rt_tick_from_millisecond(10000), // 10秒
                                        RT_TIMER_FLAG_PERIODIC | RT_TIMER_FLAG_SOFT_TIMER);

    if (battery_check_timer != RT_NULL) 
    {
        rt_timer_start(battery_check_timer);
        rt_kprintf("[ADCBattery] Battery check timer started\n");
    } 
    else 
    {
        rt_kprintf("[ADCBattery] Failed to create battery check timer\n");
    }
}
void ADCBattery::setup()
{
    rt_kprintf("[ADCBattery] setup completed\n");
    start_battery_monitor();
}
float ADCBattery::get_voltage()
{
    if (!s_adc_dev) {
        rt_kprintf("[ADCBattery] get_voltage: s_adc_dev is NULL\n");
        return 0;
    }
    rt_uint32_t value = rt_adc_read((rt_adc_device_t)s_adc_dev, channel);
    float voltage = value / 10.0f; 
    return voltage;
}

int ADCBattery::get_percentage()
{
    auto voltage = get_voltage() / 1000.0f;
    if (voltage >= 4.20)
    {
        return 100;
    }
    if (voltage <= 3.50)
    {
        return 0;
    }
    return roundf(2836.9625 * pow(voltage, 4) - 43987.4889 * pow(voltage, 3) + 255233.8134 * pow(voltage, 2) - 656689.7123 * voltage + 632041.7303);
}

bool ADCBattery::is_charging()
{
    int pin_val = rt_pin_read(CHG_STATUS);
    return pin_val == 0;
}

void ADCBattery::stop_battery_monitor()
{
    if (battery_check_timer != RT_NULL)
    {
        rt_timer_stop(battery_check_timer);
        rt_timer_delete(battery_check_timer);
        battery_check_timer = RT_NULL;
    }
}

ADCBattery::~ADCBattery()
{

    rt_pin_irq_enable(CHG_STATUS, PIN_IRQ_DISABLE);
    rt_pin_detach_irq(CHG_STATUS);
    if (battery_check_timer != RT_NULL)
    {
        rt_timer_stop(battery_check_timer);
        rt_timer_delete(battery_check_timer);
    }
}

void ADCBattery::battery_check_callback(void* parameter)
{
    ADCBattery* battery = static_cast<ADCBattery*>(parameter);
    if (battery && battery->ui_queue) 
    {
        float voltage = battery->get_voltage();
        float percentage = battery->get_percentage();
        bool is_charging = battery->is_charging();
        rt_kprintf("[ADCBattery] Battery Level %f, percent %d\n", voltage, (int)percentage);
        
        // 发送消息到UI队列
        if (percentage < 20.0f && !is_charging && battery->low_power != 1) {
            battery->low_power = 1;        
            UIAction msg = MSG_DRAW_LOW_POWER_PAGE;
            rt_mq_send(battery->ui_queue, &msg, sizeof(UIAction));
        }
        // 如果正在充电且之前处于低电量模式，则进入充电页面
        else if (is_charging && battery->low_power == 1) 
        {
            UIAction msg = MSG_DRAW_CHARGE_PAGE;
            rt_mq_send(battery->ui_queue, &msg, sizeof(UIAction));
        }
        // 如果电量充足且之前处于低电量模式，则恢复正常模式
        else if (percentage >= 20.0f && battery->low_power == 1) 
        {
            battery->low_power = 0;           
            UIAction msg = MSG_DRAW_WELCOME_PAGE;
            rt_mq_send(battery->ui_queue, &msg, sizeof(UIAction));
        }    
    }
}

// 实现获取low_power状态的方法
bool ADCBattery::get_low_power_state() 
{
    return low_power;
}

// 实现设置low_power状态的方法
void ADCBattery::set_low_power_state(uint8_t state) 
{
    low_power = state;
}
