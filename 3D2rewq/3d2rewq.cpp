#include "mpi.h"
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "sys/time.h"

#include <omp.h>

#define PIE 3.1415926

// #define DEBUG_CPU_RUNNING

//#define SHOW_NORMAL_OUTPUT

#define USE_OMP_PARALLEL_CALC                false

#define USE_OMP_PARALLEL_GATHER              false


#define SHOW_NORMAL_OUTPUT

// #ifdef __MIC__
// #    define  CORE_NUM omp_get_num_procs()
// #else
// #    define  CORE_NUM 8
// #endif

#define MIC_MAX_OFFLOAD_BYTES           1024*1024*7

#define OUT_PUT_SLICE_Z_INDEX           169

// MPI PARAMATER
#define MAX_GROUP_SIZE 4
#define SPLIT_THRESHOLD 80

#define USE_MIC_MAX_LENGTH_THRESHOLD    100

#ifdef __MIC__

#define USE_OMP_SLICES_ON_EACH_CORE_MIN     3

#else

#define USE_OMP_SLICES_ON_EACH_CORE_MIN    5

#endif

#define MIC_CPU_RATE    0.25

#define MIC_COUNT       1

#define POSITION_INDEX_HOST_X(_z,_y,_x)        ((_z)*ny*nx + (_y)*nx + (_x))
#define POSITION_INDEX_HOST_Y(_z,_y,_x)        ((_x)*nz*ny + (_z)*ny + (_y))
#define POSITION_INDEX_HOST_Z(_z,_y,_x)        ((_y)*nx*nz + (_x)*nz + (_z))

#ifndef POSITION_DEBUG_ASSERT

#define POSITION_INDEX_X(_z,_y,_x)   ((_z)*(nMicMaxYLength+10)*(nMicMaxXLength+10) + (_y)*(nMicMaxXLength+10) + (_x))

#else

inline
int DebugPositionIndexX(int z,int y,int x,int nMicMaxXLength,int nMicMaxYLength){
    int nIndex = ((_z)*(nMicMaxYLength+10)*(nMicMaxXLength+10) + (_y)*(nMicMaxXLength+10) + (_x));
    //Do exam here
    return nIndex;
}

#define POSITION_INDEX_X(_z,_y,_x)   DebugPositionIndexX((_z),(_y),(_x),nMicMaxXLength,nMicMaxYLength)

#endif

#ifndef DEBUG_CPU_RUNNING
#   define MIC_ALLOC       alloc_if(1) free_if(0)
#   define MIC_FREE        alloc_if(0) free_if(1)
#   define MIC_REUSE       alloc_if(0) free_if(0)
#   define MIC_SELF_MANAGE alloc_if(0) free_if(0)
#   define MIC_VAR         __attribute__((target(mic)))
#else
#   define MIC_VAR
#endif

#define DEBUG_ASSERT(_expr) if(!(_expr))printf("[!]Assert failed! _expr\n");

#ifdef __MIC__
#define __SHOW__(_x) for(int jjj=0;jjj<1000;jjj++) printf("Hit here!!!! %d \n", _x);
#else
#define __SHOW__(_x)
#endif


#define ROUND_TO_SIZE(_length, _alignment)    \
    (((_length) + ((_alignment)-1)) & ~((_alignment) - 1))

#define ROUND_TO_SIZE_LESS(_length,_alignment)  \
    ((_length) &~ ((_alignment)-1))

#define vpp2(_z) ((((_z)+ntop-5)<210)?(5290000.):(((_z)+ntop-5)>=260?12250000.:7840000.))
#define vss2(_z) ((((_z)+ntop-5)<210)?(1517824.):(((_z)+ntop-5)>=260?3644281.:2277081.))

typedef struct _MEMORY_BLOCKS {

    double * up, * up1, * up2, * vp, * vp1, * vp2, * wp, * wp1, \
        * wp2, * us, * us1, * us2, * vs, * vs1, * vs2, * ws, * ws1, * ws2;
    double *u, *v, *w;
    double * to_write;
} MEMORY_BLOCKS, *PMEMORY_BLOCKS;

MIC_VAR int ncy_shot, ncx_shot;

int nshot,ishot, total_sum = 0;
double t0, tt;

MIC_VAR double *wave;
MIC_VAR double  c0;
MIC_VAR double dtx, dtz;

int nx, ny, nz, lt, nedge;
double frequency;
double velmax;
double dt;
int ncx_shot1, ncy_shot1, ncz_shot;
double unit;
int nxshot, nyshot, dxshot, dyshot;

char infile[80], outfile[80], logfile[80], tmp[80];
FILE  *fin, *fout, *flog;
struct timeval start, end;
double all_time;
MIC_VAR double c[7][11];

int nSize;
int nSliceSize;

double * to_write;

int mic_used_size;
int mic_slice_size;

double *up_out;

bool init_mic_flag[MIC_COUNT];

double fbuf[4];
int buf[14];
int mpi_sum, mpi_id;
const int root_id = 0;
int group_size, group_id, core_remain, group_limit, group_begin, group_end,  group_pos;
bool l_parallel;

