/*
 Gigaverb/Gverb algorithm
 
 Original code by Juhana Sadeharju Copyright (C) 1999 kouhia at nic.funet.fi
 
 Ported to Pure Data, Edited, reviewed and modified
 by Alexandre Torres Porres (c) 2019 to include it in the ELSE library
 
 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more delates.
 
 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "m_pd.h"
#include <math.h>
#include <string.h>

typedef struct{
    int    size;
    int    idx;
    float *buf;
}t_fixeddelay;

typedef struct{
    int    size;
    float  coeff;
    int    idx;
    float *buf;
}t_diffuser;

typedef struct{
    float  damp;
    float  delay;
}t_damper;

typedef struct{
    t_object        x_obj;
    int             x_sr;           // sample rate
    float           x_in_bw;        // input bandwidth
    float           x_dry;          // dry level
    float           x_late;         // late reflections level
    float           x_early;        // early reflections level
    float           x_wet;          // wet level
    float           x_maxsize;      // maximum room size
    float           x_size;         // room size
    float           x_decay;        // decay time in seconds
    float           x_maxdelay;
    float           x_largestdelay;
    t_damper       *x_in_damper;
    t_damper      **x_fdndamps;
    t_fixeddelay  **x_fdndels;
    t_fixeddelay   *x_tapdelay;
    float          *x_fdngains;
    int            *x_fdnlens;
    float           x_fdndamp;
    t_diffuser    **x_ldifs;
    t_diffuser    **x_rdifs;
    int            *x_taps;
    float          *x_tapgains;
    float          *x_d;
    float          *x_u;
    float          *x_f;
    double          x_alpha;
}t_gverb;

void *gverb_class;

/* This FDN reverb can be smoothened by setting the matrix elements at the
 * diagonal and near of it to zero or nearly zero. By setting diagonals to zero
 * means we remove the effect of the parallel comb structure from the
 * reverberation.  A comb generates uniform impulse stream to the reverberation
 * impulse response, and thus it is not good. By setting near diagonal elements
 * to zero means we remove delay sequences having consequtive delays of the
 * similar lenths, when the delays are in sorted in length with respect to
 * matrix element index. The matrix described here could be generated by
 * differencing Rocchesso's circulant matrix at max diffuse value and at low
 * diffuse value (approaching parallel combs).
 *
 * Example 1: Set a(k, k), for all k, equal to 0.
 *
 * Example 2: Set a(k, k), a(k, k-1) and a(k, k+1) equal to 0.
 *
 * Example 3: The transition to zero gains could be smooth as well.
 * a(k, k-1) and a(k, k+1) could be 0.3, and a(k, k-2) and a(k, k+2) could
 * be 0.5, say.
 */
static inline void gverb_fdn_matrix(float *a, float *b){
    const float dl0 = a[0], dl1 = a[1], dl2 = a[2], dl3 = a[3];
    b[0] = 0.5f*(+dl0 + dl1 - dl2 - dl3);
    b[1] = 0.5f*(+dl0 - dl1 - dl2 + dl3);
    b[2] = 0.5f*(-dl0 + dl1 - dl2 + dl3);
    b[3] = 0.5f*(+dl0 + dl1 + dl2 + dl3);
}

static inline float diffuser_do(t_diffuser *d, float f){
    f = f - d->buf[d->idx]*d->coeff;
    if(PD_BADFLOAT(f)) f = 0.0f;
    float y = d->buf[d->idx] + f*d->coeff;
    d->buf[d->idx] = f;
    d->idx = (d->idx + 1) % d->size;
    return(y);
}

static inline float fixeddelay_read(t_fixeddelay *d, int n){
    int i = (d->idx - n + d->size) % d->size;
    return(d->buf[i]);
}

static inline void fixeddelay_write(t_fixeddelay *d, float f){
    if(PD_BADFLOAT(f)) f = 0.0f;
    d->buf[d->idx] = f;
    d->idx = (d->idx + 1) % d->size;
}

