#ifndef __DCSBIOS_ENCODERS_H
#define __DCSBIOS_ENCODERS_H

#include "Arduino.h"
#include "PollingInput.h"

namespace DcsBios {

	// CW
	// P1 LHHLLHHLLHHLL
	// P0 LLHHLLHHLLHHL
	//    0231023102310

	// CCW
	// P1 1001100110011
	// P0 0011001100110
	//    2013201320132
	enum StepsPerDetent {
		ONE_STEP_PER_DETENT = 1,
		TWO_STEPS_PER_DETENT = 2,
		FOUR_STEPS_PER_DETENT = 4,
		EIGHT_STEPS_PER_DETENT = 8,
	};

	template <unsigned long pollIntervalMs = POLL_EVERY_TIME, StepsPerDetent stepsPerDetent = ONE_STEP_PER_DETENT>
	class RotaryEncoderT : PollingInput {
	private:
		const char* msg_;
		const char* decArg_;
		const char* incArg_;
		char pinCLK_;
		char pinDT_;
		
		uint8_t lrmem = 3;
		int lrsum = 0;
		int num = 0;

		int8_t read_rotary()
		{
			static int8_t TRANS[] = {0,-1,1,14,1,0,14,-1,-1,14,0,1,14,1,-1,0};
			int8_t l, r;

			l = digitalRead(pinCLK_);
			r = digitalRead(pinDT_);

			lrmem = ((lrmem & 0x03) << 2) + 2*l + r;
			lrsum = lrsum + TRANS[lrmem];
			/* encoder not in the neutral state */
			if(lrsum % 4 != 0) return(0);
			/* encoder in the neutral state */
			if (lrsum == 4)
				{
				lrsum=0;
				return(1);
				}
			if (lrsum == -4)
				{
				lrsum=0;
				return(-1);
				}
			/* lrsum > 0 if the impossible transition */
			lrsum=0;
			return(0);
		}

		signed char delta_;
		
		void resetState()
		{
			lrmem = 3;
			lrsum = 0;
			num = 0;
		}
		void pollInput() {
			// To combat rotary singal noise, the internals are (heavily) inspired by this post: https://www.pinteric.com/rotary.html

			int8_t res;
   			res = read_rotary();

			if( res != 0 ) {
				delta_ += res;
				
				if (delta_ >= stepsPerDetent) {
					if (tryToSendDcsBiosMessage(msg_, incArg_))
						delta_ = 0;
				}
				else if (delta_ <= -stepsPerDetent) {
					if (tryToSendDcsBiosMessage(msg_, decArg_))
						delta_ = 0;
				}
			}
		}

	public:
		RotaryEncoderT(const char* msg, const char* decArg, const char* incArg, char pinCLK, char pinDT) :
			PollingInput(pollIntervalMs) {
			msg_ = msg;
			decArg_ = decArg;
			incArg_ = incArg;
			pinCLK_ = pinCLK;
			pinDT_ = pinDT;
			pinMode(pinCLK_, INPUT_PULLUP);
			pinMode(pinDT_, INPUT_PULLUP);
			delta_ = 0;
		}

		void SetControl( const char* msg )
		{
			msg_ = msg;
		}
	};
	typedef RotaryEncoderT<> RotaryEncoder;

	template <unsigned long pollIntervalMs = POLL_EVERY_TIME, StepsPerDetent stepsPerDetent = ONE_STEP_PER_DETENT>
	class RotaryAcceleratedEncoderT : PollingInput {
    private:
		const char* msg_;
		const char* decArg_;
		const char* incArg_;
		const char* fastDecArg_;
		const char* fastIncArg_;
		char pinCLK_;
		char pinDT_;
		char lastCLK_;
		signed char delta_;
		char cw_momentum_;

		const unsigned long FAST_THRESHOLD_MS=175;
		const unsigned long STOPPED_THRESHOLD_MS=500;
		const char MAX_MOMENTUM=4;

		unsigned long timeLastDetent_;
		
		void resetState()
		{
			lastCLK_ = (lastCLK_==0)?-1:0;
		}

		void pollInput()
		{
			char clk = digitalRead(pinCLK_);
			char dir = 0;
			
			if( clk == 1 && lastCLK_ == 0 )
			{
				// On a rising edge of the rotaries CLK pin, so sample DT
				char dt = digitalRead(pinDT_);

				if (dt != clk) {
					// CCW
					dir = -1;
				} else {
					// CW
					dir = 1;
				}
			}
			lastCLK_ = clk;

			if( dir > 0 )
			{
				// Clockwise
				if( cw_momentum_ >= 0 )
				{
					if( cw_momentum_ < MAX_MOMENTUM*stepsPerDetent )
						cw_momentum_++;
				}
				else
				{
				
					dir = 0;	// Ignore the blip in the "wrong" direction, but reduce the momentum
					cw_momentum_++;
				}
			}
			else if( dir < 0 )
			{
				// Counter-Clockwise
				if( cw_momentum_ <= 0 )
				{
					if( cw_momentum_ > -MAX_MOMENTUM*stepsPerDetent )
						cw_momentum_--;
				}
				else
				{
					dir = 0;	// Ignore the blip in the "wrong" direction, but reduce the momentum
					cw_momentum_--;
				}
			}else{
				if( (millis() - timeLastDetent_) > STOPPED_THRESHOLD_MS )
				cw_momentum_ = 0;
			}

			delta_ += dir;			
			
			if (delta_ >= stepsPerDetent) {
				const char *arg;
				if( (millis() - timeLastDetent_) < FAST_THRESHOLD_MS )
					arg = fastIncArg_;
				else
					arg = incArg_;
				if (tryToSendDcsBiosMessage(msg_, arg))
				{
					delta_ -= stepsPerDetent;
					//delta_ = 0;
					timeLastDetent_ = millis();
				}
			}
			else if (delta_ <= -stepsPerDetent) {
				const char *arg;
				if( (millis() - timeLastDetent_) < FAST_THRESHOLD_MS )
					arg = fastDecArg_;
				else
					arg = decArg_;
				if (tryToSendDcsBiosMessage(msg_, arg))
				{
					delta_ += stepsPerDetent;
					//delta_ = 0;
					timeLastDetent_ = millis();
				}
			}
		}
	public:
		RotaryAcceleratedEncoderT(const char* msg, const char* decArg, const char* incArg, const char* fastDecArg, const char* fastIncArg, char pinCLK, char pinDT) :
				PollingInput(pollIntervalMs)
		{
			msg_ = msg;
			decArg_ = decArg;
			incArg_ = incArg;
			fastDecArg_ = fastDecArg;
			fastIncArg_ = fastIncArg;
			pinCLK_ = pinCLK;
			pinDT_ = pinDT;
			pinMode(pinCLK_, INPUT_PULLUP);
			pinMode(pinDT_, INPUT_PULLUP);
			delta_ = 0;
			lastCLK_ = digitalRead(pinCLK_);
			timeLastDetent_ = millis();
			cw_momentum_ = 0;
		}
  };
  typedef RotaryAcceleratedEncoderT<> RotaryAcceleratedEncoder;
}

#endif
