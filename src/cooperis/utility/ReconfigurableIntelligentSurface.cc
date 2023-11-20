//
// Copyright (C) 2022 Michele Segata <segata@ccs-labs.org>
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//

#include <gsl/gsl_blas.h>
#include <gsl/gsl_complex_math.h>
#include <fstream>
#include "ReconfigurableIntelligentSurface.h"
#include "Utils.h"

#include <cmath>

#define IS_ZERO(x) (std::abs(x) < 1e-10)

ReconfigurableIntelligentSurface::ReconfigurableIntelligentSurface(double frequency, int n, int cellsPerLambda, int lambdaSize, double efficiency, double d_theta, double d_phi)
{
    p_tot = 0;
    this->efficiency = efficiency;
    this->d_theta = d_theta;
    this->d_phi = d_phi;
    min_phi = -M_PI + d_phi;
    max_phi = M_PI;
    phi_range = max_phi - min_phi;
    min_theta = 0;
    max_theta = M_PI / 2;
    theta_range = max_theta - min_theta;
    N_s = n;
    rho_lambda = cellsPerLambda;
    N_lambda = lambdaSize;
//    printf("ReconfigurableIntelligentSurface: freq=%f Hz, n=%d, cellsPerLambda=%d, lambdaSize=%d, totalCells=%f\n", frequency, n, cellsPerLambda, lambdaSize, pow(cellsPerLambda*lambdaSize, 2));
    // vector including the available phases depending on the number of states n
    E = linspace(0, N_s - 1, N_s);
    gsl_vector_scale(E, M_PI_X_2 / N_s);
    // speed of light
    c = 299792458;
    lambda = c / frequency;
    k = M_PI_X_2 / lambda;
    // distance between reflecting elements
    du = lambda / cellsPerLambda;
    // size of the surface in meters
    DN = lambdaSize * lambda;
    DM = DN;
    // number of elements on the rows (M) and columns (N)
    M = round(DM / du);
    N = round(DN / du);
    du_k = du * k;
    // number of discrete values for phi and theta
    Ps = round(M_PI_X_2 / d_phi);
    Ts = round(M_PI_2 / d_theta) + 1;
    phi = linspace(-M_PI + d_theta, M_PI, Ps);
    theta = linspace(0, M_PI_2, Ts);
    spherical_elements = new_vector(Ts);
    for (int i = 0; i < Ts; i++)
        gsl_vector_set(spherical_elements, i, spherical_element(gsl_vector_get(theta, i), d_theta, d_phi));
    cos_phi = matrix_from_vector(phi);
    apply_in_place(cos_phi, cos);
    sin_phi = matrix_from_vector(phi);
    apply_in_place(sin_phi, sin);
    sin_theta = matrix_from_vector(theta);
    apply_in_place(sin_theta, sin);
    k_du_sin_cos = new_matrix(Ts, Ps);
    k_du_sin_sin = new_matrix(Ts, Ps);
    // k * du * sin(theta) * cos(phi)^T
    gsl_blas_dgemm(CblasNoTrans, CblasTrans, 1, sin_theta, cos_phi, 0, k_du_sin_cos);
    gsl_matrix_scale(k_du_sin_cos, du_k);

    // k * du * sin(theta) * sin(phi)^T
    gsl_blas_dgemm(CblasNoTrans, CblasTrans, 1, sin_theta, sin_phi, 0, k_du_sin_sin);
    gsl_matrix_scale(k_du_sin_sin, du_k);

    GSL_REAL(C_0_M1j) = 0;
    GSL_IMAG(C_0_M1j) = -1;

    coding = new_matrix(M, N);

    P = new_matrix(Ts, Ps);

    // startup configuration
    configureMetaSurface(0, 0, 0, 0);

}

ReconfigurableIntelligentSurface::~ReconfigurableIntelligentSurface()
{
    gsl_matrix_free(P);
    gsl_matrix_free(coding);
    gsl_matrix_free(k_du_sin_sin);
    gsl_matrix_free(k_du_sin_cos);
    gsl_matrix_free(sin_theta);
    gsl_matrix_free(sin_phi);
    gsl_matrix_free(cos_phi);
    gsl_vector_free(spherical_elements);
    gsl_vector_free(theta);
    gsl_vector_free(phi);
    gsl_vector_free(E);
}