void initailize() {
    if(mpi_id == root_id) {
        strcpy ( tmp, "date " );
        strncat ( tmp, ">> ", 3 );
        strncat ( tmp, logfile, strlen ( logfile ) );
        flog = fopen ( logfile, "w" );
        fprintf ( flog, "------------start time------------\n" );
        fclose ( flog );
        system ( tmp );
        gettimeofday ( &start, NULL );

        fin = fopen ( infile, "r" );
        if ( fin == NULL ) {
            printf ( "file %s is  not exist\n", infile );

            exit ( 0 );
        }
        fscanf ( fin, "nx=%d\n", &nx );
        fscanf ( fin, "ny=%d\n", &ny );
        fscanf ( fin, "nz=%d\n", &nz );
        fscanf ( fin, "lt=%d\n", &lt );
        fscanf ( fin, "nedge=%d\n", &nedge );
        fscanf ( fin, "ncx_shot1=%d\n", &ncx_shot1 );
        fscanf ( fin, "ncy_shot1=%d\n", &ncy_shot1 );
        fscanf ( fin, "ncz_shot=%d\n", &ncz_shot );
        fscanf ( fin, "nxshot=%d\n", &nxshot );
        fscanf ( fin, "nyshot=%d\n", &nyshot );
        fscanf ( fin, "frequency=%lf\n", &frequency );
        fscanf ( fin, "velmax=%lf\n", &velmax );
        fscanf ( fin, "dt=%lf\n", &dt );
        fscanf ( fin, "unit=%lf\n", &unit );
        fscanf ( fin, "dxshot=%d\n", &dxshot );
        fscanf ( fin, "dyshot=%d\n", &dyshot );
        fclose ( fin );
        buf[2] = nx;buf[3] = ny; buf[4] = nz; buf[5] = lt; buf[6] = nedge; buf[7] = ncx_shot1;
        buf[8] = ncy_shot1; buf[9] = ncz_shot; buf[10] = nxshot; buf[11] = nyshot;
        buf[12] = dxshot; buf[13] = dyshot;
        fbuf[0] = frequency; fbuf[1] = velmax; fbuf[2] = dt; fbuf[3] = unit;

#ifdef SHOW_NORMAL_OUTPUT
        printf ( "\n--------workload parameter--------\n" );
        printf ( "nx=%d\n", nx );
        printf ( "ny=%d\n", ny );
        printf ( "nz=%d\n", nz );
        printf ( "lt=%d\n", lt );
        printf ( "nedge=%d\n", nedge );
        printf ( "ncx_shot1=%d\n", ncx_shot1 );
        printf ( "ncy_shot1=%d\n", ncy_shot1 );
        printf ( "ncz_shot=%d\n", ncz_shot );
        printf ( "nxshot=%d\n", nxshot );
        printf ( "nyshot=%d\n", nyshot );
        printf ( "frequency=%lf\n", frequency );
        printf ( "velmax=%lf\n", velmax );
        printf ( "dt=%lf\n", dt );
        printf ( "unit=%lf\n", unit );
        printf ( "dxshot=%d\n", dxshot );
        printf ( "dyshot=%d\n\n", dyshot );
#endif
        flog = fopen ( logfile, "a" );
        fprintf ( flog, "\n--------workload parameter--------\n" );
        fprintf ( flog, "nx=%d\n", nx );
        fprintf ( flog, "ny=%d\n", ny );
        fprintf ( flog, "nz=%d\n", nz );
        fprintf ( flog, "lt=%d\n", lt );
        fprintf ( flog, "nedge=%d\n", nedge );
        fprintf ( flog, "ncx_shot1=%d\n", ncx_shot1 );
        fprintf ( flog, "ncy_shot1=%d\n", ncy_shot1 );
        fprintf ( flog, "ncz_shot=%d\n", ncz_shot );
        fprintf ( flog, "nxshot=%d\n", nxshot );
        fprintf ( flog, "nyshot=%d\n", nyshot );
        fprintf ( flog, "frequency=%lf\n", frequency );
        fprintf ( flog, "velmax=%lf\n", velmax );
        fprintf ( flog, "dt=%lf\n", dt );
        fprintf ( flog, "unit=%lf\n", unit );
        fprintf ( flog, "dxshot=%d\n", dxshot );
        fprintf ( flog, "dyshot=%d\n\n", dyshot );
        fclose ( flog );
}

    MPI_Bcast(buf, 14, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Bcast(fbuf, 4, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    nx = buf[2]; ny = buf[3]; nz = buf[4]; lt = buf[5]; nedge = buf[6]; ncx_shot1 = buf[7];
    ncy_shot1 = buf[8]; ncz_shot = buf[9]; nxshot = buf[10]; nyshot = buf[11];
    dxshot = buf[12]; dyshot = buf[13];
    frequency = fbuf[0]; velmax = fbuf[1]; dt = fbuf[2]; unit = fbuf[3];
    nSize = nz * ny * nx;
    nSliceSize = ny * nx;
    nshot = nxshot * nyshot;

    mic_used_size  = nx*ny*nz;
    mic_slice_size = nx*ny;
  /*   mic_used_size = pow ( 2.* ( ( lt * dt * velmax ) / unit + 10. ) + 10. + 1., 3. );
    mic_slice_size = pow ( 2.* ( ( lt * dt * velmax ) / unit + 10. ) + 10. + 1, 2. );
 */

    wave = ( double* ) malloc ( sizeof ( double ) * lt );

    t0 = 1.0 / frequency;

#pragma omp parallel for
    for ( int l = 0; l < lt; l++ ) {
        tt = l * dt;
        tt = tt - t0;
        double sp = PIE * frequency * tt;
        double fx = 100000.*exp ( -sp * sp ) * ( 1. - 2.*sp * sp );
        wave[l] = fx;
    }

    c0 = -2.927222164;
    c[0][0] = 1.66666665;
    c[0][1] = -0.23809525;
    c[0][2] = 0.03968254;
    c[0][3] = -0.004960318;
    c[0][4] = 0.0003174603;
    c[1][0] = 0.83333;
    c[1][1] = -0.2381;
    c[1][2] = 0.0595;
    c[1][3] = -0.0099;
    c[1][4] = 0.0008;


    for ( int i = 0; i < 5; i++ )
        for ( int j = 0; j < 5; j++ )
            c[2 + i][j] = c[1][i] * c[1][j];

    for(int i = 0;i<MIC_COUNT;++i){
        init_mic_flag[i] = false;
    }

}


MIC_VAR
inline
void gather_single_slice ( const int i_begin, const int i_end, const int j_begin, const int j_end, const int k,
        double * up_inner  , double * up_inner1 , double * up_inner2 , double * vp_inner  , double * vp_inner1 ,
        double * vp_inner2 , double * wp_inner  , double * wp_inner1 , double * wp_inner2 , double * us_inner  ,
        double * us_inner1 , double * us_inner2 , double * vs_inner  , double * vs_inner1 , double * vs_inner2 ,
        double * ws_inner  , double * ws_inner1 , double * ws_inner2 , double * u_inner   , double * v_inner   , double * w_inner,
        const int nMicMaxXLength, const int nMicMaxYLength)
{
    volatile int nIndex;
    // printf("k %d\n",k);
    // printf(" %d i_begin, %d i_end, %d j_begin, %d j_end, %d k,%p up_inner  , %p up_inner1 , %p up_inner2 , %p vp_inner  , %p vp_inner1 ,%p vp_inner2 , %p wp_inner  , %p wp_inner1 , %p wp_inner2 , %p us_inner  , %p us_inner1 , %p us_inner2 , %p vs_inner  , %p vs_inner1 , %p vs_inner2 ,%p ws_inner  , %p ws_inner1 , %p ws_inner2 , %p u_inner   , %p v_inner   , %p w_inner,%d nMicMaxXLength, %d nMicMaxYLength\n",
    //     i_begin, i_end, j_begin, j_end, k,
    //     up_inner  , up_inner1 , up_inner2 , vp_inner  , vp_inner1 ,
    //     vp_inner2 , wp_inner  , wp_inner1 , wp_inner2 , us_inner  ,
    //     us_inner1 , us_inner2 , vs_inner  , vs_inner1 , vs_inner2 ,
    //     ws_inner  , ws_inner1 , ws_inner2 , u_inner   , v_inner   , w_inner,
    //     nMicMaxXLength, nMicMaxYLength);

    for ( int j = j_begin; j < j_end; j++ )
        for ( int i = i_begin; i < i_end; i++ ) {
            nIndex = POSITION_INDEX_X(k,j,i);
            // printf("%d nIndex\n",nIndex);

            up_inner[nIndex] += ( 2 * up_inner1[nIndex] - up_inner2[nIndex] );
            vp_inner[nIndex] += ( 2 * vp_inner1[nIndex] - vp_inner2[nIndex] );
            wp_inner[nIndex] += ( 2 * wp_inner1[nIndex] - wp_inner2[nIndex] );
            us_inner[nIndex] += ( 2 * us_inner1[nIndex] - us_inner2[nIndex] );
            vs_inner[nIndex] += ( 2 * vs_inner1[nIndex] - vs_inner2[nIndex] );
            ws_inner[nIndex] += ( 2 * ws_inner1[nIndex] - ws_inner2[nIndex] );
            u_inner[nIndex] = up_inner[nIndex] + us_inner[nIndex];
            v_inner[nIndex] = vp_inner[nIndex] + vs_inner[nIndex];
            w_inner[nIndex] = wp_inner[nIndex] + ws_inner[nIndex];

        }//for(i=nleft;i<nright;i++) end
}

MIC_VAR
inline
void calc_single_slice (
        const int i_begin, const int i_end, const int j_begin, const int j_end, const int k,
        double * up_inner  , double * up_inner1 , double * up_inner2 , double * vp_inner  , double * vp_inner1 ,
        double * vp_inner2 , double * wp_inner  , double * wp_inner1 , double * wp_inner2 , double * us_inner  ,
        double * us_inner1 , double * us_inner2 , double * vs_inner  , double * vs_inner1 , double * vs_inner2 ,
        double * ws_inner  , double * ws_inner1 , double * ws_inner2 , double * u_inner   , double * v_inner   , double * w_inner,
        const int nMicMaxXLength, const int nMicMaxYLength, const int ntop, const int nleft, const int nfront, const int ncz_shot_new, const int l_inner,
        const int ncy_shot_inner,const int ncx_shot_inner,const double c0_inner ,const double dtx_inner,const double dtz_inner
        )
{
    double vvp2_dtx_dtx;
    double vvs2_dtz_dtz;
    double vvs2_dtx_dtx;
    double vvp2_dtz_dtz;
    double vvp2_dtz_dtx;
    double vvs2_dtz_dtx;

    double vvp2, vvs2, tempux2, tempuy2, tempuz2, tempvx2, tempvy2, tempvz2,
    tempwx2, tempwy2, tempwz2, tempuxz, tempuxy, tempvyz, tempvxy, tempwxz, tempwyz;

    double _tempuxz, _tempuxy, _tempvyz, _tempvxy, _tempwxz, _tempwyz;
    double px;
    double current_c;

    vvp2 = vpp2 ( k );
    vvs2 = vss2 ( k );

    vvs2_dtz_dtz = vvs2 * dtz_inner * dtz_inner;
    vvp2_dtx_dtx = vvp2 * dtx_inner * dtx_inner;
    vvs2_dtx_dtx = vvs2 * dtx_inner * dtx_inner;
    vvp2_dtz_dtz = vvp2 * dtz_inner * dtz_inner;
    vvp2_dtz_dtx = vvp2 * dtz_inner * dtx_inner;
    vvs2_dtz_dtx = vvs2 * dtz_inner * dtx_inner;

    bool print_debug_info = (nMicMaxXLength >= USE_MIC_MAX_LENGTH_THRESHOLD && k == 5);

    // if(print_debug_info) DEBUG_PRINT("k %d vvs2_dtz_dtz %.12lf vvp2_dtx_dtx %.12lf vvs2_dtx_dtx %.12lf vvp2_dtz_dtz %.12lf vvp2_dtz_dtx %.12lf vvs2_dtz_dtx %.12lf\n",k,vvs2_dtz_dtz,vvp2_dtx_dtx,vvs2_dtx_dtx,vvp2_dtz_dtz,vvp2_dtz_dtx,vvs2_dtz_dtx);

        for ( int j = j_begin; j < j_end; j++ ) {
            for ( int i = i_begin; i < i_end; i++ ) {

                int nIndex = POSITION_INDEX_X ( k, j, i );

                if ( i - 5 + nleft == ncx_shot_inner - 1 && j - 5 + nfront == ncy_shot_inner - 1 && k - 5 + ntop == ncz_shot_new - 1 ) {
                    px = 1.0;
                } else {
                    px = 0.0;
                }

                tempux2 = 0.0f;
                tempuy2 = 0.0f;
                tempuz2 = 0.0f;
                tempvx2 = 0.0f;
                tempvy2 = 0.0f;
                tempvz2 = 0.0f;
                tempwx2 = 0.0f;
                tempwy2 = 0.0f;
                tempwz2 = 0.0f;
                tempuxz = 0.0f;
                tempuxy = 0.0f;
                tempvyz = 0.0f;
                tempvxy = 0.0f;
                tempwxz = 0.0f;
                tempwyz = 0.0f;

                for ( int kk = 1; kk <= 5; kk++ ) {
                    tempux2 = tempux2 + c[0][kk - 1] * ( u_inner[nIndex + kk] + u_inner[nIndex - kk] );

                    tempvx2 = tempvx2 + c[0][kk - 1] * ( v_inner[nIndex + kk] + v_inner[nIndex - kk] );

                    tempwx2 = tempwx2 + c[0][kk - 1] * ( w_inner[nIndex + kk] + w_inner[nIndex - kk] );

                    tempuy2 = tempuy2 + c[0][kk - 1] * ( u_inner[POSITION_INDEX_X ( k, j + kk, i )] + u_inner[POSITION_INDEX_X ( k, j - kk, i )] );

                    tempvy2 = tempvy2 + c[0][kk - 1] * ( v_inner[POSITION_INDEX_X ( k, j + kk, i )] + v_inner[POSITION_INDEX_X ( k, j - kk, i )] );

                    tempwy2 = tempwy2 + c[0][kk - 1] * ( w_inner[POSITION_INDEX_X ( k, j + kk, i )] + w_inner[POSITION_INDEX_X ( k, j - kk, i )] );

                    tempuz2 = tempuz2 + c[0][kk - 1] * ( u_inner[POSITION_INDEX_X ( k + kk, j, i )] + u_inner[POSITION_INDEX_X ( k - kk, j, i )] );

                    tempvz2 = tempvz2 + c[0][kk - 1] * ( v_inner[POSITION_INDEX_X ( k + kk, j, i )] + v_inner[POSITION_INDEX_X ( k - kk, j, i )] );

                    tempwz2 = tempwz2 + c[0][kk - 1] * ( w_inner[POSITION_INDEX_X ( k + kk, j, i )] + w_inner[POSITION_INDEX_X ( k - kk, j, i )] );

                } //for(kk=1;kk<=5;kk++) end

                tempux2 = ( tempux2 + c0_inner * u_inner[nIndex] ) * vvp2_dtx_dtx;
                tempvx2 = ( tempvx2 + c0_inner * v_inner[nIndex] ) * vvs2_dtx_dtx;
                tempwx2 = ( tempwx2 + c0_inner * w_inner[nIndex] ) * vvs2_dtx_dtx;
                tempuy2 = ( tempuy2 + c0_inner * u_inner[nIndex] ) * vvs2_dtx_dtx;
                tempvy2 = ( tempvy2 + c0_inner * v_inner[nIndex] ) * vvp2_dtx_dtx;
                tempwy2 = ( tempwy2 + c0_inner * w_inner[nIndex] ) * vvs2_dtx_dtx;
                tempuz2 = ( tempuz2 + c0_inner * u_inner[nIndex] ) * vvs2_dtz_dtz;
                tempvz2 = ( tempvz2 + c0_inner * v_inner[nIndex] ) * vvs2_dtz_dtz;
                tempwz2 = ( tempwz2 + c0_inner * w_inner[nIndex] ) * vvp2_dtz_dtz;

                for ( int kkk = 1; kkk <= 5; kkk++ ) {
                    for ( int kk = 1; kk <= 5; kk++ ) {

                        // c[1 + kk][kkk - 1] = c[1 + kk][kkk - 1];

                        _tempuxy = ( u_inner[POSITION_INDEX_X ( k, j + kkk, i + kk )] + u_inner[POSITION_INDEX_X ( k, j - kkk, i - kk )] ) - ( u_inner[POSITION_INDEX_X ( k, j + kkk, i - kk )] + u_inner[POSITION_INDEX_X ( k, j - kkk, i + kk ) ] );
                        _tempvxy = ( v_inner[POSITION_INDEX_X ( k, j + kkk, i + kk )] + v_inner[POSITION_INDEX_X ( k, j - kkk, i - kk )] ) - ( v_inner[POSITION_INDEX_X ( k, j + kkk, i - kk )] + v_inner[POSITION_INDEX_X ( k, j - kkk, i + kk ) ] );


                        _tempvyz = ( v_inner[POSITION_INDEX_X ( k + kkk, j + kk, i )] + v_inner[POSITION_INDEX_X ( k - kkk, j - kk, i )] ) - ( v_inner[POSITION_INDEX_X ( k - kkk, j + kk, i )] + v_inner[POSITION_INDEX_X ( k + kkk, j - kk, i ) ] );
                        _tempwyz = ( w_inner[POSITION_INDEX_X ( k + kkk, j + kk, i )] + w_inner[POSITION_INDEX_X ( k - kkk, j - kk, i )] ) - ( w_inner[POSITION_INDEX_X ( k - kkk, j + kk, i )] + w_inner[POSITION_INDEX_X ( k + kkk, j - kk, i ) ] );


                        _tempuxz = ( u_inner[POSITION_INDEX_X ( k + kkk, j, i + kk )] + u_inner[POSITION_INDEX_X ( k - kkk, j, i - kk )] ) - ( u_inner[POSITION_INDEX_X ( k - kkk, j, i + kk )] + u_inner[POSITION_INDEX_X ( k + kkk, j, i - kk )]);
                        _tempwxz = ( w_inner[POSITION_INDEX_X ( k + kkk, j, i + kk )] + w_inner[POSITION_INDEX_X ( k - kkk, j, i - kk )] ) - ( w_inner[POSITION_INDEX_X ( k - kkk, j, i + kk )] + w_inner[POSITION_INDEX_X ( k + kkk, j, i - kk )]);

                        tempuxz = tempuxz + ( c[1 + kk][kkk - 1] * _tempuxz );
                        tempwxz = tempwxz + ( c[1 + kk][kkk - 1] * _tempwxz );

                        tempvyz = tempvyz + ( c[1 + kk][kkk - 1] * _tempvyz );
                        tempwyz = tempwyz + ( c[1 + kk][kkk - 1] * _tempwyz );

                        tempuxy = tempuxy + ( c[1 + kk][kkk - 1] * _tempuxy );
                        tempvxy = tempvxy + ( c[1 + kk][kkk - 1] * _tempvxy );

                    } // for(kkk=1;kkk<=5;kkk++) end
                } //for(kk=1;kk<5;kk++) end

                up_inner[POSITION_INDEX_X ( k, j, i )] = (tempux2 + tempvxy * vvp2_dtz_dtx)                                          + ( tempwxz * vvp2_dtz_dtx);
                vp_inner[POSITION_INDEX_X ( k, j, i )] = (tempuxy * vvp2_dtz_dtx          )  + ( tempvy2 + tempwyz * vvp2_dtz_dtx  );
                wp_inner[POSITION_INDEX_X ( k, j, i )] = (px * wave[l_inner - 1]          )  + ( tempvyz * vvp2_dtz_dtx    )         + ( tempwz2 + tempuxz * vvp2_dtz_dtx);

                us_inner[POSITION_INDEX_X ( k, j, i )] = (- tempvxy * vvs2_dtz_dtx        )  + ( tempuy2                           ) + ( tempuz2 - tempwxz * vvs2_dtz_dtx);
                vs_inner[POSITION_INDEX_X ( k, j, i )] = (tempvx2 - tempuxy * vvs2_dtz_dtx)  - (tempwyz * vvs2_dtz_dtx             ) + ( tempvz2 );
                ws_inner[POSITION_INDEX_X ( k, j, i )] = (tempwx2                         )  + (tempwy2 - tempvyz * vvs2_dtz_dtx   ) - ( tempuxz * vvs2_dtz_dtx );
           }
        }
}

MIC_VAR
void calc_single_l (
        const int i_begin, const int i_end, const int j_begin, const int j_end, const int k_begin, const int k_end,
        double * up_inner  , double * up_inner1 , double * up_inner2 , double * vp_inner  , double * vp_inner1 ,
        double * vp_inner2 , double * wp_inner  , double * wp_inner1 , double * wp_inner2 , double * us_inner  ,
        double * us_inner1 , double * us_inner2 , double * vs_inner  , double * vs_inner1 , double * vs_inner2 ,
        double * ws_inner  , double * ws_inner1 , double * ws_inner2 , double * u_inner   , double * v_inner   , double * w_inner,
        const int nMicMaxXLength, const int nMicMaxYLength, const int ntop, const int nleft, const int nfront, const int ncz_shot_new, const int l_inner,
        const int ncy_shot_inner,const int ncx_shot_inner,const double c0_inner ,const double dtx_inner,const double dtz_inner
        ) {

#pragma omp parallel for schedule(guided)
    for (int k = k_begin; k < k_end; ++k )
        calc_single_slice(
                    i_begin, i_end, j_begin, j_end, k,
                    up_inner  , up_inner1 , up_inner2 , vp_inner  , vp_inner1 ,
                    vp_inner2 , wp_inner  , wp_inner1 , wp_inner2 , us_inner  ,
                    us_inner1 , us_inner2 , vs_inner  , vs_inner1 , vs_inner2 ,
                    ws_inner  , ws_inner1 , ws_inner2 , u_inner   , v_inner   , w_inner,
                    nMicMaxXLength, nMicMaxYLength, ntop, nleft, nfront, ncz_shot_new, l_inner,
                    ncy_shot_inner,ncx_shot_inner,c0_inner ,dtx_inner,dtz_inner
                    );

#pragma omp parallel for schedule(guided)
    for (int k = k_begin; k < k_end; ++k )
        gather_single_slice ( i_begin, i_end, j_begin, j_end, k,
            up_inner  , up_inner1 , up_inner2 , vp_inner  , vp_inner1 ,
            vp_inner2 , wp_inner  , wp_inner1 , wp_inner2 , us_inner  ,
            us_inner1 , us_inner2 , vs_inner  , vs_inner1 , vs_inner2 ,
            ws_inner  , ws_inner1 , ws_inner2 , u_inner   , v_inner   , w_inner,
            nMicMaxXLength, nMicMaxYLength
            );

}

void calc_single_l_offload_to_mic(
        int i_begin, int i_end, int j_begin, int j_end,
        double * mic_up  , double * mic_up1 , double * mic_up2 , double * mic_vp  , double * mic_vp1 ,
        double * mic_vp2 , double * mic_wp  , double * mic_wp1 , double * mic_wp2 , double * mic_us  ,
        double * mic_us1 , double * mic_us2 , double * mic_vs  , double * mic_vs1 , double * mic_vs2 ,
        double * mic_ws  , double * mic_ws1 , double * mic_ws2 , double * mic_u   , double * mic_v   , double * mic_w,
        int nMicMaxXLength, int nMicMaxYLength, int ntop, int nleft, int nfront, int ncz_shot_new, int l_inner,
        int ncy_shot_inner,int ncx_shot_inner,double c0_inner ,double dtx_inner,double dtz_inner , int mic_device_id,
        int n_start_z_position, int mic_slice_size,
        double * mic_exchange_part_front_in_u , double * mic_exchange_part_front_in_v , double * mic_exchange_part_front_in_w,
        double * mic_exchange_part_back_in_u , double * mic_exchange_part_back_in_v , double * mic_exchange_part_back_in_w,
        double * mic_exchange_part_front_out_u , double * mic_exchange_part_front_out_v , double * mic_exchange_part_front_out_w,
        double * mic_exchange_part_back_out_u , double * mic_exchange_part_back_out_v , double * mic_exchange_part_back_out_w,
        int mic_z_length
        )
{
    double copy_length;

    // double * mic_exchange_part_front_u = mic_exchange_part_front_in_u;
    // double * mic_exchange_part_front_v = mic_exchange_part_front_in_v;
    // double * mic_exchange_part_front_w = mic_exchange_part_front_in_w;
    // double * mic_exchange_part_back_u  = mic_exchange_part_back_in_u ;
    // double * mic_exchange_part_back_v  = mic_exchange_part_back_in_v ;
    // double * mic_exchange_part_back_w  = mic_exchange_part_back_in_w ;

        if (!init_mic_flag[mic_device_id]) {

            init_mic_flag[mic_device_id] = true;

#ifdef SHOW_NORMAL_OUTPUT
            printf("First time initailize max access %d\n",POSITION_INDEX_X( 5 + mic_z_length,j_end-1,i_end-1));
#endif

#pragma offload_transfer target(mic:mic_device_id)\
            in(mic_up :length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_up1:length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_up2:length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_vp :length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_vp1:length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_vp2:length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_wp :length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_wp1:length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_wp2:length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_us :length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_us1:length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_us2:length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_vs :length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_vs1:length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_vs2:length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_ws :length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_ws1:length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(mic_ws2:length(mic_slice_size * ( mic_z_length+10)) MIC_ALLOC)\
            in(wave   :length(lt) MIC_ALLOC) \
            in(c: MIC_ALLOC)\
            in(mic_w  :length(mic_slice_size * (mic_z_length+10)) MIC_ALLOC)\
            in(mic_v  :length(mic_slice_size * (mic_z_length+10)) MIC_ALLOC)\
            in(mic_u  :length(mic_slice_size * (mic_z_length+10)) MIC_ALLOC)\
            in( mic_exchange_part_front_in_u: length ( 5 * mic_slice_size ) MIC_ALLOC )\
            in( mic_exchange_part_front_in_v: length ( 5 * mic_slice_size ) MIC_ALLOC )\
            in( mic_exchange_part_front_in_w: length ( 5 * mic_slice_size ) MIC_ALLOC )\
            in( mic_exchange_part_back_in_u : length ( 5 * mic_slice_size ) MIC_ALLOC )\
            in( mic_exchange_part_back_in_v : length ( 5 * mic_slice_size ) MIC_ALLOC )\
            in( mic_exchange_part_back_in_w : length ( 5 * mic_slice_size ) MIC_ALLOC )\
            nocopy( mic_exchange_part_front_out_u: length ( 5 * mic_slice_size ) MIC_ALLOC )\
            nocopy( mic_exchange_part_front_out_v: length ( 5 * mic_slice_size ) MIC_ALLOC )\
            nocopy( mic_exchange_part_front_out_w: length ( 5 * mic_slice_size ) MIC_ALLOC )\
            nocopy( mic_exchange_part_back_out_u : length ( 5 * mic_slice_size ) MIC_ALLOC )\
            nocopy( mic_exchange_part_back_out_v : length ( 5 * mic_slice_size ) MIC_ALLOC )\
            nocopy( mic_exchange_part_back_out_w : length ( 5 * mic_slice_size ) MIC_ALLOC )\
            signal ( mic_exchange_part_back_in_w )

        } else {

            #pragma offload_transfer target(mic:mic_device_id) \
            in( mic_exchange_part_front_in_u: length ( 5 * mic_slice_size ) MIC_REUSE )\
            in( mic_exchange_part_front_in_v: length ( 5 * mic_slice_size ) MIC_REUSE )\
            in( mic_exchange_part_front_in_w: length ( 5 * mic_slice_size ) MIC_REUSE )\
            in( mic_exchange_part_back_in_u : length ( 5 * mic_slice_size ) MIC_REUSE )\
            in( mic_exchange_part_back_in_v : length ( 5 * mic_slice_size ) MIC_REUSE )\
            in( mic_exchange_part_back_in_w : length ( 5 * mic_slice_size ) MIC_REUSE )\
                signal(mic_exchange_part_back_in_w)
        }

        // in(i_begin) in(i_end) in(j_begin), in(j_end) \
        // in(nMicMaxXLength) in(nMicMaxYLength) in(ntop) in(nleft) in(nfront) in(ncz_shot_shadow) in(l) \
#pragma offload target(mic:mic_device_id) \
        nocopy( mic_exchange_part_front_in_u: length ( 5 * mic_slice_size ) MIC_REUSE )\
        nocopy( mic_exchange_part_front_in_v: length ( 5 * mic_slice_size ) MIC_REUSE )\
        nocopy( mic_exchange_part_front_in_w: length ( 5 * mic_slice_size ) MIC_REUSE )\
        nocopy( mic_exchange_part_back_in_u : length ( 5 * mic_slice_size ) MIC_REUSE )\
        nocopy( mic_exchange_part_back_in_v : length ( 5 * mic_slice_size ) MIC_REUSE )\
        nocopy( mic_exchange_part_back_in_w : length ( 5 * mic_slice_size ) MIC_REUSE )\
        out(mic_exchange_part_front_out_u:length(5*mic_slice_size) MIC_REUSE)\
        out(mic_exchange_part_front_out_v:length(5*mic_slice_size) MIC_REUSE)\
        out(mic_exchange_part_front_out_w:length(5*mic_slice_size) MIC_REUSE)\
        out(mic_exchange_part_back_out_u :length(5*mic_slice_size) MIC_REUSE)\
        out(mic_exchange_part_back_out_v :length(5*mic_slice_size) MIC_REUSE)\
        out(mic_exchange_part_back_out_w :length(5*mic_slice_size) MIC_REUSE)\
        nocopy(mic_u  : MIC_REUSE)\
        nocopy(mic_v  : MIC_REUSE)\
        nocopy(mic_w  : MIC_REUSE)\
        nocopy(mic_up : MIC_REUSE)\
        nocopy(mic_up1: MIC_REUSE)\
        nocopy(mic_up2: MIC_REUSE)\
        nocopy(mic_vp : MIC_REUSE)\
        nocopy(mic_vp1: MIC_REUSE)\
        nocopy(mic_vp2: MIC_REUSE)\
        nocopy(mic_wp : MIC_REUSE)\
        nocopy(mic_wp1: MIC_REUSE)\
        nocopy(mic_wp2: MIC_REUSE)\
        nocopy(mic_us : MIC_REUSE)\
        nocopy(mic_us1: MIC_REUSE)\
        nocopy(mic_us2: MIC_REUSE)\
        nocopy(mic_vs : MIC_REUSE)\
        nocopy(mic_vs1: MIC_REUSE)\
        nocopy(mic_vs2: MIC_REUSE)\
        nocopy(mic_ws : MIC_REUSE)\
        nocopy(mic_ws1: MIC_REUSE)\
        nocopy(mic_ws2: MIC_REUSE)\
        wait(mic_exchange_part_back_w)\
        signal(mic_exchange_part_back_w)
        {
            // calc_single_l ( i_begin, i_end, j_begin, j_end, 5, k_mic_end - cpu_z_length,

            memcpy(  mic_u                                      , mic_exchange_part_front_in_u, sizeof ( double ) * 5 * mic_slice_size );
            memcpy(  mic_v                                      , mic_exchange_part_front_in_v, sizeof ( double ) * 5 * mic_slice_size );
            memcpy(  mic_w                                      , mic_exchange_part_front_in_w, sizeof ( double ) * 5 * mic_slice_size );
            memcpy( &mic_u[POSITION_INDEX_X(5+mic_z_length,0,0)], mic_exchange_part_back_in_u , sizeof ( double ) * 5 * mic_slice_size );
            memcpy( &mic_v[POSITION_INDEX_X(5+mic_z_length,0,0)], mic_exchange_part_back_in_v , sizeof ( double ) * 5 * mic_slice_size );
            memcpy( &mic_w[POSITION_INDEX_X(5+mic_z_length,0,0)], mic_exchange_part_back_in_w , sizeof ( double ) * 5 * mic_slice_size );

            calc_single_l ( i_begin, i_end, j_begin, j_end, 5, 5 + mic_z_length,
                    mic_up  , mic_up1 , mic_up2 , mic_vp  , mic_vp1 ,
                    mic_vp2 , mic_wp  , mic_wp1 , mic_wp2 , mic_us  ,
                    mic_us1 , mic_us2 , mic_vs  , mic_vs1 , mic_vs2 ,
                    mic_ws  , mic_ws1 , mic_ws2 , mic_u   , mic_v   , mic_w,
                    nMicMaxXLength, nMicMaxYLength, ntop + n_start_z_position, nleft, nfront, ncz_shot , l_inner,
                    ncy_shot,ncx_shot, c0 , dtx, dtz
            );

            memcpy ( mic_exchange_part_front_out_u, &(mic_u[POSITION_INDEX_X(5,0,0)]), sizeof ( double ) * 5 * mic_slice_size );
            memcpy ( mic_exchange_part_front_out_v, &(mic_v[POSITION_INDEX_X(5,0,0)]), sizeof ( double ) * 5 * mic_slice_size );
            memcpy ( mic_exchange_part_front_out_w, &(mic_w[POSITION_INDEX_X(5,0,0)]), sizeof ( double ) * 5 * mic_slice_size );

            memcpy ( mic_exchange_part_back_out_u , &(mic_u[POSITION_INDEX_X(mic_z_length,0,0)]), sizeof ( double ) * 5 * mic_slice_size );
            memcpy ( mic_exchange_part_back_out_v , &(mic_v[POSITION_INDEX_X(mic_z_length,0,0)]), sizeof ( double ) * 5 * mic_slice_size );
            memcpy ( mic_exchange_part_back_out_w , &(mic_w[POSITION_INDEX_X(mic_z_length,0,0)]), sizeof ( double ) * 5 * mic_slice_size );

            double *mic_swap_temp;
            mic_swap_temp = mic_up2; mic_up2 = mic_up1; mic_up1 = mic_up; mic_up = mic_swap_temp;
            mic_swap_temp = mic_vp2; mic_vp2 = mic_vp1; mic_vp1 = mic_vp; mic_vp = mic_swap_temp;
            mic_swap_temp = mic_wp2; mic_wp2 = mic_wp1; mic_wp1 = mic_wp; mic_wp = mic_swap_temp;
            mic_swap_temp = mic_us2; mic_us2 = mic_us1; mic_us1 = mic_us; mic_us = mic_swap_temp;
            mic_swap_temp = mic_vs2; mic_vs2 = mic_vs1; mic_vs1 = mic_vs; mic_vs = mic_swap_temp;
            mic_swap_temp = mic_ws2; mic_ws2 = mic_ws1; mic_ws1 = mic_ws; mic_ws = mic_swap_temp;
        }
}


void calc_shot (
        int ncx_shot,
        int ncy_shot,
        int lStart, int lEnd, int lMax,
        PMEMORY_BLOCKS pMemBlocks
        ) {

    int i_begin, i_end, j_begin, j_end, k_begin, k_end;
    int nMicXLength, nMicYLength, nMicZLength;
    int nMicMaxXLength, nMicMaxYLength, nMicMaxZLength;

    int n_mic_left, n_mic_right, n_mic_front, n_mic_back, n_mic_top, n_mic_bottom;

    double * to_write = pMemBlocks->to_write;
    double * up_out;

    double * up  = pMemBlocks->up;
    double * up1 = pMemBlocks->up1;
    double * up2 = pMemBlocks->up2;
    double * vp  = pMemBlocks->vp;
    double * vp1 = pMemBlocks->vp1;
    double * vp2 = pMemBlocks->vp2;
    double * wp  = pMemBlocks->wp;
    double * wp1 = pMemBlocks->wp1;
    double * wp2 = pMemBlocks->wp2;
    double * us  = pMemBlocks->us;
    double * us1 = pMemBlocks->us1;
    double * us2 = pMemBlocks->us2;
    double * vs  = pMemBlocks->vs;
    double * vs1 = pMemBlocks->vs1;
    double * vs2 = pMemBlocks->vs2;
    double * ws  = pMemBlocks->ws;
    double * ws1 = pMemBlocks->ws1;
    double * ws2 = pMemBlocks->ws2;
    double * u   = pMemBlocks->u;
    double * v   = pMemBlocks->v;
    double * w   = pMemBlocks->w;

    int ncz_shot_shaddow = ncz_shot;
    double xmax = lMax * dt * velmax;
    int nleft = ncx_shot - xmax / unit - 10;
    int nright = ncx_shot + xmax / unit + 10;
    int nfront = ncy_shot - xmax / unit - 10;
    int nback = ncy_shot + xmax / unit + 10;
    int ntop = ncz_shot - xmax / unit - 10;
    int nbottom = ncz_shot + xmax / unit + 10;

    ntop = ntop - 1;
    nfront = nfront - 1;
    nleft = nleft - 1;

    if ( nleft < 5 ) nleft = 5;
    if ( nright > nx - 5 ) nright = nx - 5;
    if ( nfront < 5 ) nfront = 5;
    if ( nback > ny - 5 ) nback = ny - 5;
    if ( ntop < 5 ) ntop = 5;
    if ( nbottom > nz - 5 ) nbottom = nz - 5;

    nMicMaxXLength = nright  - nleft;
    nMicMaxYLength = nback   - nfront;
    nMicMaxZLength = nbottom - ntop;

    int k_begin_n , k_end_n ,k_length_to_calc;

//  MIC CODE
    int k_length_to_calc;
    int mic_z_each_length;
    int mic_z_total_length;
    int cpu_z_length;
    int k_mic_begin;

    double * mic_up [MIC_COUNT];
    double * mic_up1[MIC_COUNT];
    double * mic_up2[MIC_COUNT];
    double * mic_vp [MIC_COUNT];
    double * mic_vp1[MIC_COUNT];
    double * mic_vp2[MIC_COUNT];
    double * mic_wp [MIC_COUNT];
    double * mic_wp1[MIC_COUNT];
    double * mic_wp2[MIC_COUNT];
    double * mic_us [MIC_COUNT];
    double * mic_us1[MIC_COUNT];
    double * mic_us2[MIC_COUNT];
    double * mic_vs [MIC_COUNT];
    double * mic_vs1[MIC_COUNT];
    double * mic_vs2[MIC_COUNT];
    double * mic_ws [MIC_COUNT];
    double * mic_ws1[MIC_COUNT];
    double * mic_ws2[MIC_COUNT];
    double * mic_u  [MIC_COUNT];
    double * mic_v  [MIC_COUNT];
    double * mic_w  [MIC_COUNT];

    double * mic_exchange_part_u[MIC_COUNT+1][2];

    double * mic_exchange_part_v[MIC_COUNT+1][2];

    double * mic_exchange_part_w[MIC_COUNT+1][2];

//  MIC CODE END

    if( l_parallel ) {
        int *a = (int *) calloc(group_size+1, sizeof(int));
        int len = nMicMaxZLength / group_size;
        int remain = nMicMaxZLength % group_size;
        a[0] = 5;
        for(int i = 1; i <= group_size; i++) {
            if ( i < remain ) a[i] = a[i-1] + len + 1;
            else a[i] = a[i-1] + len;
        }
        k_begin_n = a[group_pos]; k_end_n = a[group_pos+1];
        free(a);


// MIC CODE
        k_length_to_calc = k_end_n - k_begin_n;

    }else{
        k_begin_n = 5;
        k_length_to_calc = nMicMaxZLength;
    }

    mic_z_each_length = MIC_CPU_RATE * k_length_to_calc;

    if(mic_z_each_length * mic_slice_size > MIC_MAX_OFFLOAD_BYTES){
        mic_z_each_length = MIC_MAX_OFFLOAD_BYTES / mic_slice_size;

        printf("[-]Unable to offload %d slices for too large. Now offload %.2f of total for each mic, %d slices\n",
            (int)(MIC_CPU_RATE * k_length_to_calc),mic_z_each_length * 1. / k_length_to_calc, mic_z_each_length);
    }else if(mic_z_each_length < 5){

        mic_z_each_length = 5;

        printf("[-]Unable to calc on mic with slice size less than 5. Adjust to 5 now as %.2f of all\n",
            mic_z_each_length * 1. / k_length_to_calc);
    }

    mic_z_total_length = MIC_COUNT * mic_z_each_length;

    cpu_z_length = k_length_to_calc - mic_z_total_length;

    k_mic_begin =k_begin_n+cpu_z_length;

    for(int i = 0;i<MIC_COUNT;++i){
        mic_u[i] = &u[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i,0, 0 )];
        mic_v[i] = &v[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i,0, 0 )];
        mic_w[i] = &w[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i,0, 0 )];
    }

    for(int i = 0 ; i<MIC_COUNT+1 ;++i){
        mic_exchange_part_u[i][0] = ( double * ) calloc ( mic_slice_size * 5 , sizeof ( double ));
        mic_exchange_part_u[i][1] = ( double * ) calloc ( mic_slice_size * 5 , sizeof ( double ));
        mic_exchange_part_v[i][0] = ( double * ) calloc ( mic_slice_size * 5 , sizeof ( double ));
        mic_exchange_part_v[i][1] = ( double * ) calloc ( mic_slice_size * 5 , sizeof ( double ));
        mic_exchange_part_w[i][0] = ( double * ) calloc ( mic_slice_size * 5 , sizeof ( double ));
        mic_exchange_part_w[i][1] = ( double * ) calloc ( mic_slice_size * 5 , sizeof ( double ));
    }


// MIC CODE END
    for ( int l = lStart; l <= lEnd; l++ ) {
        xmax = l * dt * velmax;
        n_mic_left = ncx_shot - xmax / unit - 10;
        n_mic_right = ncx_shot + xmax / unit + 10;
        n_mic_front = ncy_shot - xmax / unit - 10;
        n_mic_back = ncy_shot + xmax / unit + 10;
        n_mic_top = ncz_shot - xmax / unit - 10;
        n_mic_bottom = ncz_shot + xmax / unit + 10;

        --n_mic_left;
        --n_mic_front;
        --n_mic_top;

        if ( n_mic_left < 5 ) n_mic_left = 5;
        if ( n_mic_right > nx - 5 ) n_mic_right = nx - 5;
        if ( n_mic_front < 5 ) n_mic_front = 5;
        if ( n_mic_back > ny - 5 ) n_mic_back = ny - 5;
        if ( n_mic_top < 5 ) n_mic_top = 5;
        if ( n_mic_bottom > nz - 5 ) n_mic_bottom = nz - 5;

        nMicXLength = n_mic_right  - n_mic_left ;
        nMicYLength = n_mic_back   - n_mic_front;
        nMicZLength = n_mic_bottom - n_mic_top  ;

        i_begin = 5 + n_mic_left - nleft;
        i_end   = n_mic_left - nleft + nMicXLength + 5;

        j_begin = 5 + n_mic_front - nfront;
        j_end   = n_mic_front - nfront + nMicYLength + 5;

        k_begin = 5 + n_mic_top - ntop;
        k_end   = n_mic_top - ntop + nMicZLength + 5;

        // mic_slice_size = (nMicMaxXLength +10) * (nMicMaxYLength + 10);

        if ( l > SPLIT_THRESHOLD && l_parallel ) {
            k_begin = k_begin_n;
            k_end = k_end_n;

            int exchangeSize = (nMicMaxXLength +10) * (nMicMaxYLength + 10) * 5;

            int left = mpi_id!=group_begin ? mpi_id-1 : MPI_PROC_NULL;
            int right = group_end!=mpi_id ? mpi_id+1 : MPI_PROC_NULL;

            MPI_Status status;
            // send to left and recieve from right
            MPI_Sendrecv(&u[POSITION_INDEX_X(k_begin, 0, 0)], exchangeSize, MPI_DOUBLE, left, 1,
                         &u[POSITION_INDEX_X(k_end  , 0, 0)], exchangeSize, MPI_DOUBLE, right, 1, MPI_COMM_WORLD, &status);
            MPI_Sendrecv(&v[POSITION_INDEX_X(k_begin, 0, 0)], exchangeSize, MPI_DOUBLE, left, 1,
                         &v[POSITION_INDEX_X(k_end  , 0, 0)], exchangeSize, MPI_DOUBLE, right, 1, MPI_COMM_WORLD, &status);
            MPI_Sendrecv(&w[POSITION_INDEX_X(k_begin, 0, 0)], exchangeSize, MPI_DOUBLE, left, 1,
                         &w[POSITION_INDEX_X(k_end  , 0, 0)], exchangeSize, MPI_DOUBLE, right, 1, MPI_COMM_WORLD, &status);

            // send to right and recieve from left
            MPI_Sendrecv(&u[POSITION_INDEX_X(k_end-5  , 0, 0)], exchangeSize, MPI_DOUBLE, right, 1,
                         &u[POSITION_INDEX_X(k_begin-5, 0, 0)], exchangeSize, MPI_DOUBLE, left, 1, MPI_COMM_WORLD, &status);
            MPI_Sendrecv(&v[POSITION_INDEX_X(k_end-5  , 0, 0)], exchangeSize, MPI_DOUBLE, right, 1,
                         &v[POSITION_INDEX_X(k_begin-5, 0, 0)], exchangeSize, MPI_DOUBLE, left, 1, MPI_COMM_WORLD, &status);
            MPI_Sendrecv(&w[POSITION_INDEX_X(k_end-5  , 0, 0)], exchangeSize, MPI_DOUBLE, right, 1,
                         &w[POSITION_INDEX_X(k_begin-5, 0, 0)], exchangeSize, MPI_DOUBLE, left, 1, MPI_COMM_WORLD, &status);
        }

        printf("%d now start calc_single_l %d, k_begin %d k_end %d\n", mpi_id, l, k_begin, k_end);

// MIC CODE

        if ( nMicXLength < USE_MIC_MAX_LENGTH_THRESHOLD ) {
            //
            // Do normal way
            //
#ifdef SHOW_NORMAL_OUTPUT
            printf ( "l %d started normal nMicMaxLength %d %d %d\n", l, nMicXLength, nMicYLength, nMicZLength );
#endif
            calc_single_l ( i_begin, i_end, j_begin, j_end, k_begin, k_end,
                    up  , up1 , up2 , vp  , vp1 ,
                    vp2 , wp  , wp1 , wp2 , us  ,
                    us1 , us2 , vs  , vs1 , vs2 ,
                    ws  , ws1 , ws2 , u   , v   , w,
                    nMicMaxXLength, nMicMaxYLength, ntop, nleft, nfront, ncz_shot, l ,
                    ncy_shot,ncx_shot, c0 , dtx, dtz
                    );

            double *swap_temp;
            swap_temp = up2; up2 = up1; up1 = up; up = swap_temp;
            swap_temp = vp2; vp2 = vp1; vp1 = vp; vp = swap_temp;
            swap_temp = wp2; wp2 = wp1; wp1 = wp; wp = swap_temp;
            swap_temp = us2; us2 = us1; us1 = us; us = swap_temp;
            swap_temp = vs2; vs2 = vs1; vs1 = vs; vs = swap_temp;
            swap_temp = ws2; ws2 = ws1; ws1 = ws; ws = swap_temp;
        } else {

            k_mic_end = k_end;
            k_end = k_mic_begin;

#ifdef SHOW_NORMAL_OUTPUT
            printf ( "l %d started mic %d %d nMicMaxLength %d %d %d\n", l,k_begin, k_end, nMicXLength, nMicYLength, nMicZLength );
#endif
            for(int i_mic=0;i_mic<MIC_COUNT;++i_mic){
                mic_up [i_mic] = &up [POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_up1[i_mic] = &up1[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_up2[i_mic] = &up2[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_vp [i_mic] = &vp [POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_vp1[i_mic] = &vp1[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_vp2[i_mic] = &vp2[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_wp [i_mic] = &wp [POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_wp1[i_mic] = &wp1[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_wp2[i_mic] = &wp2[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_us [i_mic] = &us [POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_us1[i_mic] = &us1[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_us2[i_mic] = &us2[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_vs [i_mic] = &vs [POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_vs1[i_mic] = &vs1[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_vs2[i_mic] = &vs2[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_ws [i_mic] = &ws [POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_ws1[i_mic] = &ws1[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
                mic_ws2[i_mic] = &ws2[POSITION_INDEX_X (k_mic_begin - 5 + mic_z_each_length * i_mic, 0,0)];
            }

            memcpy(mic_exchange_part_u[0][0],mic_u[0],mic_slice_size*5*sizeof(double));
            memcpy(mic_exchange_part_v[0][0],mic_v[0],mic_slice_size*5*sizeof(double));
            memcpy(mic_exchange_part_w[0][0],mic_w[0],mic_slice_size*5*sizeof(double));

            memcpy ( mic_exchange_part_u[MIC_COUNT][1] ,&(u[POSITION_INDEX_X(k_mic_end,0,0)]), sizeof ( double )* 5 * mic_slice_size );
            memcpy ( mic_exchange_part_v[MIC_COUNT][1] ,&(v[POSITION_INDEX_X(k_mic_end,0,0)]), sizeof ( double )* 5 * mic_slice_size );
            memcpy ( mic_exchange_part_w[MIC_COUNT][1] ,&(w[POSITION_INDEX_X(k_mic_end,0,0)]), sizeof ( double )* 5 * mic_slice_size );

            for(int i_mic=0;i_mic<MIC_COUNT;++i_mic){
                if(5 + cpu_z_length + i_mic * mic_z_each_length < k_mic_end ){
                    calc_single_l_offload_to_mic(
                        i_begin,  i_end,  j_begin,  j_end,
                        mic_up [i_mic] ,  mic_up1[i_mic] ,  mic_up2[i_mic] ,  mic_vp [i_mic] ,  mic_vp1[i_mic] ,
                        mic_vp2[i_mic] ,  mic_wp [i_mic] ,  mic_wp1[i_mic] ,  mic_wp2[i_mic] ,  mic_us [i_mic] ,
                        mic_us1[i_mic] ,  mic_us2[i_mic] ,  mic_vs [i_mic] ,  mic_vs1[i_mic] ,  mic_vs2[i_mic] ,
                        mic_ws [i_mic] ,  mic_ws1[i_mic] ,  mic_ws2[i_mic] ,  mic_u  [i_mic] ,  mic_v  [i_mic] ,  mic_w[i_mic],
                        nMicMaxXLength,  nMicMaxYLength,  ntop,  nleft,  nfront,  ncz_shot,  l,
                        ncy_shot, ncx_shot, c0 , dtx, dtz ,  i_mic,
                        cpu_z_length + i_mic * mic_z_each_length ,mic_slice_size,
                        mic_exchange_part_u[i_mic][0] , mic_exchange_part_v[i_mic][0] , mic_exchange_part_w[i_mic][0],
                        mic_exchange_part_u[i_mic+1][1] , mic_exchange_part_v[i_mic+1][1] , mic_exchange_part_w[i_mic+1][1],
                        mic_exchange_part_u[i_mic][1] , mic_exchange_part_v[i_mic][1] , mic_exchange_part_w[i_mic][1],
                        mic_exchange_part_u[i_mic+1][0] , mic_exchange_part_v[i_mic+1][0] , mic_exchange_part_w[i_mic+1][0],
                        mic_z_each_length
                    );
                }
            }

            calc_single_l ( i_begin, i_end, j_begin, j_end, k_begin, k_end,
                    up  , up1 , up2 , vp  , vp1 ,
                    vp2 , wp  , wp1 , wp2 , us  ,
                    us1 , us2 , vs  , vs1 , vs2 ,
                    ws  , ws1 , ws2 , u   , v   , w,
                    nMicMaxXLength, nMicMaxYLength, ntop, nleft, nfront,ncz_shot, l ,
                    ncy_shot,ncx_shot, c0 , dtx, dtz
                );

            double *swap_temp;
            swap_temp = up2; up2 = up1; up1 = up; up = swap_temp;
            swap_temp = vp2; vp2 = vp1; vp1 = vp; vp = swap_temp;
            swap_temp = wp2; wp2 = wp1; wp1 = wp; wp = swap_temp;
            swap_temp = us2; us2 = us1; us1 = us; us = swap_temp;
            swap_temp = vs2; vs2 = vs1; vs1 = vs; vs = swap_temp;
            swap_temp = ws2; ws2 = ws1; ws1 = ws; ws = swap_temp;

            for(int i_mic=0;i_mic<MIC_COUNT;++i_mic){
                if(init_mic_flag[i_mic]){
                    #pragma offload_wait target(mic:i_mic) wait(mic_exchange_part_w[i_mic+1][1])
                }
            }

            memcpy ( &(u[POSITION_INDEX_X(k_mic_begin,0,0)]), mic_exchange_part_u[0][1], sizeof ( double )* 5 * mic_slice_size );
            memcpy ( &(v[POSITION_INDEX_X(k_mic_begin,0,0)]), mic_exchange_part_v[0][1], sizeof ( double )* 5 * mic_slice_size );
            memcpy ( &(w[POSITION_INDEX_X(k_mic_begin,0,0)]), mic_exchange_part_w[0][1], sizeof ( double )* 5 * mic_slice_size );

            memcpy ( &(u[POSITION_INDEX_X(k_mic_end-5,0,0)]), mic_exchange_part_u[MIC_COUNT][0], sizeof ( double )* 5 * mic_slice_size );
            memcpy ( &(v[POSITION_INDEX_X(k_mic_end-5,0,0)]), mic_exchange_part_v[MIC_COUNT][0], sizeof ( double )* 5 * mic_slice_size );
            memcpy ( &(w[POSITION_INDEX_X(k_mic_end-5,0,0)]), mic_exchange_part_w[MIC_COUNT][0], sizeof ( double )* 5 * mic_slice_size );
        }

    } //for(l=1;l<=lt;l++) end

    if(init_mic_flag[0] == true)
    {
        // copy_length = mic_slice_size * ( mic_z_length + 10 );
        if ( OUT_PUT_SLICE_Z_INDEX-ntop >= k_mic_begin) {
            int output_device_id = (OUT_PUT_SLICE_Z_INDEX - k_mic_begin -ntop )/mic_z_each_length;

#ifdef SHOW_NORMAL_OUTPUT
            printf("Getting things back... on mic %d cpu_z_length %d mic_z_each_length %d %p\n",output_device_id,cpu_z_length,mic_z_each_length,mic_up1[output_device_id]);
#endif
            double * out_up1 = mic_up1[output_device_id];
            // ON MIC
#pragma offload target(mic:output_device_id) \
            out(out_up1 : length(mic_slice_size * ( mic_z_each_length + 10 )) MIC_REUSE)
            {}

#ifdef SHOW_NORMAL_OUTPUT
            printf("All back... \n");
#endif

        }

        for(int i_mic=0;i_mic<MIC_COUNT;++i_mic){
            if(init_mic_flag[i_mic]){
                printf("Free %d MIC data...uvw\n",i_mic);
                double * mic_free_u   =  mic_u[i_mic];
                double * mic_free_v   =  mic_v[i_mic];
                double * mic_free_w   =  mic_w[i_mic];
                double * mic_free_up1 =  mic_up1[i_mic];
                double * mic_free_up2 =  mic_up2[i_mic];
                double * mic_free_vp  =  mic_vp [i_mic];
                double * mic_free_vp1 =  mic_vp1[i_mic];
                double * mic_free_vp2 =  mic_vp2[i_mic];
                double * mic_free_wp  =  mic_wp [i_mic];
                double * mic_free_wp1 =  mic_wp1[i_mic];
                double * mic_free_wp2 =  mic_wp2[i_mic];
                double * mic_free_us  =  mic_us [i_mic];
                double * mic_free_us1 =  mic_us1[i_mic];
                double * mic_free_us2 =  mic_us2[i_mic];
                double * mic_free_vs  =  mic_vs [i_mic];
                double * mic_free_vs1 =  mic_vs1[i_mic];
                double * mic_free_vs2 =  mic_vs2[i_mic];
                double * mic_free_ws  =  mic_ws [i_mic];
                double * mic_free_ws1 =  mic_ws1[i_mic];
                double * mic_free_ws2 =  mic_ws2[i_mic];

                #pragma offload_transfer target(mic:i_mic) \
                        nocopy(mic_free_u:MIC_FREE)\
                        nocopy(mic_free_v:MIC_FREE)\
                        nocopy(mic_free_w:MIC_FREE)\
                        nocopy(mic_free_up1:MIC_FREE)\
                        nocopy(mic_free_up2:MIC_FREE)\
                        nocopy(mic_free_vp :MIC_FREE)\
                        nocopy(mic_free_vp1:MIC_FREE)\
                        nocopy(mic_free_vp2:MIC_FREE)\
                        nocopy(mic_free_wp :MIC_FREE)\
                        nocopy(mic_free_wp1:MIC_FREE)\
                        nocopy(mic_free_wp2:MIC_FREE)\
                        nocopy(mic_free_us :MIC_FREE)\
                        nocopy(mic_free_us1:MIC_FREE)\
                        nocopy(mic_free_us2:MIC_FREE)\
                        nocopy(mic_free_vs :MIC_FREE)\
                        nocopy(mic_free_vs1:MIC_FREE)\
                        nocopy(mic_free_vs2:MIC_FREE)\
                        nocopy(mic_free_ws :MIC_FREE)\
                        nocopy(mic_free_ws1:MIC_FREE)\
                        nocopy(mic_free_ws2:MIC_FREE)
            }
        }
    }

// MIC CODE END

    pMemBlocks->up    = up  ;
    pMemBlocks->up1   = up1 ;
    pMemBlocks->up2   = up2 ;
    pMemBlocks->vp    = vp  ;
    pMemBlocks->vp1   = vp1 ;
    pMemBlocks->vp2   = vp2 ;
    pMemBlocks->wp    = wp  ;
    pMemBlocks->wp1   = wp1 ;
    pMemBlocks->wp2   = wp2 ;
    pMemBlocks->us    = us  ;
    pMemBlocks->us1   = us1 ;
    pMemBlocks->us2   = us2 ;
    pMemBlocks->vs    = vs  ;
    pMemBlocks->vs1   = vs1 ;
    pMemBlocks->vs2   = vs2 ;
    pMemBlocks->ws    = ws  ;
    pMemBlocks->ws1   = ws1 ;
    pMemBlocks->ws2   = ws2 ;
    pMemBlocks->u     = u   ;
    pMemBlocks->v     = v   ;
    pMemBlocks->w     = w   ;

    if ( l_parallel ) {
        printf("%d's answer is at %d, calc in %d and %d\n", mpi_id, 169-ntop+5, k_begin_n, k_end_n);
        if ( 169-ntop+5 >= k_begin_n && 169-ntop+5 < k_end_n ) {
            up_out = ( double * ) calloc ( mic_slice_size , sizeof ( double ) );
            for ( int j = 5; j < nMicYLength + 5; j++ )
                for ( int i = 5; i < nMicXLength + 5; i++ ) {
                    up_out[POSITION_INDEX_X ( 0, j, i )] = up1[POSITION_INDEX_X ( 169 - ntop + 5, j, i )];
                }
            for ( int j = nfront; j < nback; j++ )
                for ( int i = nleft; i < nright; i++ ) {
                    to_write[POSITION_INDEX_HOST ( 0, j, i )] = up_out[POSITION_INDEX_X ( 0, j + 5 - nfront, i + 5 - nleft )];
                }

            if ( group_id == 1 ) {
                if ( mpi_id != root_id ) {
                    printf("%d got answer for 1, sending to root!\n", mpi_id);
                    MPI_Send(to_write, nSliceSize, MPI_DOUBLE, 0, 1, MPI_COMM_WORLD);
                }
            } else {
                printf("%d got answer for %d, sending to root!\n", mpi_id, group_id);
                MPI_Send(to_write, nSliceSize, MPI_DOUBLE, 0, group_id, MPI_COMM_WORLD);
            }
            free ( up_out );
        } else if ( mpi_id == root_id ) {
            MPI_Status status;
            MPI_Recv(to_write, nSliceSize, MPI_DOUBLE, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);
        }
    } else {
        printf("%d finished single shot.\n",mpi_id);
            up_out = ( double * ) calloc ( mic_slice_size , sizeof ( double ) );
        for ( int j = 5; j < nMicYLength + 5; j++ )
            for ( int i = 5; i < nMicXLength + 5; i++ ) {
                up_out[POSITION_INDEX_X ( 0, j, i )] = up1[POSITION_INDEX_X ( 169 - ntop + 5, j, i )];
            }
        for ( int j = nfront; j < nback; j++ )
            for ( int i = nleft; i < nright; i++ ) {
                to_write[POSITION_INDEX_HOST ( 0, j, i )] = up_out[POSITION_INDEX_X ( 0, j + 5 - nfront, i + 5 - nleft )];
            }
        free ( up_out );
    }

    for(int i = 0 ; i<MIC_COUNT+1 ;++i){
        free(mic_exchange_part_u[i][0]);
        free(mic_exchange_part_u[i][1]);
        free(mic_exchange_part_v[i][0]);
        free(mic_exchange_part_v[i][1]);
        free(mic_exchange_part_w[i][0]);
        free(mic_exchange_part_w[i][1]);
    }

}

int main ( int argc, char **argv ) {
    // MPI init begin
    MPI_Status status;
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_id);
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_sum);
    //MPI init end
    MEMORY_BLOCKS memory_blocks;
    if (mpi_id == root_id) {
        if ( argc < 4 ) {
            printf ( "please add 3 parameter: inpurfile, outfile, logfile\n" );
            exit ( 0 );
        }
        strcpy ( infile, argv[1] );
        strcpy ( outfile, argv[2] );
        strcpy ( logfile, argv[3] );
    }
    initailize();
    dtx = dt / unit;
    dtz = dt / unit;

    memory_blocks.u   = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.v   = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.w   = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.up  = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.up1 = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.up2 = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.vp  = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.vp1 = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.vp2 = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.wp  = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.wp1 = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.wp2 = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.us  = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.us1 = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.us2 = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.vs  = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.vs1 = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.vs2 = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.ws  = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.ws1 = ( double * ) calloc ( mic_used_size , sizeof ( double ));
    memory_blocks.ws2 = ( double * ) calloc ( mic_used_size , sizeof ( double ));

    for(int i =0;i<MIC_COUNT;++i)
    {
        #pragma offload_transfer target(mic:i) in(dtx) in(dtz) in(ncy_shot) in(ncx_shot)
    }
    fout = fopen ( outfile, "wb" );

    // assume that the number of cores is greater than the number of shots
    if ( mpi_sum >= nshot ) {
        l_parallel = true;
        memory_blocks.to_write = ( double* ) calloc ( nSliceSize, sizeof ( double ) );
        ///
        double xmax = lt * dt * velmax;
        int ntop = ncz_shot - xmax / unit - 10;
        int nbottom = ncz_shot + xmax / unit + 10;


        ntop = ntop - 1;


        if ( ntop < 5 ) ntop = 5;
        if ( nbottom > nz - 5 ) nbottom = nz - 5;


        int nMicMaxZLength = nbottom - ntop;
        printf("nMicMaxZLength %d\n", nMicMaxZLength);

        ///
        group_size = mpi_sum / nshot;

        if ( nMicMaxZLength / group_size <= MIN_SLICE_LENGTH ) {
            group_size = nMicMaxZLength / (MIN_SLICE_LENGTH + 1);
            printf("new group_size: %d, nMicMaxZLength: %d\n", group_size, nMicMaxZLength);
            total_sum = mpi_sum;
            mpi_sum = nshot * group_size;
            printf("mpi_sum: %d\n", mpi_sum);
            if (mpi_id >= mpi_sum) {
                int flag = 1;
                MPI_Status status;
                printf("Too many nodes. %d exit!\n", mpi_id);
                MPI_Recv(&flag, 1, MPI_INT, 0, mpi_id, MPI_COMM_WORLD, &status);
                printf("Now %d exits!\n", mpi_id);
                MPI_Finalize();
                return 0;
            }
        }

        core_remain = mpi_sum % nshot;
        group_limit = (group_size + 1) * core_remain; // mpi_id in [0, group_limit) is in groups with (group_size+1) cores
        if ( mpi_id >= group_limit ) {
            // group count start with 0 while core count start with 0
            group_id = core_remain + ( mpi_id - group_limit ) / group_size;
            group_begin = group_limit + (mpi_id - group_limit) / group_size * group_size;
        } else {
            group_id = mpi_id / (group_size + 1);
            group_begin = group_id * (group_size + 1);
            group_size++;
        }
        group_end = group_begin + group_size - 1;
        group_pos = mpi_id - group_begin;
        group_id ++ ;
        printf("mpi_id: %d group_size: %d\n", mpi_id, group_size);
        printf("GROUP %d %d %d %d %d %d %d %d\n", mpi_id, group_id, group_size, group_limit, core_remain, mpi_sum, group_begin, group_end);

        printf("%d shot=%d\n", mpi_id, group_id);

        ncy_shot = ncy_shot1 + ( group_id / nxshot ) * dyshot;
        ncx_shot = ncx_shot1 + ( group_id % nxshot ) * dxshot;

        if ( lt > SPLIT_THRESHOLD ) {
            calc_shot ( ncx_shot, ncy_shot, 1, SPLIT_THRESHOLD, lt, &memory_blocks);
            calc_shot ( ncx_shot, ncy_shot, SPLIT_THRESHOLD+1, lt, lt, &memory_blocks);
        }

        printf("%d finished calculating shot %d!\n", mpi_id, group_id);

        if ( mpi_id == root_id ) {
            fout = fopen ( outfile, "wb" );
            fwrite ( memory_blocks.to_write, sizeof ( double ), nSliceSize, fout );
            printf("Finished Writing 1\n");
            for(int i = 2; i <= nshot; i++) {
                printf("NOW 0 is waiting data %d !\n", i);
                MPI_Status status;
                MPI_Recv(memory_blocks.to_write, nSliceSize, MPI_DOUBLE, MPI_ANY_SOURCE, i, MPI_COMM_WORLD, &status);
                fwrite ( memory_blocks.to_write, sizeof ( double ), nSliceSize, fout );
                printf ("NOW 0 finished writing data %d !\n", i);
            }
            for(int i = mpi_sum; i < total_sum; i++) {
                int flag = 1;
                MPI_Status status;
                printf("sending signal to %d to exit\n", i);
                MPI_Send(&flag, 1, MPI_INT, i, i, MPI_COMM_WORLD);
            }
            fclose ( fout );
        }
    } else {
        l_parallel = false;
        int pshot, shot_remain, shot_begin, shot_end, shot_limit;
        // shots from 1 to nshot;
        pshot = nshot/mpi_sum;
        shot_remain = nshot % mpi_sum;
        if ( mpi_id >= mpi_sum - shot_remain ) {
            shot_limit = mpi_sum - shot_remain;
            shot_begin = shot_limit * pshot + 1;
            pshot++;
            shot_begin += pshot * (mpi_id - shot_limit);
            shot_end = shot_begin + pshot;
        } else {
            shot_begin = mpi_id*pshot+1;
            shot_end = shot_begin + pshot - 1;
        }
        printf("%d got %d tasks, shot %d to %d \n", mpi_id, pshot, shot_begin, shot_end);

        write_buf = memory_blocks.to_write = ( double* ) calloc ( nSliceSize * (pshot+1), sizeof ( double ) );
        printf("%x %x\n",write_buf, memory_blocks.to_write);
        for ( ishot = shot_begin; ishot <= shot_end; ishot++ ) {
            printf("nshot is %d\n", nshot);

            ncy_shot = ncy_shot1 + ( ishot / nxshot ) * dyshot;
            ncx_shot = ncx_shot1 + ( ishot % nxshot ) * dxshot;

            calc_shot ( ncx_shot, ncy_shot, 1, lt, lt, &memory_blocks);

            memset(memory_blocks.u,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.v,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.w,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.up ,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.up1,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.up2,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.vp ,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.vp1,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.vp2,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.wp ,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.wp1,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.wp2,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.us ,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.us1,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.us2,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.vs ,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.vs1,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.vs2,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.ws ,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.ws1,0,sizeof(double)*mic_used_size);
            memset(memory_blocks.ws2,0,sizeof(double)*mic_used_size);
            memory_blocks.to_write += nSliceSize;
        }
        if ( mpi_id == root_id ) {
            fout = fopen ( outfile, "wb" );
            fwrite ( write_buf, sizeof ( double ), nSliceSize*pshot, fout );
            int flag = 0;
            for(int i = 1; i < mpi_sum; i++) {
                flag = 1;
                int length;
                MPI_Status status;
                printf("sending signal to %d\n", i);
                MPI_Send(&flag, 1, MPI_INT, i, i, MPI_COMM_WORLD);
                printf("signal is already sent to %d\n", i);
                MPI_Recv(write_buf, nSliceSize*(pshot+1), MPI_DOUBLE, i, i, MPI_COMM_WORLD, &status);
                printf("root got data from %d\n", status.MPI_TAG);
                MPI_Get_count(&status, MPI_DOUBLE, &length);
                printf("get data from %d, length %d, should be %d\n", i, length, nSliceSize*pshot);
                fwrite ( write_buf, sizeof ( double ), length, fout );
            }
            for(int i = mpi_sum; i < total_sum; i++) {
                flag = 1;
                MPI_Status status;
                printf("sending signal to %d to exit\n", i);
                MPI_Send(&flag, 1, MPI_INT, i, i, MPI_COMM_WORLD);
            }
            printf("Root finished!\n");
            fclose(fout);
        } else {
            int flag = 0;
            printf("Now %d is waiting for signal from root\n", mpi_id);
            MPI_Recv(&flag, 1, MPI_INT, 0, mpi_id, MPI_COMM_WORLD, &status);
            printf("Now %d prepared to send data of %d to %d to root\n", mpi_id, shot_begin, shot_end);
            if ( flag ) {
                MPI_Send(write_buf, nSliceSize*pshot, MPI_DOUBLE, 0, mpi_id, MPI_COMM_WORLD);
                printf("%d Finished sending! Will be terminated!\n", mpi_id);
            }
        }
    }

    MPI_Finalize();

    if ( mpi_id == root_id ) {
        gettimeofday ( &end, NULL );
        all_time = ( end.tv_sec - start.tv_sec ) + ( double ) ( end.tv_usec - start.tv_usec ) / 1000000.0;
        printf ( "run time:\t%lf s\n", all_time );
        flog = fopen ( logfile, "a" );
        fprintf ( flog, "\nrun time:\t%lf s\n\n", all_time );
        fclose ( flog );
        flog = fopen ( logfile, "a" );
        fprintf ( flog, "------------end time------------\n" );
        fclose ( flog );
        system ( tmp );
    }
    return 0;
}

