#include "pid.h"

void PID_SetTraget(ptrPIDdata pid, float target, float prev_input)
{
    pid->target = target;
    pid->prev_input = prev_input;
    PID_ResetIerr(pid);
}

/*
 *  Set coefficients
 */
void PID_SetTuningParams(ptrPIDdata pid, float Kp, float Ki, float Kd) 
{
    pid->Kp = Kp;
    pid->Ki = Ki;
    pid->Kd = Kd;
}


/*
 *  Set term limits
 */
void PID_SetLimits(ptrPIDdata pid, float Outmin, float Outmax, float Ierrmin, float Ierrmax)
{
    pid->Outmin = Outmin;
    pid->Outmax = Outmax;
    pid->Ierrmin = Ierrmin;
    pid->Ierrmax = Ierrmax;
}

/*
 *  Reset integral term accumulated error
 */
void PID_ResetIerr(ptrPIDdata pid)
{
    pid->Ierr = 0.0f;
}

/*
 *  PID control algorithm. If this function get called always at the same period, dt=1 can be used,
 *  otherwise it should be calculated
 */
float PID_Update(ptrPIDdata pid, float input) {
    // compute P error
    pid->Perr = pid->target - input;
    // compute I error
    pid->Ierr +=  pid->Ki * pid->Perr * pid->dT;
    if (pid->Ierr < pid->Ierrmin) {
        pid->Ierr = pid->Ierrmin;
    }
    else if (pid->Ierr > pid->Ierrmax) {
        pid->Ierr = pid->Ierrmax;
    }
    // compute D error
    pid->Derr = (pid->prev_input - input) / pid->dT;

    // record last value
    pid->prev_input = input;

    // clip result
    float result = (pid->Perr * pid->Kp) + pid->Ierr + (pid->Derr * pid->Kd);

    if (result > pid->Outmax) result = pid->Outmax;
    else if (result < pid->Outmin) result = pid->Outmin;
    return result;
}