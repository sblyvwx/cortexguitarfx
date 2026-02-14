#ifndef _SECOND_ORDER_IIR_FILTER_H_
#define _SECOND_ORDER_IIR_FILTER_H_
#include <stdint.h>
typedef struct 
{
    int16_t x1,x2,y1,y2;
    int32_t acc;
    const int32_t coeffA[2];
    const int32_t coeffB[3];
} SecondOrderIirFilterType;

void initSecondOrderIirFilter(SecondOrderIirFilterType* data);
void secondOrderIirFilterReset(SecondOrderIirFilterType*data);

#ifdef RP2040_FEATHER
static inline __attribute__((always_inline)) int16_t secondOrderIirFilterProcessSample(int16_t sampleIn,SecondOrderIirFilterType*data)
{
    int16_t res;
    data->acc += data->coeffB[0]*sampleIn;
    data->acc += data->coeffB[1]*data->x1;
    data->acc += data->coeffB[2]*data->x2;
    data->acc -= data->coeffA[0]*data->y1;
    data->acc -= data->coeffA[1]*data->y2;
    if (data->acc > ((1 << 29)-1))
    {
        data->acc = ((1 << 29)-1); 
    }   
    if (data->acc < -(1 << 29))
    {
        data->acc = -(1 << 29);
    }
    res = (int16_t)(data->acc >> 14);
    data->x2 = data->x1;
    data->x1 = sampleIn;
    data->y2 = data->y1;
    data->y1 = res;
    data->acc &= ((1 << 14)-1);
    return res;
}
#else
int16_t secondOrderIirFilterProcessSample(int16_t sampleIn,SecondOrderIirFilterType*data);
#endif

#endif