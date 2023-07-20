/* This file is generated by BUSMASTER */
/* VERSION [1.2] */
/* BUSMASTER VERSION [3.2.2] */
/* PROTOCOL [CAN] */

/* Start BUSMASTER include header */
#include <Windows.h>
#include <CANIncludes.h>
/* End BUSMASTER include header */


/* Start BUSMASTER global variable */
STCAN_MSG tx;
int steering;
int inertial;
/* End BUSMASTER global variable */


/* Start BUSMASTER Function Prototype  */
GCC_EXTERN void GCC_EXPORT OnTimer_miscTmr_25( );
/* End BUSMASTER Function Prototype  */

/* Start BUSMASTER Function Wrapper Prototype  */
/* End BUSMASTER Function Wrapper Prototype  */


/* Start BUSMASTER generated function - OnTimer_miscTmr_25 */
void OnTimer_miscTmr_25( )
{
/* TODO */
steering= rand()%5 + 30;
inertial= rand()%5;

tx.id=0x68;
tx.dlc=8;
tx.data[0]=steering >> 8;
tx.data[1]=steering;
tx.data[2]=inertial >> 8;
tx.data[3]=inertial;
tx.data[4]=0;
tx.data[5]=0;
tx.data[6]=0;
tx.data[7]=0;
SendMsg(tx);
}/* End BUSMASTER generated function - OnTimer_miscTmr_25 */
