#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define bit(A,i) ((A >> i) & 1)

typedef unsigned long long int u_64;
typedef unsigned int u_32;

static void print_double(double d)
{
  u_64 a = *((u_64*) &d);

  for(int i = 63; i >= 0; i--)
    (i == 63 || i == 52) ? printf("%lld ", bit(a,i)) : printf("%lld", bit(a,i));

  printf("\n");
}

static void print_float(float f)
{
  u_32 a = *((u_32*) &f);

  for(int i = 31; i >= 0; i--)
    (i == 31 || i == 23) ? printf("%d ", bit(a,i)) : printf("%d", bit(a,i));

  printf("\n");
}

void double_fct(double full_mantissa)
{
	double zero = 0.0;

	double result = full_mantissa + zero;

	// not modified function
	print_double(result);
}

void double_temoin(double full_mantissa)
{
	double zero = 0.0;

	double result = full_mantissa + zero;

	// not modified function
	print_double(result);	

	// modified function
	double_fct(full_mantissa);
}

void float_fct(float full_mantissa)
{
	float zero = 0.0;

	float result = full_mantissa + zero;

	// not modified function 
	print_float(result);
}

void float_temoin(float full_mantissa)
{
	float zero = 0.0;

	float result = full_mantissa + zero;

	// not modified function
	print_float(result);

	// modified function
	float_fct(full_mantissa);
}

int main(int argc, char const *argv[])
{
	if (argc < 3){
		fprintf(stderr, "./test [double mantissa_size] [float mantissa_size]\n");
		abort();
	}

	int size_64 = atoi(argv[1]);
	int size_32 = atoi(argv[2]);
	union {u_32 i; float f;} t_32;
	union {u_64 i; double f;} t_64;

	// not modified function 
	t_64.i = pow(2, 52-size_64)-1;

	// modified function
	double_fct(t_64.f);

	// not modified function
	double_temoin(t_64.f);

	// modified function
	printf("%la\n", pow(t_64.f,1));

	// not modified function
	printf("%la\n", pow(t_64.f,1));

	// not modified function 
	t_32.i = pow(2, 23-size_32)-1;

	// modified function
	float_fct(t_32.f);

	// not modified function
	float_temoin(t_32.f);

	// modified function
	printf("%a\n", powf(t_32.f,1));

	// not modified function
	printf("%a\n", powf(t_32.f,1));

	printf("\n");

	return 0;
}