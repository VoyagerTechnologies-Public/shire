/*
 * 42 Stubs - Minimal stubs for optional 42 functions
 * This file provides empty implementations for optional 42 features
 * that we don't need for basic dynamics simulation in Simulith.
 */

#include <stdio.h>

/* NOS3 time function stub */
double NOS3Time(void) 
{
    return 0.0;
}

/* Optical sensor stubs */
double OpticalFieldPoint(void) 
{
    return 0.0;
}

double OpticalTrain(void) 
{
    return 0.0;
}

/* AC (Attitude Control) FSW stub */
void AcFsw(void) 
{
    /* Empty implementation */
}

/* CSV output stubs */
void WriteAcVarsToCsv(void) 
{
    /* Empty implementation */
}

void WriteScVarsToCsv(void) 
{
    /* Empty implementation */
}

/* Socket communication stubs */
int WriteToSocket(void) 
{
    return 0;
}

int ReadFromSocket(void) 
{
    return 0;
}
