#include "RossbyHaurwitzTestCase.h"

RossbyHaurwitzTestCase::RossbyHaurwitzTestCase() {
    R = 4;
    omega = 3.924e-6;
    gh0 = G*8e3;
    REPORT_ONLINE;
}

RossbyHaurwitzTestCase::~RossbyHaurwitzTestCase() {
    REPORT_OFFLINE;
}

/**
 *  u = aω (cosφ + R sin²φ cosᴿ⁻¹φ cosRλ - cosᴿ⁺¹φ sinφ cosRλ)
 *
 *  v = - aωR cosᴿ⁻¹φ sinφ sinRλ
 *
 *  h = gh0 + a²A(φ) + a²B(φ)cosRλ + a²C(φ)cos2Rλ
 *
 */
void RossbyHaurwitzTestCase::calcInitCond(BarotropicModel &model) {
    if (dynamic_cast<const geomtk::SphereDomain*>(&model.getDomain()) == NULL) {
        REPORT_ERROR("Rossby-Haurwitz test case is only valid in sphere domain!");
    }
    TimeLevelIndex initTimeLevel;
    Field &u = model.getZonalWind();
    Field &v = model.getMeridionalWind();
    Field &h = model.getGeopotentialHeight();
    double a = model.getDomain().getRadius();
    double R2 = R*R;
    double R_1 = R+1;
    double R_2 = R+2;
    double omega2 = omega*omega;
    for (int j = 1; j < model.getMesh().getNumGrid(1, FULL)-1; ++j) {
        double cosLat = model.getMesh().getCosLat(FULL, j);
        double sinLat = model.getMesh().getSinLat(FULL, j);
        double cosLat2 = cosLat*cosLat;
        double cosLatR = pow(cosLat, R);
        double cosLatR2 = cosLatR*cosLatR;
        double A = (omega*OMEGA+0.5*omega2)*cosLat2+0.25*omega2*cosLatR2*(R_1*cosLat2+(2*R2-R-2)-2*R2/cosLat2);
        double B = 2*omega*OMEGA+2*omega2*cosLatR*((R2+2*R+2)-R_1*R_1*cosLat2)/R_1*R_2;
        double C = 0.25*omega2*cosLatR2*(R_1*cosLat2-R_2);
        for (int i = 0; i < model.getMesh().getNumGrid(0, FULL); ++i) {
            double lon = model.getMesh().getGridCoordComp(0, FULL, i);
            double cosRLon = cos(R*lon);
            double sinRLon = sin(R*lon);
            double cos2RLon = cos(2*R*lon);
            double u0, v0, h0;
            u0 = a*omega*(cosLat+R*pow(sinLat, 2)*cosLatR/cosLat*cosRLon-cosLatR*cosLat*sinLat*cosRLon);
            v0 = -a*omega*R*cosLatR/cosLat*sinLat*sinRLon;
            h0 = gh0+a*a*(A+B*cosRLon+C*cos2RLon);
            u(initTimeLevel, i, j) = u0;
            v(initTimeLevel, i, j) = v0;
            h(initTimeLevel, i, j) = h0;
        }
    }
    int js = 0, jn = model.getMesh().getNumGrid(1, FULL)-1;
    for (int i = 0; i < model.getMesh().getNumGrid(0, FULL); ++i) {
        u(initTimeLevel, i, js) = 0.0;
        u(initTimeLevel, i, jn) = 0.0;
        v(initTimeLevel, i, js) = 0.0;
        v(initTimeLevel, i, jn) = 0.0;
        h(initTimeLevel, i, js) = gh0;
        h(initTimeLevel, i, jn) = gh0;
    }
    u.applyBndCond(initTimeLevel);
    v.applyBndCond(initTimeLevel);
    h.applyBndCond(initTimeLevel);
}