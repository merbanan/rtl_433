#ifndef __TUNER_E4000_H
#define __TUNER_E4000_H

// Definition (implemeted for E4000)
#define E4000_1_SUCCESS			1
#define E4000_1_FAIL			0
#define E4000_I2C_SUCCESS		1
#define E4000_I2C_FAIL			0

#define E4K_I2C_ADDR		0xc8
#define E4K_CHECK_ADDR		0x02
#define E4K_CHECK_VAL		0x40

// Function (implemeted for E4000)
int
I2CReadByte(void *pTuner,
    unsigned char NoUse,
    unsigned char RegAddr,
    unsigned char *pReadingByte
    );

int
I2CWriteByte(
    void *pTuner,
	unsigned char NoUse,
	unsigned char RegAddr,
	unsigned char WritingByte
	);

int
I2CWriteArray(void *pTuner,
    unsigned char NoUse,
    unsigned char RegStartAddr,
    unsigned char ByteNum,
    unsigned char *pWritingBytes
    );



// Functions (from E4000 source code)
int tunerreset (void *pTuner);
int Tunerclock(void *pTuner);
int Qpeak(void *pTuner);
int DCoffloop(void *pTuner);
int GainControlinit(void *pTuner);

int Gainmanual(void *pTuner);
int E4000_gain_freq(void *pTuner, int frequency);
int PLL(void *pTuner, int Ref_clk, int Freq);
int LNAfilter(void *pTuner, int Freq);
int IFfilter(void *pTuner, int bandwidth, int Ref_clk);
int freqband(void *pTuner, int Freq);
int DCoffLUT(void *pTuner);
int GainControlauto(void *pTuner);

int E4000_sensitivity(void *pTuner, int Freq, int bandwidth);
int E4000_linearity(void *pTuner, int Freq, int bandwidth);
int E4000_high_linearity(void *pTuner);
int E4000_nominal(void *pTuner, int Freq, int bandwidth);


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
    void *pTuner,
	int *pTunerType
	);

void
e4000_GetDeviceAddr(
    void *pTuner,
	unsigned char *pDeviceAddr
	);

int
e4000_Initialize(
    void *pTuner
	);

int
e4000_SetRfFreqHz(
    void *pTuner,
	unsigned long RfFreqHz
	);

int
e4000_GetRfFreqHz(
    void *pTuner,
	unsigned long *pRfFreqHz
	);





// Extra manipulaing functions
int
e4000_GetRegByte(
    void *pTuner,
	unsigned char RegAddr,
	unsigned char *pReadingByte
	);

int
e4000_SetBandwidthHz(
    void *pTuner,
	unsigned long BandwidthHz
	);

int
e4000_GetBandwidthHz(
    void *pTuner,
	unsigned long *pBandwidthHz
	);

#endif
