#ifndef PID_H
#define PID_H

typedef struct _PIDdata {

    float prev_input;

    float target;

    // PID factors
    float Kp;
    float Ki;
    float Kd;
    float dT;
     
    // PID terms
    float Pterm;
    float Iterm;
    float Dterm;

    // PID terms limits
    float Outmin;
    float Outmax;
    float Iterm_min;
    float Iterm_max;
} PIDdata;
typedef PIDdata *ptrPIDdata;

void PID_SetTraget(ptrPIDdata pid, float target, float prev_input);
void PID_SetTuningParams(ptrPIDdata pid, float Kp, float Ki, float Kd);
void PID_SetLimits(ptrPIDdata pid, float Outmin, float Outmax, float Iterm_min, float Iterm_max);
void PID_ResetIterm(ptrPIDdata pid);
float PID_Update(ptrPIDdata pid, float input);


#endif /* PID_H */