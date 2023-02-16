#define F_CPU   16000000L

#include	<avr/io.h>
#include	<avr/interrupt.h>
#include	<stdio.h>

#define OC0 PB4 //워터펌프 제어 PWM 신호 출력핀
#define	OC2	PB7 //서보모터 제어 PWM 신호 출력핀

#define SENSOR0	0   //PIR센서 채널
#define SENSOR1 1   //왼쪽 적외선 센서 채널
#define SENSOR2 2   //오른쪽 적외선 센서 체널

volatile int timer0_rq = 0;                   //타이머0 인터럽트 요청 변수
volatile int timer2 = 0;                      //타이머2 정지 판정 변수
volatile int  adc_rq = 0;                     //ADC인터럽트 요청 변수
volatile int cnt = 0;                         //대기모드 전환 변수
volatile int adc_result[3];                   //ADC변환값을 저장하는 int형 배열
volatile int channel = SENSOR0;               //ADC 채널 선택 변수 초기값 = PIR센서
enum {IDLE, WATER, HOT, COLD} state = IDLE;   //상태 선언과 초기화

//타이머/카운터0 인터럽트
ISR(TIMER0_OVF_vect) {
    timer0_rq = 0;  //타이머0 인터럽트 응답
}

//타이머/카운터2 인터럽트
ISR(TIMER2_OVF_vect) {
    timer2++;
    //0.016 * 200초 후 타이머2 정지
    if(timer2 >= 200) {
        TCCR2 &= TCCR2 & ~(7);    //타이머2 정지
        TIMSK &= TIMSK & ~(TOIE2);    //타이머2 인터럽트 비활성화
    }
}

//ADC인터럽트
ISR(ADC_vect) {
    adc_result[channel] = ADC;  //현재 채널의 ADC값 저장
	adc_rq = 0;                 //ADC인터럽트 응답 완료
}

//타이머/카운터0 초기화 함수
void TIMER0_init(void) {
	TCCR0 = 1<<WGM01 | 1<<WGM00;	        //타이머0 고속 PWM 모드
	TCCR0 |= 1<<CS02 | 1<<CS01 | 1<<CS00;	//1024분주, 0.016384, 61Hz
	TCCR0 |= 1<<COM01;                  	
	OCR0 = 130; //1초당 평균전압 4.5V ~ 4.8V가 되는 OCR값
}

//타이머/카운터2 초기화 함수
void TIMER2_init(void) {
    DDRB |= 1<<OC2;                 //서보모터 PWM 출력핀 설정
    TCCR2 = 1<<WGM21 | 1<<WGM20;    //타이머2 고속 PWM 모드
    TCCR2 |= 1<<COM01;             
    OCR2 = (int)34;                 //서보모터 각도 초기값 = 냉수방향
}

//ADC초기화 함수
void ADC_init(void) {
    ADCSRA |= 1<<ADEN | 1<<ADIE | 7;    //ADC, ADC인터럽트 활성화, 128분주
	ADCSRA |= 1<<ADFR;                  //ADC 연속모드 설정
}