static inline void damper_set(t_damper *d, float f){
    d->damp = f; // clip?
}

static inline float damper_do(t_damper *d, float f){
    d->delay = f*(1.0-d->damp) + d->delay*d->damp;
    return(d->delay);
}

static inline void gverb_do(t_gverb *x, float in, float *l, float *r){
    float z;
    unsigned int i;
    float lsum, rsum, sum, sign;
    if(PD_BADFLOAT(in) || fabsf(in) > 100000.0f) in = 0.0f;
    z = damper_do(x->x_in_damper, in);
    z = diffuser_do(x->x_ldifs[0], z);
    for(i = 0; i < 4; i++)
        x->x_u[i] = x->x_tapgains[i]*fixeddelay_read(x->x_tapdelay,x->x_taps[i]);
    fixeddelay_write(x->x_tapdelay, z);
    for(i = 0; i < 4; i++){
        x->x_d[i] = damper_do(x->x_fdndamps[i],
                            x->x_fdngains[i]*fixeddelay_read(x->x_fdndels[i],
                                                           x->x_fdnlens[i]));
    }
    sum = 0.0f;
    sign = 1.0f;
    for(i = 0; i < 4; i++){
        sum += sign*(x->x_late*x->x_d[i] + x->x_early*x->x_u[i]);
        sign = -sign;
    }
    lsum = rsum = (sum += in*x->x_early);
    gverb_fdn_matrix(x->x_d, x->x_f);
    for(i = 0; i < 4; i++)
        fixeddelay_write(x->x_fdndels[i], x->x_u[i]+x->x_f[i]);
    lsum = diffuser_do(x->x_ldifs[1], lsum);
    lsum = diffuser_do(x->x_ldifs[2], lsum);
    lsum = diffuser_do(x->x_ldifs[3], lsum);
    rsum = diffuser_do(x->x_rdifs[1], rsum);
    rsum = diffuser_do(x->x_rdifs[2], rsum);
    rsum = diffuser_do(x->x_rdifs[3], rsum);
    *l = lsum;
    *r = rsum;
}

int isprime(int n){
    const unsigned int lim = (int)sqrtf((float)n);
    if(n == 2) return(1);
    if((n & 1) == 0) return(0);
    for(unsigned int i = 3; i <= lim; i += 2)
        if((n % i) == 0) return(0);
    return(1);
}

int nearest_prime(int n, float rerror){
    if(isprime(n)) return(n);
    int bound = n*rerror; // assume n is big enough & n*rerror enough < n
    for(int k = 1; k <= bound; k++){
        if(isprime(n+k)) return(n+k);
        if(isprime(n-k)) return(n-k);
    }
    return(-1);
}

int ff_trunc(float f){ // Truncate float to int
    f -= 0.5f;
    f += (3<<22);
    return *((int*)&f) - 0x4b400000;
}

int ff_round(float f){ // Round float to int (faster than f_trunc)
    f += (3<<22);
    return *((int*)&f) - 0x4b400000;
}

t_diffuser *diffuser_make(int size, float coeff){
    t_diffuser *d = (t_diffuser *)t_getbytes(sizeof(t_diffuser));
    if(!d) return (NULL);
    d->size = size;
    d->coeff = coeff;
    d->idx = 0;
    d->buf = (float *)t_getbytes(size*sizeof(float));
    if(!d->buf) return (NULL);
    for(int i = 0; i < size; i++) d->buf[i] = 0.0;
    return(d);
}

void diffuser_free(t_diffuser *d){
    t_freebytes(d->buf, d->size*sizeof(float));
    t_freebytes(d, sizeof(t_diffuser));
}

void diffuser_clear(t_diffuser *d){
    memset(d->buf, 0, d->size * sizeof(float));
}

t_damper *damper_make(float f){
    t_damper *d = (t_damper *)t_getbytes(sizeof(t_damper));
    if(!d) return (NULL);
    d->damp = f;
    d->delay = 0.0;
    return(d);
}

