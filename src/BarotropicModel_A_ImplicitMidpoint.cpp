#include "BarotropicModel_A_ImplicitMidpoint.h"

BarotropicModel_A_ImplicitMidpoint::BarotropicModel_A_ImplicitMidpoint() {
    REPORT_ONLINE;
}

BarotropicModel_A_ImplicitMidpoint::~BarotropicModel_A_ImplicitMidpoint() {
    REPORT_OFFLINE;
}

void BarotropicModel_A_ImplicitMidpoint::init(int numLon, int numLat) {
    // -------------------------------------------------------------------------
    // initialize domain
    domain = new Domain(2);
    domain->setRadius(6.371e6);
    // -------------------------------------------------------------------------
    // initialize mesh
    mesh = new Mesh(*domain);
    mesh->init(numLon, numLat);
    dlon = mesh->getGridInterval(0, FULL, 0);
    dlat = mesh->getGridInterval(1, FULL, 0); // assume equidistance grids
    // -------------------------------------------------------------------------
    // create variables
    u.create("u", "m s-1", "zonal wind speed", *mesh, CENTER, HAS_HALF_LEVEL);
    v.create("v", "m s-1", "meridional wind speed", *mesh, CENTER, HAS_HALF_LEVEL);
    gh.create("gh", "m2 s-2", "geopotential height", *mesh, CENTER, HAS_HALF_LEVEL);

    ut.create("ut", "(m s-1)*m-2", "transformed zonal wind speed", *mesh, CENTER, HAS_HALF_LEVEL);
    vt.create("vt", "(m s-1)*m-2", "transformed meridional wind speed", *mesh, CENTER, HAS_HALF_LEVEL);
    ght.create("ght", "m-2", "transformed geopotential height", *mesh, CENTER, HAS_HALF_LEVEL);

    dut.create("dut", "m s-2", "zonal wind speed tendency", *mesh, CENTER);
    dvt.create("dvt", "m s-2", "meridional zonal speed tendency", *mesh, CENTER);
    dgh.create("dgh", "m-2 s-1", "geopotential height tendency", *mesh, CENTER);

    ghu.create("ghu", "m2 s-1", "ut * ght", *mesh, CENTER);
    ghv.create("ghv", "m2 s-1", "vt * ght", *mesh, CENTER);

    fu.create("fu", "* m s-1", "* * u", *mesh, CENTER);
    fv.create("fv", "* m s-1", "* * v", *mesh, CENTER);
    // -------------------------------------------------------------------------
    // set coefficients
    // Note: Some coefficients containing cos(lat) will be specialized at Poles.
    int js = 0, jn = mesh->getNumGrid(1, FULL)-1;

    cosLat.set_size(mesh->getNumGrid(1, FULL));
    for (int j = 1; j < mesh->getNumGrid(1, FULL)-1; ++j) {
        cosLat[j] = mesh->getCosLat(FULL, j);
    }
    cosLat[js] = mesh->getCosLat(HALF,   js)*0.25;
    cosLat[jn] = mesh->getCosLat(HALF, jn-1)*0.25;

    tanLat.set_size(mesh->getNumGrid(1, FULL));
    for (int j = 1; j < mesh->getNumGrid(1, FULL)-1; ++j) {
        tanLat[j] = mesh->getTanLat(FULL, j);
    }
    tanLat[js] = -1/cosLat[js];
    tanLat[jn] =  1/cosLat[jn];

    factorCor.set_size(mesh->getNumGrid(1, FULL));
    for (int j = 0; j < mesh->getNumGrid(1, FULL); ++j) {
        factorCor[j] = 2*OMEGA*mesh->getSinLat(FULL, j);
    }

    factorCur.set_size(mesh->getNumGrid(1, FULL));
    for (int j = 0; j < mesh->getNumGrid(1, FULL); ++j) {
        factorCur[j] = tanLat[j]/domain->getRadius();
    }

    factorLon.set_size(mesh->getNumGrid(1, FULL));
    for (int j = 0; j < mesh->getNumGrid(1, FULL); ++j) {
        factorLon[j] = 1/(2*dlon*domain->getRadius()*cosLat[j]);
    }

    factorLat.set_size(mesh->getNumGrid(1, FULL));
    for (int j = 0; j < mesh->getNumGrid(1, FULL); ++j) {
        factorLat[j] = 1/(2*dlat*domain->getRadius()*cosLat[j]);
    }
    // -------------------------------------------------------------------------
    // set variables in Poles
    for (int i = -1; i < mesh->getNumGrid(0, FULL)+1; ++i) {
        dut(i, js) = 0.0; dut(i, jn) = 0.0;
        dvt(i, js) = 0.0; dvt(i, jn) = 0.0;
        ghu(i, js) = 0.0; ghu(i, jn) = 0.0;
        ghv(i, js) = 0.0; ghv(i, jn) = 0.0;
    }
}

