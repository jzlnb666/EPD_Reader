#include "Battery.h"
#include <math.h>
extern "C" {
#include "rtdevice.h"
#include "board.h"
}
extern uint8_t low_power;
class ADCBattery : public Battery
{
private:
    rt_device_t s_adc_dev;
    int channel;
    rt_timer_t battery_check_timer;
    rt_mq_t ui_queue;

    static void battery_check_callback(void* parameter);

public:
    ADCBattery(rt_mq_t ui_queue)
  {
    this->ui_queue = ui_queue;  
    s_adc_dev = rt_device_find("bat1");
    channel = 7;
    if (s_adc_dev)
    {
        rt_adc_enable((rt_adc_device_t)s_adc_dev, channel);
    }
      battery_check_timer = RT_NULL;
  }
  virtual void setup() override
  {
      rt_kprintf("[ADCBattery] setup completed\n");
      start_battery_monitor();
  }
  void start_battery_monitor()
  {
      battery_check_timer = rt_timer_create("battery_check", 
                                          battery_check_callback, 
                                          this,  
                                          rt_tick_from_millisecond(5000), // 5秒
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
  virtual float get_voltage() override
  {
      if (!s_adc_dev) {
          rt_kprintf("[ADCBattery] get_voltage: s_adc_dev is NULL\n");
          return 0;
      }
      rt_uint32_t value = rt_adc_read((rt_adc_device_t)s_adc_dev, channel);
      float voltage = value / 10.0f; 
      return voltage;
  }

  virtual int get_percentage() override
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
  
  virtual bool is_charging() override
  {
      int pin_val = rt_pin_read(CHG_STATUS);
      return pin_val == 0;
  }

   ~ADCBattery()
    {
        if (battery_check_timer != RT_NULL)
        {
            rt_timer_stop(battery_check_timer);
            rt_timer_delete(battery_check_timer);
        }
    }
};

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
        if (percentage < 20.0f && !is_charging && low_power != 1) {
            low_power = 1;        
            UIAction msg = MSG_DRAW_LOW_POWER_PAGE;
            rt_mq_send(battery->ui_queue, &msg, sizeof(UIAction));
        }
        // 如果正在充电且之前处于低电量模式，则进入充电页面
        else if (is_charging && low_power == 1) 
        {
            UIAction msg = MSG_DRAW_CHARGE_PAGE;
            rt_mq_send(battery->ui_queue, &msg, sizeof(UIAction));
        }
        // 如果电量充足且之前处于低电量模式，则恢复正常模式
        else if (percentage >= 20.0f && low_power == 1) 
        {
            low_power = 0;           
            UIAction msg = MSG_DRAW_WELCOME_PAGE;
            rt_mq_send(battery->ui_queue, &msg, sizeof(UIAction));
        }    
    }
}