#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <threads.h>
#include <complex.h>

typedef int TYPE_ATTR;
typedef short TYPE_CONV;
typedef double complex (*FunctionPtr)(double complex);

const char colors[10][12] = {
  "255 0 0 ",     // Red
  "0 255 0 ",     // Green
  "0 0 255 ",     // Blue
  "255 255 0 ",   // Yellow
  "255 0 255 ",   // Magenta
  "0 255 255 ",   // Cyan
  "128 0 0 ",     // Maroon
  "0 128 0 ",     // Olive
  "0 0 128 ",     // Navy
  "128 128 128 "   // Gray
};


double complex n1(double complex x) { return 1+0*I; }
double complex n2(double complex x) { return 1./(2.*x)  +  x/2.; }
double complex n3(double complex x) { return 1./(3.*x*x) + 2.*x/3.; }
double complex n4(double complex x) { return 0.25/(x*x*x) + 0.75*x; }
double complex n5(double complex x) { return 0.2*1./(x*x*x*x) + 0.8*x; }
double complex n6(double complex x) { complex double x2 = x*x; return 1./(6.*x2*x2*x2) + 5.*x/6.; }
double complex n7(double complex x) { complex double x2 = x*x; return 1./(7.*x2*x2*x2*x) + 6.*x/7.; }
double complex n8(double complex x) { complex double x2 = x*x; return 0.125/(x2*x2*x2*x2) + 7.*x/8.; }
double complex n9(double complex x) { complex double x2 = x*x; return 1./(9.*x2*x2*x2*x2*x) + 8.*x/9.; }
double complex n10(double complex x) { complex double x2 = x*x; return 0.1/(x2*x2*x2*x2*x2) + 9.*x/10.; }


typedef struct {
  int val;
  char pad[60]; // cacheline - sizeof(int)
} int_padded;

typedef struct {
  TYPE_ATTR **attractors;
  TYPE_CONV **convergences;
  int ib;
  int istep;
  int sz;
  int tx;
  int d;
  double complex *roots;
  mtx_t *mtx;
  cnd_t *cnd;
  int_padded *status;
  double complex (*newtonIteration)(double complex); 
} thrd_info_t;

typedef struct {
  TYPE_ATTR **attractors;
  TYPE_CONV **convergences;
  float **w;
  int sz;
  int nthrds;
  int d;
  mtx_t *mtx;
  cnd_t *cnd;
  int_padded *status;
} thrd_info_check_t;

static inline void compute_roots(int d, double complex *roots){
  double angle = 2.0 * 3.14159 / d;
  for ( int i = 0; i < d; i++ ){
    roots[i] = cos(i * angle) + sin(i * angle) * I;
  }
}

static inline void parse_args(int argc, char *argv[], int *nthrds, int *size, int *d) {
  if (argc != 4) {
    printf("Usage: newton -t<numberOfThreads> -l<numberOfLines> <degreeOfPolynomial>\n");
    exit(EXIT_FAILURE);
  }
  int num_threads_set = 0, size_set = 0, d_set = 0;

  for (int i = 1; i < argc; i++) {
    char *arg = argv[i];
    if (arg[0] == '-') {
      if (arg[1] == 't') {
        *nthrds = strtol(arg + 2, NULL, 10);
        num_threads_set = 1;
      } else if (arg[1] == 'l') {
        *size = strtol(arg + 2, NULL, 10);
        size_set = 1;
      } else {
        printf("Unrecognized option: %s\n", arg);
        printf("Usage: ./newton -t<numberOfThreads> -l<numberOfRows> <degreeOfPolynomial>\n");
        exit(EXIT_FAILURE);
      }
    } else {
      *d = strtol(arg, NULL, 10);
      d_set = 1;
    }
  }
  if (!num_threads_set || !size_set || !d_set) {
    printf("Usage: ./newton -t<numberOfThreads> -l<numberOfRows> <degreeOfPolynomial>\n");
    exit(EXIT_FAILURE);
  }
}


static inline double complex fofx(double complex x, int d){
  return cpow(x, d) - 1.0;
}

static inline double complex fprimeofx(double complex x, int d){
  return d * cpow(x, d - 1);
}