void BarotropicModel_A_ImplicitMidpoint::run(TimeManager &timeManager) {
    dt = timeManager.getStepSize();
    // -------------------------------------------------------------------------
    // initialize IO manager
    io.init(timeManager);
    int fileIdx = io.registerOutputFile(*mesh, "output", IOFrequencyUnit::DAYS, 0.5);
    io.file(fileIdx).registerOutputField<double, 2, FULL_DIMENSION>(3, &u, &v, &gh);
    // -------------------------------------------------------------------------
    // copy states
    halfTimeIdx = oldTimeIdx+0.5;
    for (int j = 0; j < mesh->getNumGrid(1, FULL); ++j) {
        for (int i = -1; i < mesh->getNumGrid(0, FULL)+1; ++i) {
            u(halfTimeIdx, i, j) = u(oldTimeIdx, i, j);
            v(halfTimeIdx, i, j) = v(oldTimeIdx, i, j);
            gh(halfTimeIdx, i, j) = gh(oldTimeIdx, i, j);
            ght(oldTimeIdx, i, j) = sqrt(gh(oldTimeIdx, i, j));
            ght(halfTimeIdx, i, j) = ght(oldTimeIdx, i, j);
            ut(oldTimeIdx, i, j) = u(oldTimeIdx, i, j)*ght(oldTimeIdx, i, j);
            ut(halfTimeIdx, i, j) = ut(oldTimeIdx, i, j);
            vt(oldTimeIdx, i, j) = v(oldTimeIdx, i, j)*ght(oldTimeIdx, i, j);
            vt(halfTimeIdx, i, j) = vt(oldTimeIdx, i, j);
        }
    }
    // -------------------------------------------------------------------------
    // output initial condition
    io.create(fileIdx);
    io.output<double, 2>(fileIdx, oldTimeIdx, 3, &u, &v, &gh);
    io.close(fileIdx);
    // -------------------------------------------------------------------------
    // main integration loop
    while (!timeManager.isFinished()) {
        integrate();
        timeManager.advance();
        oldTimeIdx.shift();
        io.create(fileIdx);
        io.output<double, 2>(fileIdx, oldTimeIdx, 3, &u, &v, &gh);
        io.close(fileIdx);
    }
}

void BarotropicModel_A_ImplicitMidpoint::integrate() {
    // -------------------------------------------------------------------------
    // set time level indices
    halfTimeIdx = oldTimeIdx+0.5;
    newTimeIdx = oldTimeIdx+1;
    // -------------------------------------------------------------------------
    // get old total energy and mass
    double e0 = calcTotalEnergy(oldTimeIdx);
    double m0 = calcTotalMass(oldTimeIdx);
#ifdef DEBUG
    cout << "iteration ";
#endif
    cout << "energy: ";
    cout << std::fixed << setw(20) << setprecision(2) << e0 << "  ";
    cout << "mass: ";
    cout << setw(20) << setprecision(2) << m0 << endl;
    // -------------------------------------------------------------------------
    // run iterations
    int iter;
    for (iter = 1; iter <= 8; ++iter) {
        // ---------------------------------------------------------------------
        // update geopotential height
        calcGeopotentialHeightTendency(halfTimeIdx);
        for (int j = 0; j < mesh->getNumGrid(1, FULL); ++j) {
            for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
                gh(newTimeIdx, i, j) = gh(oldTimeIdx, i, j)-dt*dgh(i, j);
            }
        }
        gh.applyBndCond(newTimeIdx, UPDATE_HALF_LEVEL);
        // ---------------------------------------------------------------------
        // transform geopotential height
        for (int j = 0; j < mesh->getNumGrid(1, FULL); ++j) {
            for (int i = -1; i < mesh->getNumGrid(0, FULL)+1; ++i) {
                ght(halfTimeIdx, i, j) = sqrt(gh(halfTimeIdx, i, j));
                ght(newTimeIdx,  i, j) = sqrt(gh(newTimeIdx,  i, j));
            }
        }
        // ---------------------------------------------------------------------
        // update velocity
        calcZonalWindTendency(halfTimeIdx);
        calcMeridionalWindTendency(halfTimeIdx);
        for (int j = 0; j < mesh->getNumGrid(1, FULL); ++j) {
            for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
                ut(newTimeIdx, i, j) = ut(oldTimeIdx, i, j)-dt*dut(i, j);
                vt(newTimeIdx, i, j) = vt(oldTimeIdx, i, j)-dt*dvt(i, j);
            }
        }
        ut.applyBndCond(newTimeIdx, UPDATE_HALF_LEVEL);
        vt.applyBndCond(newTimeIdx, UPDATE_HALF_LEVEL);
        // ---------------------------------------------------------------------
        // transform back velocity
        for (int j = 0; j < mesh->getNumGrid(1, FULL); ++j) {
            for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
                u(newTimeIdx, i, j) = ut(newTimeIdx, i, j)/ght(newTimeIdx, i, j);
                v(newTimeIdx, i, j) = vt(newTimeIdx, i, j)/ght(newTimeIdx, i, j);
            }
        }
        u.applyBndCond(newTimeIdx, UPDATE_HALF_LEVEL);
        v.applyBndCond(newTimeIdx, UPDATE_HALF_LEVEL);
        // get new total energy and mass
        double e1 = calcTotalEnergy(newTimeIdx);
        // TODO: Figure out how this early iteration abortion works.
        if (fabs(e1-e0)*2/(e1+e0) < 5.0e-15) {
            break;
        }
