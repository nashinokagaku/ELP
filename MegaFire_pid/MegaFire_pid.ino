// #define DEBUG_PRESS   //気圧制御のデバッグ用
#define DEBUG_FLOW  //流量系統のデバッグ
// #define DEBUG_FLOW_LORA //LoRa越しの流量デバッグ
#define DEBUG_SENS  //センサ系のデバッグ用

//////////制御定数定義/////////////
//各制御の目標値
#define r_o 0.08  //L/min
#define r_a 0.7  //L/min
#define r_g 0.08  //L/min
#define r_d 1013.25; //気圧目標値hPa

//O2のPIDゲイン
const float Kp_o = 400;
const float Ki_o = 300;
const float Kd_o = 10;

//空気のPIDゲイン
const float Kp_a = 350;
const float Ki_a = 300;
const float Kd_a = 0.001;

//LPGのPIDゲイン
const float Kp_g = 450;
const float Ki_g = 600;
const float Kd_g = 1;

//PWMのオフセット
const int OffSet_o = 2000;
const int OffSet_a = 1950;
const int OffSet_g = 2000;

//燃焼器内気圧のPID項
#define Kp_d 0.05
#define Ki_d 0.03
#define Kd_d 0.01
#define OffSet_d (30)

//流量系統の積分偏差の上限下限設定
const int sum_max =  5;
const int sum_min = -5;

//ダイアフラム制御の積分偏差の上限下限
#define sum_d_max 30
#define sum_d_min -3000

#define u_d_max 105
#define u_d_min 37
#define Servo_INVERT 127

//ダイアフラム制御周期
#define D_COUNT 1 //×Ts(ms)
/////////////////////////////////////

//////////////通信系定数//////////////
#define IG_TIME 30 //イグナイタ点火時間
#define IG_TIME_DELAY 50 //イグナイタの点火遅れ時間(先に燃料噴射)
#define Ts 50 //(ms)タイマ割り込みの周期, 制御周期
#define SENDTIME 4  //送信間隔(s)
#define FLOW_TIME 20
/////////////////////////////////////

#include "MegaFire_pid.h"  //ライブラリとピン定義

void setup(){
  pinSetup();            //IOピンの設定
  //delay(3000);
  change_freq1(2);       //PWMの周期変更31.37kHz
  wdt_enable(WDTO_4S);   //8秒周期のウォッチドッグタイマ開始
  analogWrite(IGPWM,0);
  Serial.begin(9600);    //デバッグ用UARTとの通信開始
  Serial2.begin(115200);  //LoRa用UART
  Serial.println("MegaFire_pid");
  Serial2.println("MegaFire");
//  GNSSsetup();
  wdt_reset();
//  Wire.begin();          //I2C通信開始
//  setupBME280();
  SDsetup();
  Servo_Diaphragm.attach(Servo_PWM);
  MsTimer2::set(Ts, TIME_Interrupt); // TsごとTIME_Interruptを呼び出す
  MsTimer2::start();
}

void loop(){
//  BME280_OUT_data();
//  BME280_IN_data();
//  Create_Buffer_BME280_OUT();
//  Create_Buffer_BME280_IN();
//  SDWriteData();
IG_Get(IG_TIME+IG_TIME_DELAY); 
  /*if(time_flag!=0){
    GNSS_data();
    Create_Buffer_GNSS();
    Create_Buffer_TIME();
    myFile.write(',');
    myFile.print(Buffer_GNSS);
    myFile.write(',');
    myFile.print(Buffer_TIME);
    time_flag=0;*/
    // if(timecount > (int)(SENDTIME*1000/Ts)){
    //   Serial_print();
    //   RECEVE_Str.remove(0);
    //   /*if((IG_flag != 1)|| (Pressure_OUT<310.0&&Pressure_OUT>1.0&&IG_count<1)){
    //     Serial.write(",IG");
    //     myFile.write(",IG");
    //   }*/
    //   Serial.write(',');
    //   if(analogRead(Thermocouple_PIN)>250&&Flow_flag==1)  Serial.write('2');
    //   else Serial.print(Flow_flag);
    //   Serial.println(); 
    //   timecount=0;
    // }
//  }
  // Pressure_IG();
  // myFile.println();
  // myFile.flush(); 
}

///////////////////////サブ関数////////////////////////////
void TIME_Interrupt(void){
  static uint8_t Diaphram_count=0;
  wdt_reset();
  timecount++;
  if(timecount>(int)(1000/Ts)) time_flag=1;
  //  if(Diaphram_count>D_COUNT){
  //    sei();
  //    BME280_OUT_data();
  //    BME280_IN_data();
  //    //Pressure_IN = BME280_IN.readFloatPressure() / 100; //hPa
  //    cli();
  //    Diaphragm_control();
  //    Diaphram_count=0;
  //  }
   else Diaphram_count++;
  
  if(Flow_flag==1){
    O2_Control();
    Air_Control();
    LPG_Control();
  }
  else {
    O2PWMset=0;
    AirPWMset=0;
    LPGPWMset=0;
  }
  
  if(IG_flag==1){
    IG_count--;
    if(IG_count<1){
      IG_flag=0;
      analogWrite(IGPWM,0);
      }
    else if(IG_count<(IG_TIME_DELAY)) analogWrite(IGPWM,30);
  }
  // if(Pulse_Count>0){
  //   Pulse_Count--;
  //   if(Pulse_Count<2) Flow_flag=0;
  // }
}

