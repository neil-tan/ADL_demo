/* Includes */
#include <cstdio>
#include "mbed.h"
#include "stm32f413h_discovery.h"
#include "stm32f413h_discovery_ts.h"
#include "stm32f413h_discovery_lcd.h"
#include "MMA7660FC.h"
#include "SensorQueue.hpp"
#include "Train/adl_model/models/deep_mlp.hpp"

static const float G_VALUE[64] = { 0, 0.047, 0.094, 0.141, 0.188, 0.234, 0.281, 0.328,
                      0.375, 0.422, 0.469, 0.516, 0.563, 0.609, 0.656, 0.703,
                      0.750, 0.797, 0.844, 0.891, 0.938, 0.984, 1.031, 1.078,
                      1.125, 1.172, 1.219, 1.266, 1.313, 1.359, 1.406, 1.453,
                      -1.500, -1.453, -1.406, -1.359, -1.313, -1.266, -1.219,
                      -1.172, -1.125, -1.078, -1.031, -0.984, -0.938, -0.891,
                      -0.844, -0.797, -0.750, -0.703, -0.656, -0.609, -0.563,
                      -0.516, -0.469, -0.422, -0.375, -0.328, -0.281, -0.234,
                      -0.188, -0.141, -0.094, -0.047};

MMA7660FC Acc(I2C_SDA, I2C_SCL, ADDR_MMA7660);      //sda, scl, Addr

Serial pc(USBTX, USBRX, 115200);

InterruptIn button(USER_BUTTON);

//uTensor Context
// Context ctx;

struct ACC_UINT8_VECTOR {
  uint8_t x;
  uint8_t y;
  uint8_t z;
};

SensorQueue<ACC_UINT8_VECTOR> buff(160, 32, 2);
ACC_UINT8_VECTOR sample;

void accTimerHandler(void) {
  static ACC_UINT8_VECTOR sample;
  sample.x = 0b111111 & (uint8_t) Acc.read_x();
  sample.y = 0b111111 & (uint8_t) Acc.read_y();
  sample.z = 0b111111 & (uint8_t) Acc.read_z();
  buff.append(sample);
}

void uTensorTrigger(void) {
  Context ctx;
  ACC_UINT8_VECTOR* tmp = (ACC_UINT8_VECTOR*) malloc(sizeof(ACC_UINT8_VECTOR) * 160 * 3);
  Tensor* data = new RamTensor<float>();
  std::vector<uint32_t> input_shape({1, 160 * 3});
  data->init(input_shape);
  
  buff.copyTo(tmp);

  for(uint8_t i = 0; i < 160; i++) {
    //FIXME: mis-used write function
    float* data_ptr = (float*) data->write<float>(0, 0);
    data_ptr[i*3] = G_VALUE[tmp[i].x];
    data_ptr[i*3+1] = G_VALUE[tmp[i].y];
    data_ptr[i*3+2] = G_VALUE[tmp[i].z];
  }

  free(tmp);

  get_deep_mlp_ctx(ctx, data);
  ctx.eval();
  S_TENSOR prediction = ctx.get({"y_pred:0"});
  int result = *(prediction->read<int>(0,0));
  printf("activity: %d\n\r", result);

  char result_str[16];
  switch (result) {
    case 0:
      sprintf(result_str, "Walking\0");
    break;
    case 1:
      sprintf(result_str, "Inclining\0");
    break;
    case 2:
      sprintf(result_str, "Activities\0");
    break;
    case 3:
      sprintf(result_str, "Descending\0");
    break;
    default:
      sprintf(result_str, "Resting\0");
  }

  //run inference in an event queue
  //printf("test acc reading... x: %d, y: %d, z: %d\r\n", tmp[0].x, tmp[1].y, tmp[2].z);

  BSP_LCD_Clear(LCD_COLOR_WHITE);
  BSP_LCD_SetTextColor(LCD_COLOR_BLACK);
  BSP_LCD_SetFont(&Font24);
  BSP_LCD_DisplayStringAt(0, 120, (uint8_t*) result_str, CENTER_MODE);
}

int main() {

  pc.printf("program start\r\n");

  Acc.init();
  BSP_LCD_Init();
  if (BSP_TS_Init(BSP_LCD_GetXSize(), BSP_LCD_GetYSize()) == TS_ERROR) {
      printf("BSP_TS_Init error\n");
  }
  BSP_LCD_Clear(LCD_COLOR_WHITE);

  //setup buffer callback
  buff.setCallBack(uTensorTrigger);

  //Sensor Ticker
  Ticker sensorTick;
  sensorTick.attach(&accTimerHandler, 1.0f/32); //32Hz
 

  while(1) {
    printf("background thread\r\n");
    wait(5);
  }
}