#ifdef DEBUG
        double m1 = calcTotalMass(newTimeIdx);
        cout << setw(9) << iter;
        cout << " energy: ";
        cout << std::fixed << setw(20) << setprecision(2) << e1 << "  ";
        cout << "mass: ";
        cout << setw(20) << setprecision(2) << m1 << " ";
        cout << "energy bias: ";
        cout << setw(20) << setprecision(16) << fabs(e1-e0)*2/(e1+e0) << endl;
#endif
    }
}

double BarotropicModel_A_ImplicitMidpoint::calcTotalEnergy(const TimeLevelIndex &timeIdx) const {
    double totalEnergy = 0.0;
    for (int j = 0; j < mesh->getNumGrid(1, FULL); ++j) {
        for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
            totalEnergy += (pow(ut(timeIdx, i, j), 2)+
                            pow(vt(timeIdx, i, j), 2)+
                            pow(gh(timeIdx, i, j), 2))*cosLat[j];
        }
    }
    return totalEnergy;
}

double BarotropicModel_A_ImplicitMidpoint::calcTotalMass(const TimeLevelIndex &timeIdx) const {
    double totalMass = 0.0;
    for (int j = 0; j < mesh->getNumGrid(1, FULL); ++j) {
        for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
            totalMass += gh(timeIdx, i, j)*cosLat[j];
        }
    }
    return totalMass;
}

/**
 *  Input: ut, vt, ght
 *  Intermediate: ghu, ghv
 *  Output: dgh
 */
void BarotropicModel_A_ImplicitMidpoint::calcGeopotentialHeightTendency(const TimeLevelIndex &timeIdx) {
    // calculate intermediate variables
    // Note: The results are different from computation using u, v, gh.
    for (int j = 1; j < mesh->getNumGrid(1, FULL)-1; ++j) {
        for (int i = -1; i < mesh->getNumGrid(0, FULL)+1; ++i) {
            ghu(i, j) = ut(timeIdx, i, j)*ght(timeIdx, i, j);
            ghv(i, j) = vt(timeIdx, i, j)*ght(timeIdx, i, j)*cosLat[j];
        }
    }
    // normal grids
    for (int j = 1; j < mesh->getNumGrid(1, FULL)-1; ++j) {
        for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
            dgh(i, j) = (ghu(i+1, j)-ghu(i-1, j))*factorLon[j]+
                        (ghv(i, j+1)-ghv(i, j-1))*factorLat[j];
        }
    }
    // pole grids
    // last character 's' and 'n' mean 'Sorth Pole' and 'North Pole' respectively
    int js = 0, jn = mesh->getNumGrid(1, FULL)-1;
    double dhs = 0.0, dhn = 0.0;
    for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
        dhs += ghv(i, js+1);
        dhn -= ghv(i, jn-1);
    }
    dhs *= factorLat[js]/mesh->getNumGrid(0, FULL);
    dhn *= factorLat[jn]/mesh->getNumGrid(0, FULL);
    for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
        dgh(i, js) = dhs;
        dgh(i, jn) = dhn;
    }


    double tmp = 0.0;
    for (int j = 0; j < mesh->getNumGrid(1, FULL); ++j) {
        for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
            tmp += dgh(i, j)*cosLat[j];
        }
    }
    assert(fabs(tmp) < 1.0e-10);
}

void BarotropicModel_A_ImplicitMidpoint::calcZonalWindTendency(const TimeLevelIndex &timeIdx) {
    calcZonalWindAdvection(timeIdx);
    calcZonalWindCoriolis(timeIdx);
    calcZonalWindPressureGradient(timeIdx);
}

void BarotropicModel_A_ImplicitMidpoint::calcMeridionalWindTendency(const TimeLevelIndex &timeIdx) {
    calcMeridionalWindAdvection(timeIdx);
    calcMeridionalWindCoriolis(timeIdx);
    calcMeridionalWindPressureGradient(timeIdx);
}