void ReconfigurableIntelligentSurface::configureMetaSurface(double phiR_rad, double thetaR_rad, double phiI_rad, double thetaI_rad)
{

    if (phiR_rad < KEEP_SAME_ANGLE)
        configPhiR = phiR_rad;
    else
        phiR_rad = configPhiR;
    if (phiI_rad < KEEP_SAME_ANGLE)
        configPhiI = phiI_rad;
    else
        phiI_rad = configPhiI;
    if (thetaR_rad < KEEP_SAME_ANGLE)
        configThetaR = thetaR_rad;
    else
        thetaR_rad = configThetaR;
    if (thetaI_rad < KEEP_SAME_ANGLE)
        configThetaI = thetaI_rad;
    else
        thetaI_rad = configThetaI;

    // compute phase per-element phase shift for rows and columns
    // compensate phase shift for incoming signal (-cos/sin)
    // compensate phase shift for reflection (+cos/sin)
    double dsx = du_k * (cos(phiR_rad) * sin(thetaR_rad) - cos(phiI_rad) * sin(thetaI_rad));
    double dsy = du_k * (sin(phiR_rad) * sin(thetaR_rad) - sin(phiI_rad) * sin(thetaI_rad));

    // PHI and coding are pointers, so we work on coding actually
    Matrix PHI = coding;
    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {

            gsl_matrix_set(PHI, m, n, n * dsx + m * dsy);
            // compute PHI(m, n) mod 2*PI to get a value between 0 and 2 PI
            gsl_matrix_set(PHI, m, n, nmod(gsl_matrix_get(PHI, m, n), M_PI_X_2));

            // find the nearest possible phase within the set of available ones (depending on the number of states)
            size_t p = nearest_angle_pos(E, gsl_matrix_get(PHI, m, n));

            // set the actual phase to the best available value
            gsl_matrix_set(PHI, m, n, gsl_vector_get(E, p));

        }
    }

    recomputeGainMap = true;

}

double ReconfigurableIntelligentSurface::cachedGain(double phiR_rad, double thetaR_rad) const
{

    // assume that no signal gets radiated behind the surface
    if (!(thetaR_rad >= 0 && thetaR_rad <= M_PI_2))
        return 0;

    phiR_rad = fix_azimuth(phiR_rad);

    size_t phi_idx = angle_to_index(phiR_rad, phi_range, min_phi, Ps);
    size_t theta_idx = angle_to_index(thetaR_rad, theta_range, min_theta, Ts);
    return gsl_matrix_get(P, theta_idx, phi_idx) * M_PI_X_2 / p_tot;
}

bool ReconfigurableIntelligentSurface::canUseCache(double phiI_rad, double thetaI_rad) const
{
    if (recomputeGainMap)
        return false;
    if (RAD_TO_DEG_ROUND(phiI_rad) != cached_phiI_deg || RAD_TO_DEG_ROUND(thetaI_rad) != cached_thetaI_deg)
        return false;
    return true;
}

size_t ReconfigurableIntelligentSurface::angle_to_index(double angle_rad, double angle_range_rad, double min_angle_rad, size_t n_angles)
{
    return lround((angle_rad - min_angle_rad) / angle_range_rad * (n_angles - 1));
}

