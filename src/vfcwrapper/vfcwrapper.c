/*****************************************************************************
 *                                                                           *
 *  This file is part of Verificarlo.                                        *
 *                                                                           *
 *  Copyright (c) 2015-2019                                                  *
 *     Verificarlo contributors                                              *
 *     Universite de Versailles St-Quentin-en-Yvelines                       *
 *     CMLA, Ecole Normale Superieure de Cachan                              *
 *                                                                           *
 *  Verificarlo is free software: you can redistribute it and/or modify      *
 *  it under the terms of the GNU General Public License as published by     *
 *  the Free Software Foundation, either version 3 of the License, or        *
 *  (at your option) any later version.                                      *
 *                                                                           *
 *  Verificarlo is distributed in the hope that it will be useful,           *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of           *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            *
 *  GNU General Public License for more details.                             *
 *                                                                           *
 *  You should have received a copy of the GNU General Public License        *
 *  along with Verificarlo.  If not, see <http://www.gnu.org/licenses/>.     *
 *                                                                           *
 *****************************************************************************/

#include <dlfcn.h>
#include <err.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interflop.h"

#define MAX_BACKENDS 16

struct interflop_backend_interface_t backends[MAX_BACKENDS];
void *contexts[MAX_BACKENDS];
unsigned char loaded_backends = 0;

typedef struct interflop_backend_interface_t (*interflop_init_t)(
    void **context);

/* vfc_init is run when loading vfcwrapper and initializes vfc backends */
__attribute__((constructor)) static void vfc_init(void) {
  /* Parse VFC_BACKENDS */
  char *vfc_backends = getenv("VFC_BACKENDS");
  if (vfc_backends == NULL) {
    errx(1, "VFC_BACKENDS is empty, at least one backend should be provided");
  }

  /* For each backend, load and register the backend vtable interface */
  char *token = strtok(vfc_backends, " ");
  while (token) {
    /* load the backend .so */
    void *handle = dlopen(token, RTLD_NOW);
    if (handle == NULL) {
      errx(1, "Cannot load backend %s: dlopen error", token);
    }

    warn("Loading backend %s", token);

    /* reset dl errors */
    dlerror();

    /* get the address of the interflop_init function */
    interflop_init_t handle_init =
        (interflop_init_t)dlsym(handle, "interflop_init");
    const char *dlsym_error = dlerror();
    if (dlsym_error) {
      errx(1, "Cannot find interflop_init function in backend %s: %s", token,
           strerror(errno));
    }

    /* Register backend */
    if (loaded_backends == MAX_BACKENDS) {
      fprintf(stderr, "No more than %d backends can be used simultaneously",
              MAX_BACKENDS);
    }
    backends[loaded_backends] = handle_init(&contexts[loaded_backends]);
    loaded_backends++;

    /* parse next backend token */
    token = strtok(NULL, "");
  }
}

typedef double double2 __attribute__((ext_vector_type(2)));
typedef double double4 __attribute__((ext_vector_type(4)));
typedef float float2 __attribute__((ext_vector_type(2)));
typedef float float4 __attribute__((ext_vector_type(4)));
typedef bool bool2 __attribute__((ext_vector_type(2)));
typedef bool bool4 __attribute__((ext_vector_type(4)));

/* Arithmetic wrappers */

float _floatadd(float a, float b) {
  float c;
  for (int i = 0; i < loaded_backends; i++) {
    backends[i].interflop_add_float(a, b, &c, NULL);
  }
  return c;
}

float _floatsub(float a, float b) {
  float c;
  for (int i = 0; i < loaded_backends; i++) {
    backends[i].interflop_sub_float(a, b, &c, NULL);
  }
  return c;
}

float _floatmul(float a, float b) {
  float c;
  for (int i = 0; i < loaded_backends; i++) {
    backends[i].interflop_mul_float(a, b, &c, NULL);
  }
  return c;
}

float _floatdiv(float a, float b) {
  float c;
  for (int i = 0; i < loaded_backends; i++) {
    backends[i].interflop_div_float(a, b, &c, NULL);
  }
  return c;
}

bool _floatcmp(enum FCMP_PREDICATE p, float a, float b) {
  bool c;
  for (int i = 0; i < loaded_backends; i++) {
    backends[i].interflop_cmp_float(p, a, b, &c, NULL);
  }
  return c;
}

double _doubleadd(double a, double b) {
  double c;
  for (int i = 0; i < loaded_backends; i++) {
    backends[i].interflop_add_double(a, b, &c, NULL);
  }
  return c;
}

double _doublesub(double a, double b) {
  double c;
  for (int i = 0; i < loaded_backends; i++) {
    backends[i].interflop_sub_double(a, b, &c, NULL);
  }
  return c;
}

double _doublemul(double a, double b) {
  double c;
  for (int i = 0; i < loaded_backends; i++) {
    backends[i].interflop_mul_double(a, b, &c, NULL);
  }
  return c;
}

double _doublediv(double a, double b) {
  double c;
  for (int i = 0; i < loaded_backends; i++) {
    backends[i].interflop_div_double(a, b, &c, NULL);
  }
  return c;
}

bool _doublecmp(enum FCMP_PREDICATE p, double a, double b) {
  bool c;
  for (int i = 0; i < loaded_backends; i++) {
    backends[i].interflop_cmp_double(p, a, b, &c, NULL);
  }
  return c;
}

/* Arithmetic vector wrappers */