void damper_free(t_damper *d){
    t_freebytes(d, sizeof(t_damper));
}

void damper_clear(t_damper *d){
    d->delay = 0.0f;
}

void fixeddelay_clear(t_fixeddelay *d){
    memset(d->buf, 0, d->size * sizeof(float));
}

t_fixeddelay *fixeddelay_make(int size){
    t_fixeddelay *d = (t_fixeddelay *)t_getbytes(sizeof(t_fixeddelay));
    if(!d) return (NULL);
    d->size = size;
    d->idx = 0;
    d->buf = (float *)t_getbytes(size*sizeof(float));
    if(!d->buf) return (NULL);
    for(int i = 0; i < size; i++)
        d->buf[i] = 0.0;
    return(d);
}

void fixeddelay_free(t_fixeddelay *d){
    t_freebytes(d->buf, d->size*sizeof(float));
    t_freebytes(d, sizeof(t_diffuser));
}

// METHODS!!!

static inline void gverb_spread(t_gverb *x, t_floatarg f){
    float spread1 = (f < 0 ? 0 : f > 1 ? 1 : f) * 100;
    float spread2 = 3.0*spread1;
    int a, b = 210, c, cc, d, dd, e;
// Diffuser section
    float diffscale = (float)x->x_fdnlens[3]/(210+159+562+410);
// Left
    a = spread1*0.125541f;
    c = 159+a+b;
    cc = c-b;
    a = spread2*0.854046f;
    d = 159+562+a+b;
    dd = d-c;
    e = 1341-d;
    x->x_ldifs[0] = diffuser_make((int)(diffscale*b), 0.75);
    x->x_ldifs[1] = diffuser_make((int)(diffscale*cc), 0.75);
    x->x_ldifs[2] = diffuser_make((int)(diffscale*dd), 0.625);
    x->x_ldifs[3] = diffuser_make((int)(diffscale*e), 0.625);
    if(!x->x_ldifs[0] || !x->x_ldifs[1] || !x->x_ldifs[2] || !x->x_ldifs[3]){
        error("[giga.rev~]: out of memory");
        return;
    }
// Right
    a = spread1*-0.568366f;
    c = 159+a+b;
    cc = c-b;
    a = spread2*-0.126815f;
    d = 159+562+a+b;
    dd = d-c;
    e = 1341-d;
    x->x_rdifs[0] = diffuser_make((int)(diffscale*b), 0.75);
    x->x_rdifs[1] = diffuser_make((int)(diffscale*cc), 0.75);
    x->x_rdifs[2] = diffuser_make((int)(diffscale*dd), 0.625);
    x->x_rdifs[3] = diffuser_make((int)(diffscale*e), 0.625);
    if(!x->x_rdifs[0] || !x->x_rdifs[1] || !x->x_rdifs[2] || !x->x_rdifs[3]){
        error("[giga.rev~]: out of memory");
        return;
    }
}

static inline void gverb_size(t_gverb *x, t_floatarg f){
    int i;
    if(f < 0.1f) x->x_size = 0.1f;
    else x->x_size = f < 0.1f ? 0.1f : f > x->x_maxsize ? x->x_maxsize : f;
    x->x_largestdelay = x->x_sr * x->x_size/340;
    x->x_fdnlens[0] = ff_round(1.000000f*x->x_largestdelay);
    x->x_fdnlens[1] = ff_round(0.816490f*x->x_largestdelay);
    x->x_fdnlens[2] = ff_round(0.707100f*x->x_largestdelay);
    x->x_fdnlens[3] = ff_round(0.632450f*x->x_largestdelay);
    for(i = 0; i < 4; i++)
        x->x_fdngains[i] = -powf((float)x->x_alpha, x->x_fdnlens[i]);
    x->x_taps[0] = 5+ff_round(0.410f*x->x_largestdelay);
    x->x_taps[1] = 5+ff_round(0.300f*x->x_largestdelay);
    x->x_taps[2] = 5+ff_round(0.155f*x->x_largestdelay);
    x->x_taps[3] = 5+ff_round(0.000f*x->x_largestdelay);
    for(i = 0; i < 4; i++)
        x->x_tapgains[i] = powf((float)x->x_alpha, x->x_taps[i]);
}