double ReconfigurableIntelligentSurface::gain(double phiRX_rad, double thetaRX_rad, double phiTX_rad, double thetaTX_rad)
{

    // fix the corner case in which phi is between -180 and -180 + d_phi/2
    // the gain matrix (if d_phi = 1 degree) would have values from -179 to 180
    // if phi = -179.8, for example, this will fall into the 180 degrees bin, as it is equivalent to 180.2 degrees
    // the 180 degrees bin indeed goes from 179.5 to 180.5 degrees (for d_phi = 1 degree)
    phiRX_rad = fix_azimuth(phiRX_rad);
    phiTX_rad = fix_azimuth(phiTX_rad);

    if (canUseCache(phiTX_rad, thetaTX_rad)) {
        return cachedGain(phiRX_rad, thetaRX_rad);
    }

    CMatrix phase = new_cmatrix(Ts, Ps);

    Matrix PHI = coding;
    double alpha;
    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            // compute the phase offset due to the incidence of the signal
            alpha = du_k * (n * sin(thetaTX_rad) * cos(phiTX_rad) + m * sin(thetaTX_rad) * sin(phiTX_rad));
            // compute the phase offsets for all the possible phiRX,thetaRX pairs
            Matrix n_k_du_sin_cos = new_matrix(k_du_sin_cos);
            Matrix m_k_du_sin_sin = new_matrix(k_du_sin_sin);
            // scale by negative m and n, as these matrices need to be subtracted
            gsl_matrix_scale(n_k_du_sin_cos, -n);
            gsl_matrix_scale(m_k_du_sin_sin, -m);
            gsl_matrix_add(n_k_du_sin_cos, m_k_du_sin_sin);
            // add phase offset due to the position of the transmitter
            gsl_matrix_add_constant(n_k_du_sin_cos, alpha);
            // add phase offset due to coding
            gsl_matrix_add_constant(n_k_du_sin_cos, gsl_matrix_get(PHI, m, n));
            CMatrix complex_matrix = new_cmatrix(n_k_du_sin_cos);
            gsl_matrix_complex_scale(complex_matrix, C_0_M1j);
            exp(complex_matrix);
            // compute final phases e^-j(...)
            gsl_matrix_complex_add(phase, complex_matrix);

            gsl_matrix_complex_free(complex_matrix);
            gsl_matrix_free(n_k_du_sin_cos);
            gsl_matrix_free(m_k_du_sin_sin);
        }
    }

    // compute absolute value of all phases, i.e., length of all vectors
    // if length = 0 then all signals were interfering destructively
    // the longer the vector, the more constructively all the signals are summing up together
    Matrix Fa = abs(phase);
    gsl_matrix_complex_free(phase);

    // P = Fa ^ 2
    // compute the power by squaring the absolute values
    matrix_copy(P, Fa);
    gsl_matrix_mul_elements(P, P);

    // (P^T * 1)
    Vector powers_by_theta = matrix_sum(P, true);
    // (P^T * 1) * A(THETA, d_theta, d_phi)
    p_tot = dot_product(powers_by_theta, spherical_elements);

    gsl_vector_free(powers_by_theta);
    gsl_matrix_free(Fa);

    recomputeGainMap = false;
    cached_phiI_deg = RAD_TO_DEG_ROUND(phiTX_rad);
    cached_thetaI_deg = RAD_TO_DEG_ROUND(thetaTX_rad);

    return cachedGain(phiRX_rad, thetaRX_rad);
}

inline double ReconfigurableIntelligentSurface::fix_azimuth(double phi) const
{
    // fix the corner case in which phi is between -180 and -180 + d_phi/2
    // the gain matrix (if d_phi = 1 degree) would have values from -179 to 180
    // if phi = -179.8, for example, this will fall into the 180 degrees bin, as it is equivalent to 180.2 degrees
    // the 180 degrees bin indeed goes from 179.5 to 180.5 degrees (for d_phi = 1 degree)
    if (-M_PI <= phi && phi < -M_PI + d_phi/2)
        return M_PI;
    else
        return phi;
}

void ReconfigurableIntelligentSurface::getConfiguration(double& phiR_rad, double& thetaR_rad, double& phiI_rad, double& thetaI_rad) const
{
    phiR_rad = configPhiR;
    phiI_rad = configPhiI;
    thetaR_rad = configThetaR;
    thetaI_rad = configThetaI;
}

double ReconfigurableIntelligentSurface::nmod(double x, double y) {
    if (x < 0) {
        double m1 = std::fmod(x, y);
        // force 0 to avoid cases like (-1e-15 + y) mod y != 0
        if (IS_ZERO(m1))
            m1 = 0;
        m1 += y;
        double m2 = std::fmod(m1, y);
        return m2;
    }
    else
        return std::fmod(x, y);
}

double ReconfigurableIntelligentSurface::spherical_element(double theta, double d_theta, double d_phi)
{
    if (ANGLE_EQUALS(theta, 0)) {
        return d_phi * (1 - cos(d_theta/2));
    }
    else if (ANGLE_EQUALS(theta, M_PI_2)) {
        return d_phi * cos(theta - d_theta/2);
    }
    else {
        return d_phi * (cos(theta - d_theta/2) - cos(theta + d_theta/2));
    }
}

double ReconfigurableIntelligentSurface::dot_product(Vector a, Vector b)
{
    if (a->size != b->size)
        return 0;
    double sum = 0;
    for (size_t i = 0; i < a->size; i++)
        sum += gsl_vector_get(a, i) * gsl_vector_get(b, i);
    return sum;
}

