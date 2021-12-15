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
     
    // PID err
    float Perr;
    float Ierr;
    float Derr;

    // PID terms limits
    float Outmin;
    float Outmax;
    float Ierrmin;
    float Ierrmax;
} PIDdata;
typedef PIDdata *ptrPIDdata;

void PID_SetTraget(ptrPIDdata pid, float target, float prev_input);
void PID_SetTuningParams(ptrPIDdata pid, float Kp, float Ki, float Kd);
void PID_SetLimits(ptrPIDdata pid, float Outmin, float Outmax, float Ierrmin, float Ierrmax);
void PID_ResetIerr(ptrPIDdata pid);
float PID_Update(ptrPIDdata pid, float input);


#endif /* PID_H */