int main(void) {
//############################## 컴파일용 ##############################
    char lcd_string[4][0x40];           //LCD에 표시할 글자를 저장할 char 배열
	LCD_init();                         //LCD초기화

    sprintf(lcd_string[0], "ADC0=");    //PIR센서 측정값
	sprintf(lcd_string[1], "ADC1=");    //왼쪽 적외선 센서 측정값
	sprintf(lcd_string[2], "ADC2=");    //오른쪽 적외선 센서 측정값
    sprintf(lcd_string[3], "CNT=");     //컴파일 결과 확인용

	LCD_str_write(0, 0, lcd_string[0]); //PIR 센서
    LCD_str_write(0, 9, lcd_string[3]); //CNT
	LCD_str_write(1, 0, lcd_string[1]); //적외선 센서 좌
	LCD_str_write(1, 8, lcd_string[2]); //적외선 센서 우
//#####################################################################
    
	ADC_init();     //ADC초기화
    TIMER0_init();  //타이머/카운터0 초기화
    TIMER2_init();  //타이머/카운터2 초기화

    sei();		    //전역 인터럽트 활성화(이 시점 부터 ADC인터럽트 발생 = ADC값을 받기 시작한다)

//##################################### 메인 루프 시작 #####################################
    while(1) {
        switch(state) {
            case IDLE:                                      //대기모드
                //동작 인식하면 급수 시작
                if(adc_result[0] > 500) {        
                    TIMSK |= 1<<TOIE0;                      //타이머0 인터럽트 활성화
                    state = WATER;                          //급수모드로 변경
                }
            break;

            case WATER:                 //급수모드
                DDRB |= 1<<OC0;         //PB4 출력핀으로 설정 = 워터펌프 PWM 출력핀
                
//##################################### ADC채널 변경 #####################################
                //ADC채널을 설정하는 변수값 변경
                channel = channel == SENSOR0 ? SENSOR1 : (channel == SENSOR1 ? SENSOR2 : SENSOR0);
                //ADC채널 변경을 위해 ADMUX레지스터 변경
		        ADMUX = channel == SENSOR0 ? 0x0 : (channel == SENSOR1 ? 0x1 : 0x2);
                timer0_rq = 1;  //타이머0 인터럽트 요청
                while(timer0_rq);   //타이머/카운터0 인터럽트 발생까지 대기 = 16ms대기 = ADC 채널변경을 위한 시간지연
//########################################################################################



//##################################### 대기모드 전환 #####################################
                //일정시간 동작이 없으면 대기모드로 전환 + 냉수로 변경
                cnt = adc_result[0] < 500 ? cnt+1 : 0;
                if(cnt >= 300) {
                    state = IDLE;               //대기모드로 전환
                    timer2 = 0;                 //타이머2 정지를 위한 변수값 초기화
                    OCR2 = (int)34;             //OCR2 값 변경 = 냉수방향
                    TCCR2 |= 1<<CS22 | 1<<CS20; //타이머2 작동
                    TIMSK |= 1<<TOIE2;          //타이머2 인터럽트 활성화

                    cnt = 0;                    //대기모드 전환 카운트 초기화
                    channel = SENSOR0;          //PIR센서 채널로 변경
                    ADMUX = 0x0;                //PIR센서 채널로 변경을 위해 ADMUX 레지스터 변경
                    timer0_rq = 1;              //타이머0 인터럽트 요청
                    while(timer0_rq);           //타이머/카운터0 인터럽트 발생까지 대기 = 16ms대기 = ADC 채널변경을 위한 시간지연
                    DDRB &= DDRB & 0b10000000;  //타이머0 PWM 신호 출력 중지 = 워터펌프 정지
                    TIMSK &= TIMSK & ~(TOIE0);  //타이머0 인터럽트 비활성화
                }//end if
//########################################################################################

                if(adc_result[1] <= 980)        //왼쪽 제스처 = 온수 변경
                    state = HOT;
                else if(adc_result[2] <= 980)   //오른쪽 제스처 = 냉수 변경
                    state = COLD;               
            break;
        
            case HOT:                           //온수 변경
                cnt = 0;                        //대기모드 전환 카운트 초기화
                timer2 = 0;                     //타이머2 종료를 위한 변수값 초기화
                OCR2 = (int)21;                 //OCR2 값 변경 = 온수 방향
                TCCR2 |= 1<<CS22 | 1<<CS20;     //타이머2 작동, 1024분주
                TIMSK |= 1<<TOIE2;              //타이머2 인터럽트 활성화
                state = WATER;                  //급수 모드로 복귀
            break;
        
            case COLD:                          //냉수 변경
                cnt = 0;                        //대기모드 전환 카운트 초기화
                timer2 = 0;                     //타이머2 종료를 위한 변수값 초기화
                OCR2 = (int)34;                 //OCR2 값 변경 = 냉수 방향
                TCCR2 |= 1<<CS22 | 1<<CS20;     //타이머2 작동, 1024분주
                TIMSK |= 1<<TOIE2;              //타이머2 인터럽트 활성화
                state = WATER;                  //급수 모드로 복귀
            break;
        }   //end switch

		adc_rq = 1;         //ADC 인터럽트 요청
		ADCSRA |= 1<<ADSC;	// A/D 변환 시작
		while(adc_rq);		// ADC 인터럽트 응답 대기
    }   //end while
    return 0;
}