Vector ReconfigurableIntelligentSurface::linspace(double min, double max, int n)
{
    Vector v = new_vector(n);
    for (int i = 0; i < n; i++)
        gsl_vector_set(v, i, min + ((double)i / (n - 1)) * (max - min));
    return v;
}

Vector ReconfigurableIntelligentSurface::new_vector(size_t n, double value)
{
    Vector v = gsl_vector_alloc(n);
    gsl_vector_set_all(v, value);
    return v;
}

Vector ReconfigurableIntelligentSurface::new_vector(Vector src)
{
    Vector v = gsl_vector_alloc(src->size);
    for (int i = 0; i < src->size; i++)
        gsl_vector_set(v, i, gsl_vector_get(src, i));
    return v;
}

Matrix ReconfigurableIntelligentSurface::matrix_from_vector(Vector v)
{
    Matrix m = new_matrix(v->size, 1);
    for (int i = 0; i < v->size; i++)
        gsl_matrix_set(m, i, 0, gsl_vector_get(v, i));
    return m;
}

Matrix ReconfigurableIntelligentSurface::new_matrix(size_t rows, size_t columns, double value)
{
    Matrix m = gsl_matrix_alloc(rows, columns);
    gsl_matrix_set_all(m, value);
    return m;
}

Matrix ReconfigurableIntelligentSurface::new_matrix(Matrix src)
{
    Matrix m = gsl_matrix_alloc(src->size1, src->size2);
    for (int r = 0; r < src->size1; r++)
        for (int c = 0; c < src->size2; c++)
            gsl_matrix_set(m, r, c, gsl_matrix_get(src, r, c));


    return m;
}

CMatrix ReconfigurableIntelligentSurface::new_cmatrix(size_t rows, size_t columns, gsl_complex value)
{
    CMatrix m = gsl_matrix_complex_alloc(rows, columns);
    gsl_matrix_complex_set_all(m, value);
    return m;
}

CMatrix ReconfigurableIntelligentSurface::new_cmatrix(Matrix src)
{
    CMatrix m = gsl_matrix_complex_alloc(src->size1, src->size2);
    matrix_to_cmatrix(m, src);
    return m;
}

void ReconfigurableIntelligentSurface::matrix_to_cmatrix(CMatrix dst, const Matrix src)
{
    for (int r = 0; r < src->size1; r++)
        for (int c = 0; c < src->size2; c++) {
            gsl_complex v;
            GSL_REAL(v) = gsl_matrix_get(src, r, c);
            GSL_IMAG(v) = 0;
            gsl_matrix_complex_set(dst, r, c, v);
        }
}

void ReconfigurableIntelligentSurface::matrix_copy(Matrix dst, const Matrix src)
{
    for (int r = 0; r < src->size1; r++)
        for (int c = 0; c < src->size2; c++) {
            gsl_matrix_set(dst, r, c, gsl_matrix_get(src, r, c));
        }
}

void ReconfigurableIntelligentSurface::apply_in_place(Matrix m, double (*f)(double))
{
    for (int r = 0; r < m->size1; r++)
        for (int c = 0; c < m->size2; c++)
            gsl_matrix_set(m, r, c, f(gsl_matrix_get(m, r, c)));


}

void ReconfigurableIntelligentSurface::abs(Vector v)
{
    for (int i = 0; i < v->size; i++)
        gsl_vector_set(v, i, std::abs(gsl_vector_get(v, i)));
}

void ReconfigurableIntelligentSurface::abs(Matrix m)
{
    for (int r = 0; r < m->size1; r++)
        for (int c = 0; c < m->size2; c++)
            gsl_matrix_set(m, r, c, std::abs(gsl_matrix_get(m, r, c)));


}

Matrix ReconfigurableIntelligentSurface::abs(CMatrix m)
{
    Matrix n = gsl_matrix_alloc(m->size1, m->size2);
    for (int r = 0; r < m->size1; r++)
        for (int c = 0; c < m->size2; c++)
            gsl_matrix_set(n, r, c, gsl_complex_abs(gsl_matrix_complex_get(m, r, c)));


    return n;
}

size_t ReconfigurableIntelligentSurface::min_pos(Vector v)
{
    if (v->size == 0)
        return -1;
    size_t pos = gsl_vector_min_index(v);
    return pos;
}