void Serial_print(void){
  Serial.print(Buffer_BME280_OUT);
  //Serial.write(',');
  #ifdef DEBUG_FLOW_LORA
  Serial.print(Flow_data_LoRa[0]);
  Serial.write(',');
  Serial.print(Flow_data_LoRa[1]);
  Serial.write(',');
  Serial.print(Flow_data_LoRa[2]);
  #else
  Serial.print(Buffer_GNSS); 
  #endif
}
///////////////////////////////////////////////////////////

//////////////////////PID制御関数///////////////////////////
void Diaphragm_control(){
  /* 変数設定 */
  static float etmp_d = 0 , sum_d = 0; //1ステップ前の誤差, 誤差の総和
  float u_d = 0;
  float y_d = Pressure_IN; //現在の圧力
  float e_d = r_d - y_d; //誤差
  /* 制御計算 */
  // u_d = OffSet_d - (Kp_d * e_d + Ki_d * sum_d + Kd_d * (e_d - etmp_d) / (Ts * 1e-3)); //制御入力を計算
  u_d = Kp_d * e_d + Ki_d * sum_d + Kd_d * (e_d - etmp_d) / (Ts * 1e-3); //制御入力を計算
  etmp_d = e_d; //誤差を更新
  sum_d += (Ts * 1e-3) * e_d; //誤差の総和を更新
  /* 上下限設定 */
  if(sum_d > sum_d_max) sum_d = sum_d_max;
  else if(sum_d < sum_d_min) sum_d = sum_d_min;
  if(u_d > u_d_max) u_d = u_d_max;
  else if(u_d < u_d_min) u_d = u_d_min;
  /* 入力 */
  Servo_Diaphragm.write(u_d);
  #ifdef DEBUG_PRESS
  Serial.print(y_d);
  Serial.write(',');
  Serial.println(u_d);
  #endif
}

void O2_Control(){
  /* 変数設定 */
  static double etmp_o = 0, sum_o = 0; //1ステップ前の誤差, 誤差の総和
  double y = 0; //現在の流量
  int16_t u = 0; //制御入力
  y = analogRead(O2_flow);
  y = y * 5 / 1024; //(V) 電圧値に戻す
  y = 0.0192 * y * y + 0.0074 * y - 0.0217; //流量の線形フィッティング
  double e = r_o - y; //誤差
  /* 制御計算 */
  u = int16_t(Kp_o * e + Ki_o * sum_o + Kd_o * (e - etmp_o) / (Ts * 1e-3) + OffSet_o); //制御入力を計算
  etmp_o = e; //1ステップ前の誤差を更新
  sum_o += (Ts * 1e-3) * e; //誤差の総和を更新
  /* 上下限設定 */
  if(u > 4095) u = 4095;
  else if(u < 0) u = 0;
  /* 入力 */
  O2PWMset = u; //O2 PWM
  Flow_data_LoRa[0] = y;
  #ifdef DEBUG_FLOW
  Serial.print("O2=");
  Serial.print(y);
  Serial.write(',');
  Serial.print(u);
  Serial.write(',');
  #endif
}

void Air_Control(){
  /* 変数設定 */
  static double etmp_a = 0, sum_a = 0;
  double y = 0;
  int16_t u = 0;
  y = analogRead(Air_flow);
  y = y * 5 / 1024;
  y = 0.0528 * y * y -0.0729 * y + 0.0283;
  double e = r_a - y;
  /* 制御計算 */
  u = int16_t(Kp_a * e + Ki_a * sum_a + Kd_a * (e - etmp_a) / (Ts * 1e-3) + OffSet_a);
  etmp_a = e;
  sum_a += (Ts * 1e-3) * e;
  /* 上下限設定 */
  if(u > 4095) u = 4095;
  else if(u < 0) u = 0;
  /* 入力 */
  AirPWMset = u;//Air PWM
  Flow_data_LoRa[1] = y;
  #ifdef DEBUG_FLOW
  Serial.print("Air=");
  Serial.print(y);
  Serial.write(',');
  Serial.print(u);
  Serial.write(',');
  #endif
}

void LPG_Control(){
  /* 変数設定 */
  static double etmp_g = 0, sum_g = 0;
  double y = 0;
  int16_t u = 0;
  y = analogRead(LPG_flow);
  y = y * 5 / 1024;
  y = 0.0192 * y * y + 0.0074 * y - 0.0217;
  double e = r_g - y;
  /* 制御計算 */
  u = int16_t(Kp_g * e + Ki_g * sum_g + Kd_g * (e - etmp_g) / (Ts * 1e-3) + OffSet_g);
  etmp_g = e;
  sum_g += (Ts * 1e-3) * e;
  /* 上下限設定 */
  if(u > 4095) u = 4095;
  else if(u < 0) u = 0;
  /* 入力 */
  LPGPWMset = u;//LPG PWM
  Flow_data_LoRa[2] = y;
  #ifdef DEBUG_FLOW
  Serial.print("LPG=");
  Serial.print(y);
  Serial.write(',');
  Serial.println(u);
  #endif
}