/**
 *  Input: u, v, ut
 *  Output, s1
 */
void BarotropicModel_A_ImplicitMidpoint::calcZonalWindAdvection(const TimeLevelIndex &timeIdx) {
    for (int j = 1; j < mesh->getNumGrid(1, FULL)-1; ++j) {
        for (int i = -1; i < mesh->getNumGrid(0, FULL)+1; ++i) {
            fu(i, j) = ut(timeIdx, i, j)*u(timeIdx, i, j);
            fv(i, j) = ut(timeIdx, i, j)*v(timeIdx, i, j)*cosLat[j];
        }
    }
    // normal grids
    for (int j = 1; j < mesh->getNumGrid(1, FULL)-1; ++j) {
        for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
            double dx1 = fu(i+1, j)-fu(i-1, j);
            double dy1 = fv(i, j+1)-fv(i, j-1);
            double dx2 = u(timeIdx, i, j)*(ut(timeIdx, i+1, j)-ut(timeIdx, i-1, j));
            double dy2 = v(timeIdx, i, j)*(ut(timeIdx, i, j+1)-ut(timeIdx, i, j-1))*cosLat[j];
            dut(i, j) = 0.5*((dx1+dx2)*factorLon[j]+(dy1+dy2)*factorLat[j]);
        }
    }
}

/**
 *  Input: u, v, vt
 *  Output, dvt
 */
void BarotropicModel_A_ImplicitMidpoint::calcMeridionalWindAdvection(const TimeLevelIndex &timeIdx) {
    for (int j = 1; j < mesh->getNumGrid(1, FULL)-1; ++j) {
        for (int i = -1; i < mesh->getNumGrid(0, FULL)+1; ++i) {
            fu(i, j) = vt(timeIdx, i, j)*u(timeIdx, i, j);
            fv(i, j) = vt(timeIdx, i, j)*v(timeIdx, i, j)*cosLat[j];
        }
    }
    // normal grids
    for (int j = 1; j < mesh->getNumGrid(1, FULL)-1; ++j) {
        for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
            double dx1 = fu(i+1,j)-fu(i-1,j);
            double dy1 = fv(i,j+1)-fv(i,j-1);
            double dx2 = u(timeIdx, i, j)*(vt(timeIdx, i+1, j)-vt(timeIdx, i-1, j));
            double dy2 = v(timeIdx, i, j)*(vt(timeIdx, i, j+1)-vt(timeIdx, i, j-1))*cosLat[j];
            dvt(i, j) = 0.5*((dx1+dx2)*factorLon[j]+(dy1+dy2)*factorLat[j]);
        }
    }
}

/**
 *  Input: u, vt
 *  Output: dut
 */
void BarotropicModel_A_ImplicitMidpoint::calcZonalWindCoriolis(const TimeLevelIndex &timeIdx) {
    for (int j = 1; j < mesh->getNumGrid(1, FULL)-1; ++j) {
        for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
            double f = factorCor[j]+u(timeIdx, i, j)*factorCur[j];
            dut(i, j) -= f*vt(timeIdx, i, j);
        }
    }
}

/**
 *  Input: u, ut
 *  Output: dvt
 */
void BarotropicModel_A_ImplicitMidpoint::calcMeridionalWindCoriolis(const TimeLevelIndex &timeIdx) {
    for (int j = 1; j < mesh->getNumGrid(1, FULL)-1; ++j) {
        for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
            double f = factorCor[j]+u(timeIdx, i, j)*factorCur[j];
            dvt(i, j) += f*ut(timeIdx, i, j);
        }
    }
}

/*
 *  Input: gh, ght
 *  Output: dut
 */
void BarotropicModel_A_ImplicitMidpoint::calcZonalWindPressureGradient(const TimeLevelIndex &timeIdx) {
    for (int j = 1; j < mesh->getNumGrid(1, FULL)-1; ++j) {
        for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
            dut(i, j) += (gh(timeIdx, i+1, j)-gh(timeIdx, i-1, j))*
                         factorLon[j]*ght(timeIdx, i, j);
        }
    }
}

/*
 *  Input: gh, ght
 *  Output: dvt
 */
void BarotropicModel_A_ImplicitMidpoint::calcMeridionalWindPressureGradient(const TimeLevelIndex &timeIdx) {
    for (int j = 1; j < mesh->getNumGrid(1, FULL)-1; ++j) {
        for (int i = 0; i < mesh->getNumGrid(0, FULL); ++i) {
            dvt(i, j) += (gh(timeIdx, i, j+1)-gh(timeIdx, i, j-1))*
                         factorLat[j]*cosLat[j]*ght(timeIdx, i, j);
        }
    }
}
