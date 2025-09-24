#ifndef __APP_EMA_H__
#define __APP_EMA_H__

typedef struct {
    float y;
    bool init;
    float alpha;
} ema_t;
static inline void ema_init(ema_t *e, float alpha) {
    e->alpha = alpha;
    e->init = false;
}
static inline float ema_apply(ema_t *e, float x) {
    if (!e->init) {
        e->y = x;
        e->init = true;
        return e->y;
    }
    e->y = e->alpha * x + (1.0f - e->alpha) * e->y;
    return e->y;
}

#endif // __APP_EMA_H__