static inline void compute_distances(int size, int d, int ix, TYPE_ATTR *attractor, TYPE_CONV *convergence, double complex *roots, double complex (*newtonIteration)(double complex)){
  double tol = 1e-3;
  int max_iter = 128;
  double complex x_new, x_old;
  double b = 2.0-(4.0*ix/size);

  for ( int j = 0; j < size; j++){
    int iter = 1;
    double a = -2.0+(4.0*j/size);
    x_old = a + b*I;
    for ( iter; iter < max_iter; iter++){ 
      x_new = newtonIteration(x_old);
      // x_new = x_old - fofx(x_old, d)/fprimeofx(x_old,d);  
      
      if (cabs(fofx(x_new,d)) < 1e-3)  
        break;
      if (cabs(creal(x_new)) > 1e10 || cabs(cimag(x_new)) > 1e10) 
        break;
      x_old = x_new;
    }

    // Check which root it converges to and set the attractor
    double min_cabs = 1e10;
    for ( int k = 0; k < d; k++ ){
      if (cabs(x_new - roots[k]) < min_cabs ){
        min_cabs = cabs(x_new - roots[k]);
        attractor[j] = k;
      }
    }
    convergence[j] = iter;
  }
}

int main_thrd(void *args){
  const thrd_info_t *thrd_info = (thrd_info_t*) args;
  TYPE_ATTR **attractors = thrd_info->attractors;
  TYPE_CONV **convergences = thrd_info->convergences;
  const int ib = thrd_info->ib;
  const int istep = thrd_info->istep;
  const int sz = thrd_info->sz;
  const int tx = thrd_info->tx;
  const int d = thrd_info->d;
  mtx_t *mtx = thrd_info->mtx;
  cnd_t *cnd = thrd_info->cnd;
  int_padded *status = thrd_info->status;
  double complex *roots = thrd_info->roots;
  double complex (*newtonIteration)(double complex) = thrd_info->newtonIteration;

  for ( int ix = ib; ix < sz; ix += istep ) {
    TYPE_ATTR *attractor =   (TYPE_ATTR*) malloc(sz*sizeof(TYPE_ATTR));
    TYPE_CONV *convergence = (TYPE_CONV*) malloc(sz*sizeof(TYPE_CONV));
    if ( attractor == NULL || convergence == NULL ) {
      perror("Memory allocation failed");
      exit(EXIT_FAILURE);
    }
    for  ( int jx = 0; jx < sz; ++jx ){
      attractor[jx] = 0;
      convergence[jx] = 0;
    }

    compute_distances(sz, d, ix, attractor, convergence, roots, newtonIteration);

    mtx_lock(mtx);
    attractors[ix] = attractor;
    convergences[ix] = convergence;
    status[tx].val = ix + istep;
    mtx_unlock(mtx);
    cnd_signal(cnd);
  }

  return 0;
}


int main_thrd_check(void *args){
  const thrd_info_check_t *thrd_info = (thrd_info_check_t*) args;
  const TYPE_ATTR *const *attractors = (const TYPE_ATTR *const *)thrd_info->attractors;
  const TYPE_CONV *const *convergences = (const TYPE_CONV *const *)thrd_info->convergences;
  const int sz = thrd_info->sz;
  const int nthrds = thrd_info->nthrds;
  mtx_t *mtx = thrd_info->mtx;
  cnd_t *cnd = thrd_info->cnd;
  int d = thrd_info -> d;
  int_padded *status = thrd_info->status;

  char convFileName[sizeof("newton_convergence_x%d.ppm")];
  char attrFileName[sizeof("newton_attractors_x%d.ppm")];
  sprintf(convFileName, "newton_convergence_x%d.ppm", d);
  sprintf(attrFileName, "newton_attractors_x%d.ppm", d);
  FILE *fpConv = fopen(convFileName, "w");
  FILE *fpAttr = fopen(attrFileName, "w");

  if (fpConv == NULL || fpAttr == NULL) {
    perror("Failed to open the file");
    exit(EXIT_FAILURE);
  }
  fprintf(fpConv, "P3\n%d %d\n255\n", sz, sz);
  fprintf(fpAttr, "P3\n%d %d\n255\n", sz, sz);

  size_t pxl_size = 12;
  char convArr[256][pxl_size];

  for (int jx =0; jx<256; ++jx)
    sprintf(convArr[jx], "%03i %03i %03i ", jx, jx, jx);


  int cap = 10;
  for ( int ix = 0, ibnd; ix < sz; ) {
    // Compute min if new row available
    for ( mtx_lock(mtx); ; ) {
      ibnd = sz;
      for ( int tx = 0; tx < nthrds; ++tx )
        if ( ibnd > status[tx].val )
          ibnd = status[tx].val;

      if ( ibnd > ix ){
        mtx_unlock(mtx);
        break;
      }
      cnd_wait(cnd,mtx);
    }

    if (ibnd > ix + cap || ibnd == sz) {
      fprintf(stderr, "checking until %i\n", ibnd);

      int nrRows = ibnd - ix;
      char *conv_buf = malloc(sz*pxl_size*nrRows);
      char *attr_buf = malloc(sz*pxl_size*nrRows);

      for (int idx=0; ix < ibnd; ++ix, ++idx ){

        for (int jx = 0; jx < sz; ++jx){
          int scaledConv = (int)((convergences[ix][jx] - 1) * 255 / 127);

          memcpy(conv_buf+idx*pxl_size*sz+jx*pxl_size ,convArr[scaledConv],pxl_size); 
          memcpy(attr_buf+idx*pxl_size*sz+jx*pxl_size ,colors[attractors[ix][jx]],pxl_size); 

        }
        free((void *)attractors[ix]);
        free((void *)convergences[ix]);
      }

      fwrite(conv_buf, 1, pxl_size*sz*nrRows, fpConv);
      fwrite(attr_buf, 1, pxl_size*sz*nrRows, fpAttr);

      free(conv_buf);
      free(attr_buf);
   }
  }
  fclose(fpConv);
  fclose(fpAttr);
  return 0;
}


