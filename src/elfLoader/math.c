
#include <math.h>
#include <stdlib.h>
#include "math.h"
#include "symbol.h"

int    hook_finite(double x)   { return isfinite(x) ? 1 : 0; }
int    hook_finitef(float x)   { return isfinite(x) ? 1 : 0; }

void mathBridge_initBridges(void)
{
    symbol_register("atan", atan, NULL);
    symbol_register("atan2", atan2, NULL);
    symbol_register("atanf", atanf, NULL);
    symbol_register("atan2f", atan2f, NULL);
    symbol_register("tan", tan, NULL);
    symbol_register("log", log, NULL);
    symbol_register("logf", logf, NULL);
    symbol_register("log10", log10, NULL);
    symbol_register("log10f", log10f, NULL);
    symbol_register("exp", exp, NULL);
    symbol_register("expf", expf, NULL);
    symbol_register("asin", asin, NULL);
    symbol_register("asinf", asinf, NULL);
    symbol_register("sin", sin, NULL);
    symbol_register("sinh", sinh, NULL);
    symbol_register("sinf", sinf, NULL);
    symbol_register("sinhf", sinhf, NULL);
    symbol_register("acos", acos, NULL);
    symbol_register("acosf", acosf, NULL);
    symbol_register("cos", cos, NULL);
    symbol_register("cosh", cosh, NULL);
    symbol_register("cosf", cosf, NULL);
    symbol_register("coshf", coshf, NULL);
    symbol_register("tanh", tanh, NULL);
    symbol_register("tanf", tanf, NULL);
    symbol_register("tanhf", tanhf, NULL);
    symbol_register("pow", pow, NULL);
    symbol_register("powf", powf, NULL);
    symbol_register("hypot", hypot, NULL);
    symbol_register("hypotf", hypotf, NULL);
    symbol_register("fmod", fmod, NULL);
    symbol_register("fmodf", fmodf, NULL);
    symbol_register("modf", modf, NULL);
    symbol_register("modff", modff, NULL);
    symbol_register("sqrt", sqrt, NULL);
    symbol_register("sqrtf", sqrtf, NULL);
    symbol_register("div", div, NULL);
    symbol_register("finitef", hook_finitef, NULL);
    symbol_register("finite", hook_finite, NULL);
    symbol_register("frexp", frexp, NULL);
    symbol_register("frexpf", frexpf, NULL);
}