static inline void gverb_decay(t_gverb *x, t_floatarg f){
    x->x_decay = f < 0.001f ? 0.001f : f > 3600.0f ? 3600.0f : f;
    x->x_alpha = (double)powf(0.001f, 1.0f/(x->x_sr*x->x_decay));
    for(int i = 0; i < 4; i++){
        x->x_fdngains[i] = -powf((float)x->x_alpha, x->x_fdnlens[i]);
        x->x_tapgains[i] = powf((float)x->x_alpha, x->x_taps[i]);
    }
}

static inline void gverb_damp(t_gverb *x, t_floatarg f){
    x->x_fdndamp = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    for(int i = 0; i < 4; i++)
        damper_set(x->x_fdndamps[i], x->x_fdndamp);
}

static inline void gverb_bw(t_gverb *x, t_floatarg f){
    x->x_in_bw = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
    damper_set(x->x_in_damper, 1.0f - x->x_in_bw);
}

static inline void gverb_dry(t_gverb *x, t_floatarg f){
    x->x_dry = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

static inline void gverb_wet(t_gverb *x, t_floatarg f){
    x->x_wet = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

static inline void gverb_early(t_gverb *x, t_floatarg f){
    x->x_early = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

static inline void gverb_late(t_gverb *x, t_floatarg f){
    x->x_late = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

void gverb_clear(t_gverb *x){
    damper_clear(x->x_in_damper);
    for(int i = 0; i < 4; i++){
        fixeddelay_clear(x->x_fdndels[i]);
        damper_clear(x->x_fdndamps[i]);
        diffuser_clear(x->x_ldifs[i]);
        diffuser_clear(x->x_rdifs[i]);
    }
    memset(x->x_d, 0, 4 * sizeof(float));
    memset(x->x_u, 0, 4 * sizeof(float));
    memset(x->x_f, 0, 4 * sizeof(float));
    fixeddelay_clear(x->x_tapdelay);
}

t_int *gverb_perform(t_int *w){
    t_gverb *x = (t_gverb *)(w[1]);
    t_float *input = (t_float *)(w[2]);
    t_float *out1 = (t_float *)(w[3]);
    t_float *out2 = (t_float *)(w[4]);
    int n = (int)(w[5]);
    t_float in;
    t_float outL, outR;
    while(n--){
        in = *input++;
        gverb_do(x, in, &outL, &outR);
        *out1++ = (in * x->x_dry) + (outL * x->x_wet);
        *out2++ = (in * x->x_dry) + (outR * x->x_wet);
    }
    return(w+6);
}

void gverb_dsp(t_gverb *x, t_signal **sp){
    if(x->x_sr != sp[0]->s_sr) x->x_sr = sp[0]->s_sr;
    dsp_add(gverb_perform, 5, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[0]->s_n);
}

void gverb_print(t_gverb *x){
    post("------------------------------------------------------");
    post("[giga.rev~] parameters:");
    post("    - maximum room size: %0.0f meters", x->x_maxsize);
    post("    - room size: %0.0f meters", x->x_size);
    post("    - decay time: %0.02f seconds", x->x_decay);
    post("    - high frequency damping: %0.02f", x->x_fdndamp);
    post("    - input bw (bandwidth): %02.02f", x->x_in_bw);
    post("    - dry level: %02.02f", x->x_dry);
    post("    - early reflections level: %02.02f", x->x_early);
    post("    - late reflections level: %02.02f", x->x_late);
    post("    - wet level: %02.02f", x->x_wet);
    post("------------------------------------------------------");
}

void gverb_free(t_gverb *x){
    damper_free(x->x_in_damper);
    for(int i = 0; i < 4; i++){
        fixeddelay_free(x->x_fdndels[i]);
        damper_free(x->x_fdndamps[i]);
        diffuser_free(x->x_ldifs[i]);
        diffuser_free(x->x_rdifs[i]);
    }
    t_freebytes(x->x_fdndels, 4*sizeof(t_fixeddelay *));
    t_freebytes(x->x_fdngains, 4*sizeof(float));
    t_freebytes(x->x_fdnlens, 4*sizeof(int));
    t_freebytes(x->x_fdndamps, 4*sizeof(t_damper *));
    t_freebytes(x->x_d, 4*sizeof(float));
    t_freebytes(x->x_u, 4*sizeof(float));
    t_freebytes(x->x_f, 4*sizeof(float));
    t_freebytes(x->x_ldifs, 4*sizeof(t_diffuser *));
    t_freebytes(x->x_rdifs, 4*sizeof(t_diffuser *));
    t_freebytes(x->x_taps, 4*sizeof(int));
    t_freebytes(x->x_tapgains, 4*sizeof(float));
    fixeddelay_free(x->x_tapdelay);
}

t_gverb *gverb_new(t_symbol *s, short ac, t_atom *av){
    t_gverb *x = (t_gverb *)pd_new(gverb_class);
    t_symbol *dummy = s;
    dummy = NULL;
    float maxsize = 300.0f;
    float size = 50.0f;
    float decay = 7.0f;
    float damp = 0.5f;
    float spread = 0.5f;
    float in_bw = 0.5f;
    float dry = 0.5f;
    float early = 0.25f;
    float late = 0.25f;
    float wet = 1.0f;
/////////////////////////////////////////////////////////////////////////////////////
    while(ac > 0){
        if(av->a_type == A_SYMBOL && ac >= 2){
            t_symbol *symarg = atom_getsymbolarg(0, ac, av);
            t_float f = av[1].a_w.w_float;
            if(!strcmp(symarg->s_name, "-maxsize")){
                maxsize = f < 0.1f ? 0.1f : f > 10000.0f ? 10000.f : f;
                ac -= 2;
                av += 2;
            }
            else if(!strcmp(symarg->s_name, "-spread")){
                spread = (f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f) * 100;
                ac -= 2;
                av += 2;
            }
            else if(!strcmp(symarg->s_name, "-size")){
                size = f;
                if(size < 0.1f)
                    size = 0.1f;
                if(size > maxsize)
                    maxsize = size;
                ac -= 2;
                av += 2;
            }
            else if(!strcmp(symarg->s_name, "-bw")){
                in_bw = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
                ac -= 2;
                av += 2;
            }
            else if(!strcmp(symarg->s_name, "-decay")){
                decay = f < 0.001f ? 0.001f : f > 36000.0f ? 36000.0f : f;
                ac -= 2;
                av += 2;
            }
            else if(!strcmp(symarg->s_name, "-damp")){
                damp = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
                ac -= 2;
                av += 2;
            }
            else if(!strcmp(symarg->s_name, "-dry")){
                dry = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
                ac -= 2;
                av += 2;
            }
            else if(!strcmp(symarg->s_name, "-early")){
                early = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
                ac -= 2;
                av += 2;
            }
            else if(!strcmp(symarg->s_name, "-late")){
                late = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
                ac -= 2;
                av += 2;
            }
            else if(!strcmp(symarg->s_name, "-wet")){
                wet = f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
                ac -= 2;
                av += 2;
            }
            else
                goto errstate;
        }
        else
            goto errstate;
    };
/////////////////////////////////////////////////////////////////////////////////////
    float ga, gb, gt;
    int i, n;
    float r;
    float diffscale;
    int a, b, c, cc, d, dd, e;
    float spread1, spread2;
    x->x_sr = sys_getsr();
    x->x_fdndamp = damp;
    x->x_maxsize = maxsize;
    x->x_size = size < 0.1f ? 0.1f : size > maxsize ? maxsize : size;
    x->x_decay = decay;
    x->x_dry = dry;
    x->x_wet = wet;
    x->x_early = early;
    x->x_late = late;
    x->x_maxdelay = x->x_sr*x->x_maxsize/340.0;
    x->x_largestdelay = x->x_sr*x->x_size/340.0;
    outlet_new(&x->x_obj, gensym("signal"));
    outlet_new(&x->x_obj, gensym("signal"));
// Input damper
    x->x_in_bw = in_bw;
    x->x_in_damper = damper_make(1.0 - x->x_in_bw);
// FDN section
    x->x_fdndels = (t_fixeddelay **)t_getbytes(4*sizeof(t_fixeddelay *));
    if(!x->x_fdndels){
        error("[giga.rev~]: out of memory");
        return (NULL);
    }
    for(i = 0; i < 4; i++){
        x->x_fdndels[i] = fixeddelay_make((int)x->x_maxdelay+1000);
        if(!x->x_fdndels[i]){
            error("[giga.rev~]: out of memory");
            return (NULL);
        }
    }
    x->x_fdngains = (float *)t_getbytes(4*sizeof(float));
    x->x_fdnlens = (int *)t_getbytes(4*sizeof(int));
    if(!x->x_fdngains || !x->x_fdnlens){
        error("[giga.rev~]: out of memory");
        return (NULL);
    }
    x->x_fdndamps = (t_damper **)t_getbytes(4*sizeof(t_damper *));
    if(!x->x_fdndamps){
        error("[giga.rev~]: out of memory");
        return (NULL);
    }
    for(i = 0; i < 4; i++){
        x->x_fdndamps[i] = damper_make(x->x_fdndamp);
        if(!x->x_fdndamps[i]){
            error("[giga.rev~]: out of memory");
            return (NULL);
        }
    }
    ga = 60.0;
    gt = x->x_decay;
    ga = pow(10.0,-ga/20.0);
    n = x->x_sr*gt;
    x->x_alpha = pow((double)ga,(double)1.0/(double)n);
    gb = 0.0;
    for(i = 0; i < 4; i++){
        if(i == 0) gb = 1.000000*x->x_largestdelay;
        if(i == 1) gb = 0.816490*x->x_largestdelay;
        if(i == 2) gb = 0.707100*x->x_largestdelay;
        if(i == 3) gb = 0.632450*x->x_largestdelay;
        x->x_fdnlens[i] = (int)gb;
        x->x_fdngains[i] = -powf((float)x->x_alpha, x->x_fdnlens[i]);
    }
    x->x_d = (float *)t_getbytes(4*sizeof(float));
    x->x_u = (float *)t_getbytes(4*sizeof(float));
    x->x_f = (float *)t_getbytes(4*sizeof(float));
    if(!x->x_d || !x->x_u || !x->x_f){
        error("[giga.rev~]: out of memory");
        return (NULL);
    }
// Diffuser section
    diffscale = (float)x->x_fdnlens[3]/(210+159+562+410);
    spread1 = spread;
    spread2 = 3.0*spread;
    b = 210;
    r = 0.125541f;
    a = spread1*r;
    c = 210+159+a;
    cc = c-b;
    r = 0.854046f;
    a = spread2*r;
    d = 210+159+562+a;
    dd = d-c;
    e = 1341-d;
    x->x_ldifs = (t_diffuser **)t_getbytes(4*sizeof(t_diffuser *));
    if(!x->x_ldifs){
        error("[giga.rev~]: out of memory");
        return (NULL);
    }
    x->x_ldifs[0] = diffuser_make((int)(diffscale*b), 0.75);
    x->x_ldifs[1] = diffuser_make((int)(diffscale*cc), 0.75);
    x->x_ldifs[2] = diffuser_make((int)(diffscale*dd), 0.625);
    x->x_ldifs[3] = diffuser_make((int)(diffscale*e), 0.625);
    if(!x->x_ldifs[0] || !x->x_ldifs[1] || !x->x_ldifs[2] || !x->x_ldifs[3]){
        error("[giga.rev~]: out of memory");
        return (NULL);
    }
    b = 210;
    r = -0.568366f;
    a = spread1*r;
    c = 210+159+a;
    cc = c-b;
    r = -0.126815f;
    a = spread2*r;
    d = 210+159+562+a;
    dd = d-c;
    e = 1341-d;
    x->x_rdifs = (t_diffuser **)t_getbytes(4*sizeof(t_diffuser *));
    if(!x->x_rdifs){
        error("[giga.rev~]: out of memory");
        return (NULL);
    }
    x->x_rdifs[0] = diffuser_make((int)(diffscale*b), 0.75);
    x->x_rdifs[1] = diffuser_make((int)(diffscale*cc), 0.75);
    x->x_rdifs[2] = diffuser_make((int)(diffscale*dd), 0.625);
    x->x_rdifs[3] = diffuser_make((int)(diffscale*e), 0.625);
    if(!x->x_rdifs[0] || !x->x_rdifs[1] || !x->x_rdifs[2] || !x->x_rdifs[3]){
        error("[giga.rev~]: out of memory");
        return (NULL);
    }
// Tapped delay section
    x->x_tapdelay = fixeddelay_make(44000);
    x->x_taps = (int *)t_getbytes(4*sizeof(int));
    x->x_tapgains = (float *)t_getbytes(4*sizeof(float));
    if(!x->x_tapdelay || !x->x_taps || !x->x_tapgains){
        error("[giga.rev~]: out of memory");
        return (NULL);
    }
    x->x_taps[0] = 5+0.410*x->x_largestdelay;
    x->x_taps[1] = 5+0.300*x->x_largestdelay;
    x->x_taps[2] = 5+0.155*x->x_largestdelay;
    x->x_taps[3] = 5+0.000*x->x_largestdelay;
    for(i = 0; i < 4; i++)
        x->x_tapgains[i] = pow(x->x_alpha, (double)x->x_taps[i]);
    return(x);
    errstate:
        pd_error(x, "[giga.rev~]: improper args");
        return NULL;
}

void setup_giga0x2erev_tilde(void){
    gverb_class = class_new(gensym("giga.rev~"), (t_newmethod)gverb_new,
        (t_method)gverb_free, sizeof(t_gverb), 0, A_GIMME, 0);
    class_addmethod(gverb_class, nullfn, gensym("signal"), 0);
    class_addmethod(gverb_class, (t_method)gverb_dsp, gensym("dsp"), A_CANT, 0);
    class_addmethod(gverb_class, (t_method)gverb_spread, gensym("spread"), A_FLOAT, 0);
    class_addmethod(gverb_class, (t_method)gverb_size, gensym("size"), A_FLOAT, 0);
    class_addmethod(gverb_class, (t_method)gverb_decay, gensym("decay"), A_FLOAT, 0);
    class_addmethod(gverb_class, (t_method)gverb_damp, gensym("damp"), A_FLOAT, 0);
    class_addmethod(gverb_class, (t_method)gverb_bw, gensym("bw"), A_FLOAT, 0);
    class_addmethod(gverb_class, (t_method)gverb_dry, gensym("dry"), A_FLOAT, 0);
    class_addmethod(gverb_class, (t_method)gverb_wet, gensym("wet"), A_FLOAT, 0);
    class_addmethod(gverb_class, (t_method)gverb_early, gensym("early"), A_FLOAT, 0);
    class_addmethod(gverb_class, (t_method)gverb_late, gensym("late"), A_FLOAT, 0);
    class_addmethod(gverb_class, (t_method)gverb_clear, gensym("clear"), 0);
    class_addmethod(gverb_class, (t_method)gverb_print, gensym("print"), 0);
}
