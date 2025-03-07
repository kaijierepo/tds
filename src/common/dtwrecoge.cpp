/*dtwrecoge.cpp**************************************************************/

#include "dtwrecoge.h"
double dtw_distance[DTWMAXNUM][DTWMAXNUM]; 
double dtwpath[DTWMAXNUM][DTWMAXNUM]; 


double DTWDistanceFun(double *A,int I,double *B,int J,int r)
{
	int i,j;
	double dist;
	int istart,imax;
	int r2=r+ABS(I-J);
	double g1,g2,g3;
	int pathsig=1;


	if(I>DTWMAXNUM||J>DTWMAXNUM){
		//printf("Too big number\n");
		return -1.0;
	}
	

	for(i=0;i<I;i++){
		for(j=0;j<J;j++){
			dtwpath[i][j]=0;
			dtw_distance[i][j]=DTWVERYBIG;
		}
	}

	dtw_distance[0][0]=(double)2*ABS(A[0]-B[0]);
	for(i=1;i<=r2;i++){
		dtw_distance[i][0]=dtw_distance[i-1][0]+ABS(A[i]-B[0]);
	}
	for(j=1;j<=r2;j++){
		dtw_distance[0][j]=dtw_distance[0][j-1]+ABS(A[0]-B[j]);
	}
	
	for(j=1;j<J;j++){
		istart=j-r2;
		if(j<=r2)
			istart=1;
		imax=j+r2;
		if(imax>=I)
			imax=I-1;
		
		for(i=istart;i<=imax;i++){
			g1=dtw_distance[i-1][j]+ABS(A[i]-B[j]);
			g2=dtw_distance[i-1][j-1]+2*ABS(A[i]-B[j]);
			g3=dtw_distance[i][j-1]+ABS(A[i]-B[j]);
			g2=MIN(g1,g2);
			g3=MIN(g2,g3);
			dtw_distance[i][j]=g3;
		}
	}
		
	dist=dtw_distance[I-1][J-1]/((double)(I+J));
	return dist;
}/*end DTWDistance*/


int DTWTemplate(double *A,int I,double *B,int J,double *temp,int turn,double tt,double *rltdistance)
{
	double dist;
	int i,j;
	int pathsig=1;
	dist=DTWDistanceFun(A,I,B,J,(int)(I/30));
	if(dist>tt){
		printf("\nSample doesn't match!\n");
		return -1;
	}
		
	if(turn==1)
		*rltdistance=dist;
	else{
		*rltdistance=((*rltdistance)*(turn-1)+dist)/turn;
	}

	i=I-1;
	j=J-1;
	while(j>=1||i>=1){
		double m;
		if(i>0&&j>0){
			m=MIN(MIN(dtw_distance[i-1][j],dtw_distance[i-1][j-1]),dtw_distance[i][j-1]);
			if(m==dtw_distance[i-1][j]){
				dtwpath[i-1][j]=pathsig;
				i--;
			}
			else if(m==dtw_distance[i-1][j-1]){
				dtwpath[i-1][j-1]=pathsig;
				i--;
				j--;
			}
			else{
				dtwpath[i][j-1]=pathsig;
				j--;
			}
		}
		else if(i==0){
			dtwpath[0][j-1]=pathsig;
			j--;
		}
		else{/*j==0*/
			dtwpath[i-1][0]=pathsig;
			i--;
		}
	}
	dtwpath[0][0]=pathsig;
	dtwpath[I-1][J-1]=pathsig;
	

	for(i=0;i<I;i++){
		double ftemp=0.0;
		int ntemp=0;
		for(j=0;j<J;j++){
			if(dtwpath[i][j]==pathsig){
				ftemp+=B[j];
				ntemp++;
			}
		}
		ftemp/=((double)ntemp);
		temp[i]=(A[i]*turn+ftemp)/((double)(turn+1));
	}
	
	return I;
}//end DTWTemplate