int main(int argc, char *argv[]){
  int nthrds, sz, d;
  parse_args(argc, argv, &nthrds, &sz, &d);

  FunctionPtr functions[] = {n1, n2, n3, n4, n5, n6, n7, n8, n9, n10};
  FunctionPtr newtonIteration = functions[d-1];

  double complex tmpRoots[d];
  compute_roots(d, tmpRoots);

  TYPE_ATTR **attractors = (TYPE_ATTR**) malloc(sz*sizeof(TYPE_ATTR*));
  TYPE_CONV **convergences = (TYPE_CONV**) malloc(sz*sizeof(TYPE_CONV*));
  if ( attractors == NULL || convergences == NULL ) {
    perror("Memory allocation failed");
    exit(EXIT_FAILURE);
  }

  thrd_t thrds[nthrds];
  thrd_info_t thrds_info[nthrds];

  thrd_t thrd_check;
  thrd_info_check_t thrd_info_check;
  
  mtx_t mtx;
  mtx_init(&mtx, mtx_plain);

  cnd_t cnd;
  cnd_init(&cnd);

  int_padded status[nthrds];

  for ( int tx = 0; tx < nthrds; ++tx ) {
    thrds_info[tx].attractors = attractors;
    thrds_info[tx].convergences = convergences;
    thrds_info[tx].ib = tx;
    thrds_info[tx].istep = nthrds;
    thrds_info[tx].sz = sz;
    thrds_info[tx].tx = tx;
    thrds_info[tx].d = d;
    thrds_info[tx].mtx = &mtx;
    thrds_info[tx].cnd = &cnd;
    thrds_info[tx].status = status;
    thrds_info[tx].roots = tmpRoots;
    thrds_info[tx].newtonIteration = newtonIteration;
    status[tx].val = -1;

    int r = thrd_create(thrds+tx, main_thrd, (void*) (thrds_info+tx));
    if ( r != thrd_success ) {
      fprintf(stderr, "failed to create thread\n");
      exit(1);
    }
    thrd_detach(thrds[tx]);
  }

  {
    thrd_info_check.attractors = attractors;
    thrd_info_check.convergences = convergences;
    thrd_info_check.sz = sz;
    thrd_info_check.nthrds = nthrds;
    thrd_info_check.d = d;
    thrd_info_check.mtx = &mtx;
    thrd_info_check.cnd = &cnd;
    thrd_info_check.status = status;
    int r = thrd_create(&thrd_check, main_thrd_check, (void*) (&thrd_info_check));
    if ( r != thrd_success ) {
      fprintf(stderr, "failed to create thread\n");
      exit(1);
    }
  }

  {
    int r;
    thrd_join(thrd_check, &r);
  }

  free(attractors);
  free(convergences);
  mtx_destroy(&mtx);
  cnd_destroy(&cnd);
  return 0;
}