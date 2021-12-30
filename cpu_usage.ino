//Class for idle waiting and tracking of CPU/MCU utilization
//
//License: MIT (https://opensource.org/licenses/MIT)
//Dependencies: Arduino API, CPU/MCU agnostic
//
//- With usage example code


#include <cassert>


#define MICROS_PER_SEC  1000000
#define MICROS_BOUNDARY_OVERFLOW_SHIFT  (1000000 - ((0xFFFFFFFF % 1000000) + 1))  //== 32704


/*
Class for measuring roughly the CPU utilization (usage vs. idle time) using Arduino API's micros() function.
Allows for waiting idle time and calculates utilization.
Each passed second the usage percentage is calculated automatically.
Additionally the overall usage since reset is tracked as float value.
Interrupt service time is not tracked and could falsely count to idle time.

Whenever you have waiting time, call the wait method to keep track of idle microseconds.
Try to call wait() method multiple times per second evenly spread to avoid bouncing of the previous second percentage value.
"yield()" call is not integrated.

simplified example:
  int frame_delay = 1000 / FPS; //delay between frames to maintain target FPS rate
  void loop() {
    render_frame();
    wait_idle_ms(frame_delay); //parameter should be > 0
  }

Overflow issue:
- An unsigned 32bit int measures "micros"
- At first the full second boundary in micros is 0 + X * 1000000
- After the first overflow of micros() at 4294967296 (4295 seconds) it shifts to 32704 + X * 1000000 (by 32704 per overflow)
- We just ignore it here, because the measure error is too small
*/
class CPU_Usage {
	public:
	
	uint32_t time_start; //last measurement start
	uint32_t time_stop;  //last measurement stop
	
	uint32_t cnt_micros_prev_second; //count of idle microseconds in passed full second
	uint32_t cnt_micros_curr_second; //count of micros in currently unfinished second
	uint8_t usage_prev_second; //usage of previously passed second
	
	uint32_t micros_boundary_shift;
	
	uint32_t cnt_sec;  //count passed seconds (since reset)
	float usage_total; //usage over passed seconds (since reset)
	
	
	protected:
	
	//----------------------------------------
	//Calculate utilization percentage for the last second
	//Called for each second boundary passed
	inline void update_second(){
		cnt_micros_prev_second = cnt_micros_curr_second;
		cnt_micros_curr_second = 0;
		
		usage_prev_second = 100 - (100 * cnt_micros_prev_second / MICROS_PER_SEC ); // 100% - idle% = usage%
		
		//for total usage percentage we ignore the micros from second 0 because it's not tracked fully and adds error
		if(cnt_sec >= 1)
			usage_total = (float)(usage_prev_second + (usage_total * (cnt_sec - 1)) ) / (cnt_sec);
		
		cnt_sec++;
	}
	
	
	//----------------------------------------
	//Return number of micros left to full second boundary
	inline uint32_t micros_to_sec_boundary(uint32_t t){
		return MICROS_PER_SEC - (t % MICROS_PER_SEC);
	}
	
	
	
	public:
	
	//----------------------------------------
	CPU_Usage(){
		usage_prev_second = 0;
		micros_boundary_shift = 0;
		reset_usage_total();
	}
	
	
	//----------------------------------------
	//Wait 100 idle microseconds and keep track of usage ratio
	//This whole function code time counts as idle
	//@8MHz: 800 cycles (enough for calculations?)
	void wait_idle_micros_100(){
		time_start = micros(); //get reference timestamp at the very start
		
		//usage time has passed outside this idle waiting call => could also be multiple seconds
		
		uint32_t diff_micros = time_start - time_stop; //also correct with overflown time_start: current start - previous stop
		
		//previous stop to full second boundary
		uint32_t n = micros_to_sec_boundary(time_stop);
		if(diff_micros >= n){
			update_second();
			diff_micros -= n;
		}
		//full seconds with no idle micros => usage 100%
		while(diff_micros >= MICROS_PER_SEC) {
			update_second();
			diff_micros -= MICROS_PER_SEC;
		}
		
		time_stop = time_start + 100;
		
		//calculate results before waiting for stop time to increase precision
		uint32_t time_start_biased = time_start;
		uint32_t time_stop_biased = time_stop;
		uint32_t bias = 0;
		
		//if micros() has overflow => use safe bias
		if(time_start > time_stop) {
			bias = MICROS_PER_SEC;
			time_start_biased += bias;
			time_stop_biased += bias;
		}
		
		int us_to_full_sec = micros_to_sec_boundary(time_start_biased); //remaining micros to full second
		
		//if second boundary reached
		if(us_to_full_sec <= 100){
			cnt_micros_curr_second += us_to_full_sec;
			update_second();
			cnt_micros_curr_second += 100 - us_to_full_sec;
		}
		else
			cnt_micros_curr_second += 100;
		
		if(time_start > time_stop) { //if overflow
			micros_boundary_shift += MICROS_BOUNDARY_OVERFLOW_SHIFT;
		}
		
		//wait for micros reaching stop timestamp
		while(micros() + bias < time_stop_biased){
		}
	}
	
	
	//----------------------------------------
	//Wait number of idle milliseconds
	void wait_idle_ms(uint32_t ms){
		//(just calling multiple times to wait for 100 micros saves a lot of code but also adds some extra waiting time)
		while(ms){
			wait_idle_micros_100();
			wait_idle_micros_100();
			wait_idle_micros_100();
			wait_idle_micros_100();
			wait_idle_micros_100();
			wait_idle_micros_100();
			wait_idle_micros_100();
			wait_idle_micros_100();
			wait_idle_micros_100();
			wait_idle_micros_100();
			ms--;
		}
	}
	
	
	//----------------------------------------
	//Return % utilization of last second
	uint8_t get_usage_sec(){
		return usage_prev_second;
	}
	
	
	//----------------------------------------
	//Return average % utilization since last reset() call
	float get_usage_total(){
		return usage_total;
	}
	
	
	//----------------------------------------
	//Reset total time tracking
	void reset_usage_total(){
		usage_total = 0.0;
		cnt_sec = 0;
	}
};




class CPU_Usage cpu_usage; //create instance


void setup() {
	
	//pinMode(OUTPUT_PIN, OUTPUT);
	
	delay(1);  //calls ESP background functions (also yield(), or a return in loop())
	
	Serial.begin(115200);
}


//Simulate a second of usage in % with idle waiting
void sim_usage(int perc){
	if(perc < 0  ||  perc > 100)
		assert( Serial.println(String("Error \"") + __FILE__ + "\", " + __LINE__ + ": Invalid value, only 0..100 allowed") );
	
	//10 times 100 ms
	for(int k = 0; k < 10; k++) {
		delay(perc); //simulated utilization time
		cpu_usage.wait_idle_ms(100-perc);
	}
	if(100-perc == 0)
		cpu_usage.wait_idle_ms(1); //make sure to call at least once per second with parameter > 0 for usage calculation
}


void loop() {
	sim_usage(95);
	
	Serial.println(cpu_usage.get_usage_sec());
	Serial.println(cpu_usage.get_usage_total());
	Serial.println(micros());
}

