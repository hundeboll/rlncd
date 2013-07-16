#pragma once

#define ONE 255.0

DECLARE_double(fixed_overshoot);

bool r_test(uint8_t e1, uint8_t e2, uint8_t e3)
{
    return (ONE - e2) < (e3 - e1*e3/ONE);
}

double r_val(size_t g, uint8_t e1, uint8_t e2, uint8_t e3)
{
    double nom, denom;

    if (r_test(e1, e2, e3)) {
        denom = e3 - e1*e3/ONE ? : ONE;
        return ONE/denom;
    } else {
        nom = ONE*g - g*e2 - g*e3 + g*e1*e3/ONE;
        denom = ONE + e1*e3*e2/ONE/ONE - e2 - e1*e3/ONE ? : ONE;
        return nom/denom;
    }
}

double source_budget(size_t g, uint8_t e1, uint8_t e2, uint8_t e3)
{
    double nom, denom, r = r_val(g, e1, e2, e3);

    nom = g*ONE + r*ONE - r*e2;
    denom = 2*ONE - e3 - e2 ? : ONE;

    return FLAGS_fixed_overshoot*nom/denom;
}

double recoder_budget(size_t g, uint8_t e1, uint8_t e2, uint8_t e3)
{
    double nom, denom, r = r_val(g, e1, e2, e3);

    nom = g*ONE + r*ONE - r*e2;
    denom = 2*ONE - e3 - e2;

    return nom/denom;
}

double recoder_credit(size_t e1, size_t e2, size_t e3)
{
    double denom = ONE - e3*e1/ONE;
    return ONE/denom;
}

double source_credit(size_t e1, size_t e2, size_t e3)
{
    return recoder_credit(e1, e2, e3);
}
