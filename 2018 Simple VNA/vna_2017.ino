#include "Arduino.h"
#include "Wire.h"
#include "si5351.h"
#include "quadrature.h"
#include <ti/devices/msp432p4xx/driverlib/driverlib.h>
#include "DynamicCommandParser.h" // https://github.com/mdjarv/DynamicCommandParser

extern "C"{
#include "adc14vna.h"
};
#include <stdio.h>
#include <math.h>

#define BUFFER_LENGTH 256
#define MAX_NUMBER_FREQ 1000
#define F_IF 500
#define OMEGA_IF F_IF*2*PI

Si5351 si5351;
DynamicCommandParser dcp('^', '$', ',');  //https://github.com/mdjarv/DynamicCommandParser

volatile uint16_t ref[SAMPLE_LENGTH];
volatile uint16_t meas[SAMPLE_LENGTH];
extern volatile bool doneADC;
volatile bool sendMeasurement = false;
volatile int numberFrequenciestoMeasure, frequencyIndex;
volatile float  refSum, measSum;

float shift[SAMPLE_LENGTH];  // Make this constant sometime.

int simpleDownConverter(void);
void sweepFreqMeas(char **values, int valueCount);
void voltageMeasurement(char **values, int valueCount); // For testing (sending individual voltages.
void setOscillator(unsigned long long freq);
void sendSampleRate(char **values, int valueCount);

void setup()
{
    adc14_main(); // Initialize ADC14 for multichannel conversion at 8 kHz.
    si5351.init(SI5351_CRYSTAL_LOAD_8PF, 0, 0);
    // For debugging 1/4/2018
    si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);
    setOscillator(200000000ULL);
    // Initialize the data parser using the start, end and delimiting character
    // For frequency sweep: "^SWEEP,Fmin,Fmax,NFreq$"
    dcp.addParser("SWEEP", sweepFreqMeas);
    // Returns single frequency measurements as a function of time:  "^TIME,Freq$"
    dcp.addParser("TIME", voltageMeasurement);
    // Returns the sample rate:  "^SAMPLERATE,Fs$"
    dcp.addParser("SAMPLERATE", sendSampleRate);
    for(int n=0;n<SAMPLE_LENGTH;n++) // Initialize shift, should make constant later.
    {
        shift[n] = cos(OMEGA_IF*n/SAMPLE_FREQUENCY)\
                *0.5*(1-cos(2*PI*n/(SAMPLE_LENGTH-1))); // Hanning window
    }
}

void loop()
{
    while(Serial.available() > 0)
    {
        dcp.appendChar(Serial.read());
    }
}

int simpleDownConverter(void)    // Do DSP here.
{
    int j;
    float refSum, measSum;
    refSum = 0.0;
    measSum = 0.0;
    for(j=0;j<SAMPLE_LENGTH;j++)
    {
        refSum = ref[j]*shift[j]+refSum;
        measSum = meas[j]*shift[j]+measSum;
    }
    return(1);  // Later fix this to report errors if there are any.
}

void sweepFreqMeas(char **values, int valueCount) // Might change functiono type to return errors.
{
    int i;
    unsigned long long fMin, fMax, deltaFreq, freq[MAX_NUMBER_FREQ];
    if(valueCount != 4)
    {
        Serial.println("In sweepFreqMeas, number of arguments is not correct.");
        return;  // Something is wrong if you end up here.
    }
    fMin = atoi(values[1])*100ULL;
    fMax = atoi(values[2])*100ULL;
    numberFrequenciestoMeasure = atoi(values[3]);

    deltaFreq = (fMax-fMin)/numberFrequenciestoMeasure;
    /* The idea is that we will get the first frequency's data, and then
     * send it out the serial port to gnu octave while we are getting the
     * next frequency's data.  We use the multithreaded Energia stuff to do
     * this sending.  The MultiTaskSerial.ino does the sending.  We compute the data
     * after we have collected the SAMPLE_LENGTH of it at one frequency.
     * Then we go on to the next frequency.
     *
     * At this time, we are waiting until the sending is finished, before
     * going on to the next frequency.  However, we should be able to determine
     * which operation takes the longest, and do both at the same time, to make
     * measurements quicker.  It would also speed things up to send binary data,
     * but this is harder to debug, so for now, we want to use ASCII.
     */
    for(i=0;i<numberFrequenciestoMeasure;i++)
    {
        freq[i]=fMin+i*deltaFreq;
        setOscillator(freq[i]);
        ADC14_enableConversion();
        while(!doneADC)
        {
            /* Wait until it is done converting everything at
             * this frequency.  Eventually we want to do concurrent processing.
             * For now we will just let the ADC interrupt this loop
             * and finish up its job.
             */
        }
        simpleDownConverter();
        while(sendMeasurement)
        {
            /* Wait until the last measurement is sent.  (MultiTaskSerial sets it false.)
             * Eventually we will want to do concurrent processing, but for now this is
             * safer.
             */
        }
        frequencyIndex = i;
        sendMeasurement = true;
    }
    return;
}

void voltageMeasurement(char **values, int valueCount) // Might want to return error number.
{
    int j;
    unsigned long long freq;
    if (valueCount != 2)
    {
        Serial.println(
                "In voltageMeasurement, number of arguments is not correct.");
        return;  // Something is wrong if you end up here.
    }
    freq = atoi(values[1]);
    setOscillator(freq);
    ADC14_enableConversion();
    while(!doneADC)
    {}
    {
        for (j = 0; j < SAMPLE_LENGTH; j++)
        {
            Serial.print(ref[j]);
            Serial.print(", ");
            //Serial.flush(); // Waits until completion of transmitted data.
        }
        Serial.print('\n');
        //Serial.println();
        Serial.flush();
        delay(500);
        for (j = 0; j < SAMPLE_LENGTH; j++)
        {
            Serial.print(meas[j]);
            Serial.print(", ");
            //Serial.flush(); // Waits until completion of transmitted data.
        }
        Serial.print('\n');
        //Serial.println();
        Serial.flush();
    }
    Serial.print('Done sending both ref and meas.');
}

void setOscillator (unsigned long long freq)
{
    si5351.set_freq(freq, SI5351_CLK0);
    si5351.set_freq(freq+100ULL*F_IF, SI5351_CLK1);
    si5351.set_freq(freq+100ULL*F_IF, SI5351_CLK2);
    delay(1000); // Wait for oscillator and steady state.  Do we need 1 second?
}

void sendSampleRate (char **values, int valueCount)
{
    int Fs = SAMPLE_FREQUENCY;
    int N = SAMPLE_LENGTH;
    Serial.println(Fs);
    Serial.println(N);
}