double2 _2xdoubleadd(double2 a, double2 b) {
  double2 c;

  c[0] = _doubleadd(a[0], b[0]);
  c[1] = _doubleadd(a[1], b[1]);
  return c;
}

double2 _2xdoublesub(double2 a, double2 b) {
  double2 c;

  c[0] = _doublesub(a[0], b[0]);
  c[1] = _doublesub(a[1], b[1]);
  return c;
}

double2 _2xdoublemul(double2 a, double2 b) {
  double2 c;

  c[0] = _doublemul(a[0], b[0]);
  c[1] = _doublemul(a[1], b[1]);
  return c;
}

double2 _2xdoublediv(double2 a, double2 b) {
  double2 c;

  c[0] = _doublediv(a[0], b[0]);
  c[1] = _doublediv(a[1], b[1]);
  return c;
}

bool2 _2xdoublecmp(enum FCMP_PREDICATE p, double2 a, double2 b) {
  bool2 c;

  c[0] = _doublecmp(p, a[0], b[0]);
  c[1] = _doublecmp(p, a[1], b[1]);
  return c;
}

/*********************************************************/

double4 _4xdoubleadd(double4 a, double4 b) {
  double4 c;

  c[0] = _doubleadd(a[0], b[0]);
  c[1] = _doubleadd(a[1], b[1]);
  c[2] = _doubleadd(a[2], b[2]);
  c[3] = _doubleadd(a[3], b[3]);
  return c;
}

double4 _4xdoublesub(double4 a, double4 b) {
  double4 c;

  c[0] = _doublesub(a[0], b[0]);
  c[1] = _doublesub(a[1], b[1]);
  c[2] = _doublesub(a[2], b[2]);
  c[3] = _doublesub(a[3], b[3]);
  return c;
}

double4 _4xdoublemul(double4 a, double4 b) {
  double4 c;

  c[0] = _doublemul(a[0], b[0]);
  c[1] = _doublemul(a[1], b[1]);
  c[2] = _doublemul(a[2], b[2]);
  c[3] = _doublemul(a[3], b[3]);
  return c;
}

double4 _4xdoublediv(double4 a, double4 b) {
  double4 c;

  c[0] = _doublediv(a[0], b[0]);
  c[1] = _doublediv(a[1], b[1]);
  c[2] = _doublediv(a[2], b[2]);
  c[3] = _doublediv(a[3], b[3]);
  return c;
}

bool4 _4xdoublecmp(enum FCMP_PREDICATE p, double4 a, double4 b) {
  bool4 c;

  c[0] = _doublecmp(p, a[0], b[0]);
  c[1] = _doublecmp(p, a[1], b[1]);
  c[2] = _doublecmp(p, a[2], b[2]);
  c[3] = _doublecmp(p, a[3], b[3]);
  return c;
}

/*********************************************************/

float2 _2xfloatadd(float2 a, float2 b) {
  float2 c;

  c[0] = _floatadd(a[0], b[0]);
  c[1] = _floatadd(a[1], b[1]);
  return c;
}

float2 _2xfloatsub(float2 a, float2 b) {
  float2 c;

  c[0] = _floatsub(a[0], b[0]);
  c[1] = _floatsub(a[1], b[1]);
  return c;
}

float2 _2xfloatmul(float2 a, float2 b) {
  float2 c;

  c[0] = _floatmul(a[0], b[0]);
  c[1] = _floatmul(a[1], b[1]);
  return c;
}

float2 _2xfloatdiv(float2 a, float2 b) {
  float2 c;

  c[0] = _floatdiv(a[0], b[0]);
  c[1] = _floatdiv(a[1], b[1]);
  return c;
}

bool2 _2xfloatcmp(enum FCMP_PREDICATE p, float2 a, float2 b) {
  bool2 c;

  c[0] = _floatcmp(p, a[0], b[0]);
  c[1] = _floatcmp(p, a[1], b[1]);
  return c;
}

/*********************************************************/

float4 _4xfloatadd(float4 a, float4 b) {
  float4 c;

  c[0] = _floatadd(a[0], b[0]);
  c[1] = _floatadd(a[1], b[1]);
  c[2] = _floatadd(a[2], b[2]);
  c[3] = _floatadd(a[3], b[3]);
  return c;
}

float4 _4xfloatsub(float4 a, float4 b) {
  float4 c;

  c[0] = _floatsub(a[0], b[0]);
  c[1] = _floatsub(a[1], b[1]);
  c[2] = _floatsub(a[2], b[2]);
  c[3] = _floatsub(a[3], b[3]);
  return c;
}

float4 _4xfloatmul(float4 a, float4 b) {
  float4 c;

  c[0] = _floatmul(a[0], b[0]);
  c[1] = _floatmul(a[1], b[1]);
  c[2] = _floatmul(a[2], b[2]);
  c[3] = _floatmul(a[3], b[3]);
  return c;
}

float4 _4xfloatdiv(float4 a, float4 b) {
  float4 c;

  c[0] = _floatdiv(a[0], b[0]);
  c[1] = _floatdiv(a[1], b[1]);
  c[2] = _floatdiv(a[2], b[2]);
  c[3] = _floatdiv(a[3], b[3]);
  return c;
}

bool4 _4xfloatcmp(enum FCMP_PREDICATE p, float4 a, float4 b) {
  bool4 c;

  c[0] = _floatcmp(p, a[0], b[0]);
  c[1] = _floatcmp(p, a[1], b[1]);
  c[2] = _floatcmp(p, a[2], b[2]);
  c[3] = _floatcmp(p, a[3], b[3]);
  return c;
}
