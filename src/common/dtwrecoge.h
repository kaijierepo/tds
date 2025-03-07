/***dtwrecoge.h*********************************************************************/
#ifndef dtwrecoge_h
#define dtwrecoge_h

#include<stdio.h>
#include<math.h>

#define DTWMAXNUM 2000
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ABS(a) ((a)>0?(a):(-(a)))
#define DTWVERYBIG 100000000.0




double DTWDistanceFun(double *A,int I,double *B,int J,int r);

int DTWTemplate(double *A,int I,double *B,int J,double *temp,int turn,double tt,double *rltdistance);

#endif