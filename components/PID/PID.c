#include "pid.h"

void PID_SetTraget(ptrPIDdata pid, float target, float prev_input)
{
    pid->target = target;
    pid->prev_input = prev_input;
    PID_ResetIterm(pid);
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
void PID_SetLimits(ptrPIDdata pid, float Outmin, float Outmax, float Iterm_min, float Iterm_max)
{
    pid->Outmin = Outmin;
    pid->Outmax = Outmax;
    pid->Iterm_min = Iterm_min;
    pid->Iterm_max = Iterm_max;
}

/*
 *  Reset integral term accumulated error
 */
void PID_ResetIterm(ptrPIDdata pid)
{
    pid->Iterm = 0.0f;
}

/*
 *  PID control algorithm. If this function get called always at the same period, dt=1 can be used,
 *  otherwise it should be calculated
 */
float PID_Update(ptrPIDdata pid, float input) {
    // compute P error
    pid->Pterm = pid->Kp * (pid->target - input);
    
    // compute I error
    pid->Iterm +=  pid->Ki * (pid->target - input) * pid->dT;
    if (pid->Iterm < pid->Iterm_min) pid->Iterm = pid->Iterm_min;
    else if (pid->Iterm > pid->Iterm_max)  pid->Iterm = pid->Iterm_max;

    // compute D error
    pid->Dterm = pid->Kd * (pid->prev_input - input) / pid->dT;

    // record last value
    pid->prev_input = input;

    // clip result
    float result = pid->Pterm + pid->Iterm + pid->Dterm;
    if (result > pid->Outmax - 30) result = pid->Outmax; // 30ms leeway fot the ssr
    else if (result < pid->Outmin + 30) result = pid->Outmin;

    return result; 
}