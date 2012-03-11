#ifndef __TUNER_E4000_H
#define __TUNER_E4000_H

/**

@file

@brief   E4000 tuner module declaration

One can manipulate E4000 tuner through E4000 module.
E4000 module is derived from tuner module.



@par Example:
@code

// The example is the same as the tuner example in tuner_base.h except the listed lines.



#include "tuner_e4000.h"


...



int main(void)
{
	TUNER_MODULE        *pTuner;
	E4000_EXTRA_MODULE *pTunerExtra;

	TUNER_MODULE          TunerModuleMemory;
	BASE_INTERFACE_MODULE BaseInterfaceModuleMemory;
//	I2C_BRIDGE_MODULE     I2cBridgeModuleMemory;

	unsigned long BandwidthMode;


	...



	// Build E4000 tuner module.
	BuildE4000Module(
		&pTuner,
		&TunerModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		0xac,								// I2C device address is 0xac in 8-bit format.
		CRYSTAL_FREQ_16384000HZ,			// Crystal frequency is 16.384 MHz.
		E4000_AGC_INTERNAL					// The E4000 AGC mode is internal AGC mode.
		);





	// Get E4000 tuner extra module.
	pTunerExtra = (T2266_EXTRA_MODULE *)(pTuner->pExtra);





	// ==== Initialize tuner and set its parameters =====

	...

	// Set E4000 bandwidth.
	pTunerExtra->SetBandwidthMode(pTuner, E4000_BANDWIDTH_6MHZ);





	// ==== Get tuner information =====

	...

	// Get E4000 bandwidth.
	pTunerExtra->GetBandwidthMode(pTuner, &BandwidthMode);



	// See the example for other tuner functions in tuner_base.h


	return 0;
}


@endcode

*/





//#include "tuner_base.h"





// The following context is implemented for E4000 source code.


// Definition (implemeted for E4000)
#define E4000_1_SUCCESS			1
#define E4000_1_FAIL			0
#define E4000_I2C_SUCCESS		1
#define E4000_I2C_FAIL			0



// Function (implemeted for E4000)
int
I2CReadByte(
	int pTuner,
	unsigned char NoUse,
	unsigned char RegAddr,
	unsigned char *pReadingByte
	);

int
I2CWriteByte(
	int pTuner,
	unsigned char NoUse,
	unsigned char RegAddr,
	unsigned char WritingByte
	);

int
I2CWriteArray(
	int pTuner,
	unsigned char NoUse,
	unsigned char RegStartAddr,
	unsigned char ByteNum,
	unsigned char *pWritingBytes
	);



// Functions (from E4000 source code)
int tunerreset (int pTuner);
int Tunerclock(int pTuner);
int Qpeak(int pTuner);
int DCoffloop(int pTuner);
int GainControlinit(int pTuner);

int Gainmanual(int pTuner);
int E4000_gain_freq(int pTuner, int frequency);
int PLL(int pTuner, int Ref_clk, int Freq);
int LNAfilter(int pTuner, int Freq);
int IFfilter(int pTuner, int bandwidth, int Ref_clk);
int freqband(int pTuner, int Freq);
int DCoffLUT(int pTuner);
int GainControlauto(int pTuner);

int E4000_sensitivity(int pTuner, int Freq, int bandwidth);
int E4000_linearity(int pTuner, int Freq, int bandwidth);
int E4000_high_linearity(int pTuner);
int E4000_nominal(int pTuner, int Freq, int bandwidth);


// The following context is E4000 tuner API source code

// Definitions

// Bandwidth in Hz
enum E4000_BANDWIDTH_HZ
{
	E4000_BANDWIDTH_6000000HZ = 6000000,
	E4000_BANDWIDTH_7000000HZ = 7000000,
	E4000_BANDWIDTH_8000000HZ = 8000000,
};


// Manipulaing functions
void
e4000_GetTunerType(
	int pTuner,
	int *pTunerType
	);

void
e4000_GetDeviceAddr(
	int pTuner,
	unsigned char *pDeviceAddr
	);

int
e4000_Initialize(
	int pTuner
	);

int
e4000_SetRfFreqHz(
	int pTuner,
	unsigned long RfFreqHz
	);

int
e4000_GetRfFreqHz(
	int pTuner,
	unsigned long *pRfFreqHz
	);





// Extra manipulaing functions
int
e4000_GetRegByte(
	int pTuner,
	unsigned char RegAddr,
	unsigned char *pReadingByte
	);

int
e4000_SetBandwidthHz(
	int pTuner,
	unsigned long BandwidthHz
	);

int
e4000_GetBandwidthHz(
	int pTuner,
	unsigned long *pBandwidthHz
	);

#endif
