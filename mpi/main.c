#include <assert.h>
#include <complex.h>
#include <fftw3-mpi.h>
#include <math.h>
#include <stdlib.h>

enum { N = 1 << 5, Nf = N / 2 + 1, tot = N * N * N };
#define MALLOC(var, nelem)                                                     \
  if ((var = fftw_malloc(nelem * sizeof *var)) == NULL) {                      \
    fprintf(stderr, "%s:%d: fftwf_malloc failed\n", __FILE__, __LINE__);       \
    exit(1);                                                                   \
  }

int main(int argc, char **argv) {
  double dx;
  double L;
  double s_in;
  double s_out;
  fftw_complex *curlX;
  fftw_complex *curlY;
  fftw_complex *curlZ;
  fftw_complex *dU;
  fftw_complex *dV;
  fftw_complex *dW;
  fftw_complex *P_hat;
  fftw_complex *U_hat;
  fftw_complex *U_hat0;
  fftw_complex *U_hat1;
  fftw_complex *V_hat;
  fftw_complex *V_hat0;
  fftw_complex *V_hat1;
  fftw_complex *W_hat;
  fftw_complex *W_hat0;
  fftw_complex *W_hat1;
  fftw_plan irfftn;
  fftw_plan rfftn;
  int *dealias;
  int i;
  int j;
  int k;
  int m;
  int rank;
  int rk;
  int tstep;
  int z;
  double a[] = {1 / 6.0, 1 / 3.0, 1 / 3.0, 1 / 6.0};
  double b[] = {0.5, 0.5, 1.0};
  double *CU;
  double *CV;
  double *CW;
  double *kk;
  double kmax;
  double kx[N];
  double kz[Nf];
  double nu, dt, T;
  double pi = 3.141592653589793238;
  double t;
  double *U;
  double *U_tmp;
  double *V;
  double *V_tmp;
  double *W;
  double *W_tmp;
  ptrdiff_t n;
  ptrdiff_t n0;
  ptrdiff_t n1;
  ptrdiff_t s0;
  ptrdiff_t s1;

  nu = 0.000625;
  T = 0.1;
  dt = 0.01;
  L = 2 * pi;
  dx = L / N;
  MPI_Init(&argc, &argv);
  fftw_mpi_init();
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  n = fftw_mpi_local_size_3d_transposed(N, N, Nf, MPI_COMM_WORLD, &n0, &s0, &n1,
                                        &s1);
  assert(n == n1 * N * Nf);
  MALLOC(U, 2 * n);
  MALLOC(V, 2 * n);
  MALLOC(W, 2 * n);
  MALLOC(U_tmp, 2 * n);
  MALLOC(V_tmp, 2 * n);
  MALLOC(W_tmp, 2 * n);
  MALLOC(CU, 2 * n);
  MALLOC(CV, 2 * n);
  MALLOC(CW, 2 * n);
  MALLOC(dealias, 2 * n);
  MALLOC(kk, 2 * n);
  MALLOC(U_hat, n);
  MALLOC(V_hat, n);
  MALLOC(W_hat, n);
  MALLOC(P_hat, n);
  MALLOC(U_hat0, n);
  MALLOC(V_hat0, n);
  MALLOC(W_hat0, n);
  MALLOC(U_hat1, n);
  MALLOC(V_hat1, n);
  MALLOC(W_hat1, n);
  MALLOC(dU, n);
  MALLOC(dV, n);
  MALLOC(dW, n);
  MALLOC(curlX, n);
  MALLOC(curlY, n);
  MALLOC(curlZ, n);
  for (i = 0; i < N / 2; i++) {
    kx[i] = i;
    kz[i] = i;
  }
  kz[N / 2] = N / 2;
  for (i = -N / 2; i < 0; i++)
    kx[i + N] = i;

  rfftn = fftw_mpi_plan_dft_r2c_3d(N, N, N, U, U_hat, MPI_COMM_WORLD,
                                   FFTW_MPI_TRANSPOSED_OUT);
  irfftn = fftw_mpi_plan_dft_c2r_3d(N, N, N, U_hat, U, MPI_COMM_WORLD,
                                    FFTW_MPI_TRANSPOSED_IN);

  for (i = 0; i < n0; i++)
    for (j = 0; j < N; j++)
      for (k = 0; k < N; k++) {
        z = (i * N + j) * 2 * Nf + k;
        U[z] = sin(dx * (i + s0)) * cos(dx * j) * cos(dx * k);
        V[z] = -cos(dx * (i + s0)) * sin(dx * j) * cos(dx * k);
        W[z] = 0.0;
      }

  fftw_mpi_execute_dft_r2c(rfftn, U, U_hat);
  fftw_mpi_execute_dft_r2c(rfftn, V, V_hat);
  fftw_mpi_execute_dft_r2c(rfftn, W, W_hat);

  kmax = 2. / 3. * (N / 2 + 1);
  for (i = 0; i < n1; i++)
    for (j = 0; j < N; j++)
      for (k = 0; k < Nf; k++) {
        z = (i * N + j) * Nf + k;
        dealias[z] = (fabs(kx[i + s1]) < kmax) * (fabs(kx[j]) < kmax) *
                     (fabs(kx[k]) < kmax);
      }

  for (i = 0; i < n1; i++)
    for (j = 0; j < N; j++)
      for (k = 0; k < Nf; k++) {
        z = (i * N + j) * Nf + k;
        m = kx[i + s1] * kx[i + s1] + kx[j] * kx[j] + kx[k] * kx[k];
        kk[z] = m > 0 ? m : 1;
      }
  t = 0.0;
  tstep = 0;
  while (t <= T) {
    t += dt;
    tstep++;
    for (i = 0; i < n1; i++)
      for (j = 0; j < N; j++)
        for (k = 0; k < Nf; k++) {
          z = (i * N + j) * Nf + k;
          U_hat0[z] = U_hat[z];
          V_hat0[z] = V_hat[z];
          W_hat0[z] = W_hat[z];
          U_hat1[z] = U_hat[z];
          V_hat1[z] = V_hat[z];
          W_hat1[z] = W_hat[z];
        }
    for (rk = 0; rk < 4; rk++) {
      if (rk > 0) {
        fftw_mpi_execute_dft_c2r(irfftn, U_hat, U);
        fftw_mpi_execute_dft_c2r(irfftn, V_hat, V);
        fftw_mpi_execute_dft_c2r(irfftn, W_hat, W);
        for (k = 0; k < 2 * n; k++) {
          U[k] /= tot;
          V[k] /= tot;
          W[k] /= tot;
        }
      }
      for (i = 0; i < n1; i++)
        for (j = 0; j < N; j++)
          for (k = 0; k < Nf; k++) {
            z = (i * N + j) * Nf + k;
            curlZ[z] = I * (kx[i + s1] * V_hat[z] - kx[j] * U_hat[z]);
            curlY[z] = I * (kz[k] * U_hat[z] - kx[i + s1] * W_hat[z]);
            curlX[z] = I * (kx[j] * W_hat[z] - kz[k] * V_hat[z]);
          }
      fftw_mpi_execute_dft_c2r(irfftn, curlX, CU);
      fftw_mpi_execute_dft_c2r(irfftn, curlY, CV);
      fftw_mpi_execute_dft_c2r(irfftn, curlZ, CW);
      for (k = 0; k < 2 * n; k++) {
        CU[k] /= tot;
        CV[k] /= tot;
        CW[k] /= tot;
      }
      for (i = 0; i < n0; i++)
        for (j = 0; j < N; j++)
          for (k = 0; k < N; k++) {
            z = (i * N + j) * 2 * Nf + k;
            U_tmp[z] = V[z] * CW[z] - W[z] * CV[z];
            V_tmp[z] = W[z] * CU[z] - U[z] * CW[z];
            W_tmp[z] = U[z] * CV[z] - V[z] * CU[z];
          }
      fftw_mpi_execute_dft_r2c(rfftn, U_tmp, dU);
      fftw_mpi_execute_dft_r2c(rfftn, V_tmp, dV);
      fftw_mpi_execute_dft_r2c(rfftn, W_tmp, dW);

      for (i = 0; i < n1; i++)
        for (j = 0; j < N; j++)
          for (k = 0; k < Nf; k++) {
            z = (i * N + j) * Nf + k;
            dU[z] *= dealias[z] * dt;
            dV[z] *= dealias[z] * dt;
            dW[z] *= dealias[z] * dt;
          }
      for (i = 0; i < n1; i++)
        for (j = 0; j < N; j++)
          for (k = 0; k < Nf; k++) {
            z = (i * N + j) * Nf + k;
            P_hat[z] =
                (dU[z] * kx[i + s1] + dV[z] * kx[j] + dW[z] * kz[k]) / kk[z];
            dU[z] -= P_hat[z] * kx[i + s1] + nu * dt * kk[z] * U_hat[z];
            dV[z] -= P_hat[z] * kx[j] + nu * dt * kk[z] * V_hat[z];
            dW[z] -= P_hat[z] * kz[k] + nu * dt * kk[z] * W_hat[z];
          }

      if (rk < 3) {
        for (i = 0; i < n1; i++)
          for (j = 0; j < N; j++)
            for (k = 0; k < Nf; k++) {
              z = (i * N + j) * Nf + k;
              U_hat[z] = U_hat0[z] + b[rk] * dU[z];
              V_hat[z] = V_hat0[z] + b[rk] * dV[z];
              W_hat[z] = W_hat0[z] + b[rk] * dW[z];
            }
      }
      for (i = 0; i < n1; i++)
        for (j = 0; j < N; j++)
          for (k = 0; k < Nf; k++) {
            z = (i * N + j) * Nf + k;
            U_hat1[z] += a[rk] * dU[z];
            V_hat1[z] += a[rk] * dV[z];
            W_hat1[z] += a[rk] * dW[z];
          }
    }
    for (i = 0; i < n1; i++)
      for (j = 0; j < N; j++)
        for (k = 0; k < Nf; k++) {
          z = (i * N + j) * Nf + k;
          U_hat[z] = U_hat1[z];
          V_hat[z] = V_hat1[z];
          W_hat[z] = W_hat1[z];
        }

    if (tstep % 2 == 0) {
      s_in = 0.0;
      for (i = 0; i < n0; i++)
        for (j = 0; j < N; j++)
          for (k = 0; k < N; k++) {
            z = (i * N + j) * 2 * Nf + k;
            s_in += U[z] * U[z] + V[z] * V[z] + W[z] * W[z];
          }
      s_in *= 0.5 * dx * dx * dx / L / L / L;
      MPI_Reduce(&s_in, &s_out, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);
      if (rank == 0)
        fprintf(stderr, "eng: %.16e\n", s_out);
    }
  }
  fftw_free(U);
  fftw_free(V);
  fftw_free(W);
  fftw_free(U_tmp);
  fftw_free(V_tmp);
  fftw_free(W_tmp);
  fftw_free(CU);
  fftw_free(CV);
  fftw_free(CW);
  fftw_free(dealias);
  fftw_free(kk);
  fftw_free(U_hat);
  fftw_free(V_hat);
  fftw_free(W_hat);
  fftw_free(P_hat);
  fftw_free(U_hat0);
  fftw_free(V_hat0);
  fftw_free(W_hat0);
  fftw_free(U_hat1);
  fftw_free(V_hat1);
  fftw_free(W_hat1);
  fftw_free(dU);
  fftw_free(dV);
  fftw_free(dW);
  fftw_free(curlX);
  fftw_free(curlY);
  fftw_free(curlZ);
  fftw_destroy_plan(irfftn);
  fftw_destroy_plan(rfftn);
  fftw_mpi_cleanup();
  MPI_Finalize();
}