double ReconfigurableIntelligentSurface::angle_distance(double a1, double a2) {
    double d = nmod(a2 - a1 + M_PI, M_PI_X_2) - M_PI;
    if (d < -M_PI)
        return d + M_PI_X_2;
    else
        return d;
}

size_t ReconfigurableIntelligentSurface::nearest_angle_pos(Vector phases, double phase)
{
    Vector tmp = new_vector(phases);
    for (size_t i = 0; i < tmp->size; i++)
        // set tmp[i] = distance(phases[i], phase)
        gsl_vector_set(tmp, i, angle_distance(gsl_vector_get(tmp, i), phase));
    abs(tmp);
    size_t pos = min_pos(tmp);
    gsl_vector_free(tmp);
    return pos;
}

void ReconfigurableIntelligentSurface::exp(CMatrix m)
{
    for (int r = 0; r < m->size1; r++)
        for (int c = 0; c < m->size2; c++)
            gsl_matrix_complex_set(m, r, c, gsl_complex_exp(gsl_matrix_complex_get(m, r, c)));
}

double ReconfigurableIntelligentSurface::matrix_sum(Matrix m)
{
    double sum = 0;
    for (int r = 0; r < m->size1; r++)
        for (int c = 0; c < m->size2; c++)
            sum += gsl_matrix_get(m, r, c);
    return sum;
}

Vector ReconfigurableIntelligentSurface::matrix_sum(Matrix m, bool by_row)
{
    Vector sum;
    if (by_row) {
        sum = new_vector(m->size1);
    }
    else {
        sum = new_vector(m->size2);
    }
    for (int r = 0; r < m->size1; r++) {
        for (int c = 0; c < m->size2; c++) {
            if (by_row) {
                gsl_vector_set(sum, r, gsl_vector_get(sum, r) + gsl_matrix_get(m, r, c));
            }
            else {
                gsl_vector_set(sum, c, gsl_vector_get(sum, c) + gsl_matrix_get(m, r, c));
            }
        }
    }
    return sum;
}

void ReconfigurableIntelligentSurface::print_matrix(Matrix m) {
    for (int r = 0; r < m->size1; r++) {
        for (int c = 0; c < m->size2; c++) {
            printf("%+.4f ", gsl_matrix_get(m, r, c));
        }
        printf("\n");
    }
}

void ReconfigurableIntelligentSurface::print_matrix(CMatrix m) {
    for (int r = 0; r < m->size1; r++) {
        for (int c = 0; c < m->size2; c++) {
            gsl_complex v = gsl_matrix_complex_get(m, r, c);
            printf("%+.4f %+.4fi ", GSL_REAL(v), GSL_IMAG(v));
        }
        printf("\n");
    }
}

void ReconfigurableIntelligentSurface::print_vector(Vector v) {
    for (int c = 0; c < v->size; c++)
        printf("%+.4f ", gsl_vector_get(v, c));
    printf("\n");
}

const Matrix& ReconfigurableIntelligentSurface::getPhases() const {
    return coding;
}

const Matrix& ReconfigurableIntelligentSurface::getGains(double &p_tot, double phiTX_rad, double thetaTX_rad) {
    gain(0, 0, phiTX_rad, thetaTX_rad);
    p_tot = this->p_tot;
    return P;
}

void ReconfigurableIntelligentSurface::writeGains(string prefix, double phiTX_rad, double thetaTX_rad) {
    ofstream f;
    stringstream ss;
    char output_fname[500];
    sprintf(output_fname, "%s_phiR_%f_thetaR_%f_phiI_%f_thetaI_%f_phiTX_%f_thetaTX_%f_n_%d_pl_%d_nl_%d.csv",
            prefix.c_str(), RAD_TO_DEG(configPhiR), RAD_TO_DEG(configThetaR), RAD_TO_DEG(configPhiI), RAD_TO_DEG(configThetaI),
            RAD_TO_DEG(phiTX_rad), RAD_TO_DEG(thetaTX_rad), N_s, rho_lambda, N_lambda);
    f.open(output_fname, ios::out);

    f << "phi,theta,gain\n";
    for (int phi = 180; phi >= -179; phi--) {
        for (int theta = 0; theta <= 90; theta++) {
            double phi_rad = DEG_TO_RAD(phi);
            double theta_rad = DEG_TO_RAD(theta);
            double g = gain(phi_rad, theta_rad, phiTX_rad, thetaTX_rad);
            f << phi << "," << theta << "," << g << "\n";
        }
    }
    f.close();
}