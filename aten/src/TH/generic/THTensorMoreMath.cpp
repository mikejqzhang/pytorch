#ifndef TH_GENERIC_FILE
#define TH_GENERIC_FILE "generic/THTensorMoreMath.cpp"
#else

#include <TH/generic/THTensorApply.hpp>

void THTensor_(baddbmm)(THTensor *result, real beta, THTensor *t, real alpha, THTensor *batch1, THTensor *batch2)
{
  int64_t batch;

  THArgCheck(THTensor_(nDimensionLegacyNoScalars)(batch1) == 3, 1, "expected 3D tensor, got %dD", THTensor_(nDimensionLegacyNoScalars)(batch1));
  THArgCheck(THTensor_(nDimensionLegacyNoScalars)(batch2) == 3, 2, "expected 3D tensor, got %dD", THTensor_(nDimensionLegacyNoScalars)(batch2));
  THArgCheck(THTensor_(size)(batch1, 0) == THTensor_(size)(batch2, 0), 2,
             "equal number of batches expected, got %d, %d",
             THTensor_(size)(batch1, 0), THTensor_(size)(batch2, 0));
  THArgCheck(THTensor_(size)(batch1, 2) == THTensor_(size)(batch2, 1), 2,
             "wrong matrix size, batch1: %dx%d, batch2: %dx%d",
             THTensor_(size)(batch1, 1), THTensor_(size)(batch1, 2),
             THTensor_(size)(batch2, 1), THTensor_(size)(batch2, 2));

  int64_t bs = THTensor_(size)(batch1, 0);
  int64_t dim1 = THTensor_(size)(batch1, 1);
  int64_t dim2 = THTensor_(size)(batch2, 2);
  THArgCheck(THTensor_(size)(t, 0) == bs, 1,   "output tensor of incorrect size");
  THArgCheck(THTensor_(size)(t, 1) == dim1, 1, "output tensor of incorrect size");
  THArgCheck(THTensor_(size)(t, 2) == dim2, 1, "output tensor of incorrect size");

  if (t != result) {
    THTensor_(resizeAs)(result, t);
    if (beta != 0.0) {
      THTensor_(copy)(result, t);
    }
  }

  THTensor *matrix1 = THTensor_(new)();
  THTensor *matrix2 = THTensor_(new)();
  THTensor *result_matrix = THTensor_(new)();

  for (batch = 0; batch < THTensor_(size)(batch1, 0); ++batch) {
    THTensor_(select)(matrix1, batch1, 0, batch);
    THTensor_(select)(matrix2, batch2, 0, batch);
    THTensor_(select)(result_matrix, result, 0, batch);

    THTensor_(addmm)(result_matrix, beta, result_matrix, alpha, matrix1, matrix2);
  }

  THTensor_(free)(matrix1);
  THTensor_(free)(matrix2);
  THTensor_(free)(result_matrix);
}

ptrdiff_t THTensor_(numel)(THTensor *t)
{
  return THTensor_(nElement)(t);
}


// Helper function to be used in a reduction operation.
// Due to resize semantics of outputs, if the specified output tensor r_ has
// same size as the output of the reduction operation, then any noncontiguities
// in r_ should be preserved.
// The reduction operation, however, needs to act on r_ with an extra dimension
// (the reduced dimension), so this function "resizes" r_ and preserves its
// noncontiguities if necessary.
void THTensor_(preserveReduceDimSemantics)(
    THTensor *r_, int in_dims, int reduce_dimension, int keepdim) {
  if (r_ && !keepdim &&
      THTensor_(nDimensionLegacyAll)(r_) == in_dims - 1 &&
      THTensor_(nDimensionLegacyAll)(r_) != 0) {
    THTensor_(unsqueeze1d)(r_, r_, reduce_dimension);
  }
}

void THTensor_(max)(THTensor *values_, THLongTensor *indices_, THTensor *t, int dimension, int keepdim)
{
  THLongStorage *dim;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 2, "dimension %d out of range",
      dimension + TH_INDEX_BASE);

  int in_dims = THTensor_(nDimensionLegacyAll)(t);
  THTensor_(preserveReduceDimSemantics)(values_, in_dims, dimension, keepdim);
  THLongTensor_preserveReduceDimSemantics(indices_, in_dims, dimension, keepdim);
  dim = THTensor_(newSizeOf)(t);
  THLongStorage_set(dim, dimension, 1);
  THTensor_(resize)(values_, dim, NULL);
  THLongTensor_resize(indices_, dim, NULL);
  THLongStorage_free(dim);

  // two implementations optimized for data locality
  if (t->stride(dimension) == 1) {
    real theMax;
    real value;
    int64_t theIndex;
    int64_t i;
    TH_TENSOR_DIM_APPLY3(real, t, real, values_, int64_t, indices_, dimension,
                         TH_TENSOR_DIM_APPLY3_SIZE_EQ_EXCEPT_DIM,
                         theMax = t_data[0];
                         theIndex = 0;

                         for(i = 0; i < t_size; i++)
                         {
                           value = t_data[i*t_stride];
                           /* This is not the same as value>theMax in the case of NaNs */
                           if(!(value <= theMax))
                           {
                             theIndex = i;
                             theMax = value;
                             th_isnan_break(value)
                           }
                         }
                         *indices__data = theIndex;
                         *values__data = theMax;);
  } else {
    if (THTensor_(nDimensionLegacyAll)(t) > 1) {
      THTensor *t0 = THTensor_(newSelect)(t, dimension, 0);
      THTensor_(copy)(values_, t0);
      THTensor_(free)(t0);
    } else {
      THTensor_(fill)(values_, THTensor_(get1d)(t, 0));
    }
    THLongTensor_zero(indices_);

    if(t->size(dimension) == 1) {
      if (!keepdim) {
        THTensor_(squeeze1d)(values_, values_, dimension);
        THLongTensor_squeeze1d(indices_, indices_, dimension);
      }
      return;
    }

    THTensor *tempValues_ = THTensor_(newWithTensor)(values_);
    // tempValues_.expand_as(t)
    THTensor_setSizeAtDim(tempValues_, dimension, t->size(dimension));
    THTensor_setStrideAtDim(tempValues_, dimension, 0);

    THLongTensor *tempIndices_ = THLongTensor_newWithTensor(indices_);
    // tempIndices_.expand_as(t)
    THTensor_setSizeAtDim(tempIndices_, dimension, t->size(dimension));
    THTensor_setStrideAtDim(tempIndices_, dimension, 0);

    TH_TENSOR_APPLY3_D(real, t, real, tempValues_, int64_t, tempIndices_, dimension,
                          if(!(*t_data <= *tempValues__data) && !th_isnan(*tempValues__data)) {
                            *tempValues__data = *t_data;
                            *tempIndices__data = *tempIndices__dimOffset;
                          });

    THTensor_(free)(tempValues_);
    THLongTensor_free(tempIndices_);
  }

  if (!keepdim) {
    THTensor_(squeeze1d)(values_, values_, dimension);
    THLongTensor_squeeze1d(indices_, indices_, dimension);
  }
}

void THTensor_(min)(THTensor *values_, THLongTensor *indices_, THTensor *t, int dimension, int keepdim)
{
  THLongStorage *dim;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 2, "dimension %d out of range",
      dimension + TH_INDEX_BASE);

  int in_dims = THTensor_(nDimensionLegacyAll)(t);
  THTensor_(preserveReduceDimSemantics)(values_, in_dims, dimension, keepdim);
  THLongTensor_preserveReduceDimSemantics(indices_, in_dims, dimension, keepdim);
  dim = THTensor_(newSizeOf)(t);
  THLongStorage_set(dim, dimension, 1);
  THTensor_(resize)(values_, dim, NULL);
  THLongTensor_resize(indices_, dim, NULL);
  THLongStorage_free(dim);

  // two implementations optimized for data locality
  if (t->stride(dimension) == 1) {
    real theMax;
    real value;
    int64_t theIndex;
    int64_t i;
    TH_TENSOR_DIM_APPLY3(real, t, real, values_, int64_t, indices_, dimension,
                         TH_TENSOR_DIM_APPLY3_SIZE_EQ_EXCEPT_DIM,
                         theMax = t_data[0];
                         theIndex = 0;

                         for(i = 0; i < t_size; i++)
                         {
                           value = t_data[i*t_stride];
                           /* This is not the same as value>theMax in the case of NaNs */
                           if(!(value >= theMax))
                           {
                             theIndex = i;
                             theMax = value;
                             th_isnan_break(value)
                           }
                         }
                         *indices__data = theIndex;
                         *values__data = theMax;);
  } else {
    if (THTensor_(nDimensionLegacyAll)(t) > 1) {
      THTensor *t0 = THTensor_(newSelect)(t, dimension, 0);
      THTensor_(copy)(values_, t0);
      THTensor_(free)(t0);
    } else {
      THTensor_(fill)(values_, THTensor_(get1d)(t, 0));
    }
    THLongTensor_zero(indices_);

    if(t->size(dimension) == 1) {
      if (!keepdim) {
        THTensor_(squeeze1d)(values_, values_, dimension);
        THLongTensor_squeeze1d(indices_, indices_, dimension);
      }
      return;
    }

    THTensor *tempValues_ = THTensor_(newWithTensor)(values_);
    // tempValues_.expand_as(t)
    THTensor_setSizeAtDim(tempValues_, dimension, t->size(dimension));
    THTensor_setStrideAtDim(tempValues_, dimension, 0);

    THLongTensor *tempIndices_ = THLongTensor_newWithTensor(indices_);
    // tempIndices_.expand_as(t)
    THTensor_setSizeAtDim(tempIndices_, dimension, t->size(dimension));
    THTensor_setStrideAtDim(tempIndices_, dimension, 0);

    TH_TENSOR_APPLY3_D(real, t, real, tempValues_, int64_t, tempIndices_, dimension,
                          if(!(*t_data >= *tempValues__data) && !th_isnan(*tempValues__data)) {
                            *tempValues__data = *t_data;
                            *tempIndices__data = *tempIndices__dimOffset;
                          });

    THTensor_(free)(tempValues_);
    THLongTensor_free(tempIndices_);
  }

  if (!keepdim) {
    THTensor_(squeeze1d)(values_, values_, dimension);
    THLongTensor_squeeze1d(indices_, indices_, dimension);
  }
}

void THTensor_(sum)(THTensor *r_, THTensor *t, int dimension, int keepdim)
{
  THLongStorage *dim;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 2, "dimension %d out of range",
      dimension + TH_INDEX_BASE);

  THTensor_(preserveReduceDimSemantics)(r_, THTensor_(nDimensionLegacyAll)(t), dimension, keepdim);
  dim = THTensor_(newSizeOf)(t);
  THLongStorage_set(dim, dimension, 1);
  THTensor_(resize)(r_, dim, NULL);
  THLongStorage_free(dim);

  int serial_path = 0;
#ifdef _OPENMP
  int inOMP = omp_in_parallel();
  if (inOMP) {
    serial_path = 1;
  } else {
    int r_Contig = THTensor_(isContiguous)(r_);
    real *tp = THTensor_(data)(t);
    real *rp = THTensor_(data)(r_);
    if(r_Contig && (tp != rp)){
      ptrdiff_t iter = 0;
      ptrdiff_t r_Size = THTensor_(nElement)(r_);
      int r_Dim = THTensor_nDimensionLegacyAll(r_);
      #pragma omp parallel for if ( r_Size > HYPER_TH_OMP_OVERHEAD_THRESHOLD)
      for (iter = 0; iter < r_Size; iter++) {
        int j;
        int64_t quot;
        int64_t rem = iter;
        ptrdiff_t tBasicIndex = 0;

        for(j = 0; j < r_Dim; ++j) {
          if(j != dimension){
            quot = rem/r_->stride(j);
            rem = rem%r_->stride(j);
            tBasicIndex += quot*t->stride(j);
          }
        }
        real *t_data = tp+tBasicIndex;
        real *r__data = rp+iter;
        *r__data = 0;
        for(j=0; j < t->size(dimension); ++j) {
          *r__data += *(t_data + j*t->stride(dimension));
        }
      }
    } else {
      serial_path = 1;
    }
  }
#else
  serial_path = 1;
#endif
  if (serial_path) {
    // two implementations optimized for data locality
    if (t->stride(dimension) == 1) {
      TH_TENSOR_DIM_APPLY2(real, t, real, r_, dimension,
                           accreal sum = 0;
                           int64_t i;
                           for(i = 0; i < t_size; i++)
                             sum += t_data[i*t_stride];
                           *r__data = (real)sum;);
    } else {
      THTensor_(zero)(r_);
      THTensor *temp_ = THTensor_(newWithTensor)(r_);
      // r_.expand_as(t)
      THTensor_setSizeAtDim(temp_, dimension, t->size(dimension));
      THTensor_setStrideAtDim(temp_, dimension, 0);

      TH_TENSOR_APPLY2(real, temp_, real, t, *temp__data = *temp__data + *t_data;);
      THTensor_(free)(temp_);
    }
  }

  if (!keepdim) {
    THTensor_(squeeze1d)(r_, r_, dimension);
  }
}

void THTensor_(prod)(THTensor *r_, THTensor *t, int dimension, int keepdim)
{
  THLongStorage *dim;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 2, "dimension %d out of range",
      dimension + TH_INDEX_BASE);

  THTensor_(preserveReduceDimSemantics)(r_, THTensor_(nDimensionLegacyAll)(t), dimension, keepdim);
  dim = THTensor_(newSizeOf)(t);
  THLongStorage_set(dim, dimension, 1);
  THTensor_(resize)(r_, dim, NULL);
  THLongStorage_free(dim);

  int serial_path = 0;
#ifdef _OPENMP
  int inOMP = omp_in_parallel();
  if (inOMP) {
    serial_path = 1;
  } else {
    int r_Contig = THTensor_(isContiguous)(r_);
    real *tp = THTensor_(data)(t);
    real *rp = THTensor_(data)(r_);
    if(r_Contig && (tp != rp)){
      ptrdiff_t iter = 0;
      ptrdiff_t r_Size = THTensor_(nElement)(r_);
      int r_Dim = THTensor_nDimensionLegacyAll(r_);
      #pragma omp parallel for if ( r_Size > HYPER_TH_OMP_OVERHEAD_THRESHOLD)
      for (iter = 0; iter < r_Size; iter++) {
        int j;
        int64_t quot;
        int64_t rem = iter;
        ptrdiff_t tBasicIndex = 0;

        for(j = 0; j < r_Dim; ++j) {
          if(j != dimension){
            quot = rem/r_->stride(j);
            rem = rem%r_->stride(j);
            tBasicIndex += quot*t->stride(j);
          }
        }
        real *t_data = tp+tBasicIndex;
        real *r__data = rp+iter;
        *r__data = 1;
        for(j=0; j < t->size(dimension); ++j) {
          *r__data *= *(t_data + j*t->stride(dimension));
        }
      }
    } else {
      serial_path = 1;
    }
  }
#else
  serial_path = 1;
#endif

  if(serial_path) {
    // two implementations optimized for data locality
    if (t->stride(dimension) == 1) {
      TH_TENSOR_DIM_APPLY2(real, t, real, r_, dimension,
                           accreal prod = 1;
                           int64_t i;
                           for(i = 0; i < t_size; i++)
                             prod *= t_data[i*t_stride];
                           *r__data = (real)prod;);
    } else {
      THTensor_(fill)(r_, 1);
      THTensor *temp_ = THTensor_(newWithTensor)(r_);
      // r_.expand_as(t)
      THTensor_setSizeAtDim(temp_, dimension, t->size(dimension));
      THTensor_setStrideAtDim(temp_, dimension, 0);

      TH_TENSOR_APPLY2(real, temp_, real, t, *temp__data = *temp__data * *t_data;);
      THTensor_(free)(temp_);
    }
  }
  if (!keepdim) {
    THTensor_(squeeze1d)(r_, r_, dimension);
  }
}

void THTensor_(cumsum)(THTensor *r_, THTensor *t, int dimension)
{
  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyNoScalars)(t), 2, "dimension %d out of range",
      dimension + TH_INDEX_BASE);

  THTensor_(resizeAs)(r_, t);

  TH_TENSOR_DIM_APPLY2(real, t, real, r_, dimension,
                       accreal cumsum = 0;
                       int64_t i;
                       for(i = 0; i < t_size; i++)
                       {
                         cumsum += t_data[i*t_stride];
                         r__data[i*r__stride] = (real)cumsum;
                       });
}

void THTensor_(cumprod)(THTensor *r_, THTensor *t, int dimension)
{
  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyNoScalars)(t), 2, "dimension %d out of range",
      dimension + TH_INDEX_BASE);

  THTensor_(resizeAs)(r_, t);

  TH_TENSOR_DIM_APPLY2(real, t, real, r_, dimension,
                       accreal cumprod = 1;
                       int64_t i;
                       for(i = 0; i < t_size; i++)
                       {
                         cumprod *= t_data[i*t_stride];
                         r__data[i*r__stride] = (real)cumprod;
                       });
}


void THTensor_(sign)(THTensor *r_, THTensor *t)
{
  THTensor_(resizeAs)(r_, t);

#if defined (TH_REAL_IS_BYTE)
  TH_TENSOR_APPLY2(real, r_, real, t,
    if (*t_data > 0) *r__data = 1;
    else *r__data = 0;);
#else
  TH_TENSOR_APPLY2(real, r_, real, t,
    if (*t_data > 0) *r__data = 1;
    else if (*t_data < 0) *r__data = -1;
    else *r__data = 0;);
#endif
}


accreal THTensor_(trace)(THTensor *t)
{
  real *t_data = THTensor_(data)(t);
  accreal sum = 0;
  int64_t i = 0;
  int64_t t_stride_0, t_stride_1, t_diag_size;

  THArgCheck(THTensor_(nDimensionLegacyAll)(t) == 2, 1, "expected a matrix");

  t_stride_0 = THTensor_(stride)(t, 0);
  t_stride_1 = THTensor_(stride)(t, 1);
  t_diag_size = THMin(THTensor_(size)(t, 0), THTensor_(size)(t, 1));
  while(i < t_diag_size)
  {
    sum += t_data[i*(t_stride_0+t_stride_1)];
    i++;
  }

  return sum;
}

void THTensor_(cross)(THTensor *r_, THTensor *a, THTensor *b, int dimension)
{
  int i;

  if(THTensor_(nDimensionLegacyNoScalars)(a) != THTensor_(nDimensionLegacyNoScalars)(b))
    THError("inconsistent tensor dimension %dD, %dD",
        THTensor_(nDimensionLegacyNoScalars)(a), THTensor_(nDimensionLegacyNoScalars)(b));

  for(i = 0; i < THTensor_(nDimensionLegacyNoScalars)(a); i++)
  {
    if(THTensor_(size)(a, i) != THTensor_(size)(b, i)) {
        THDescBuff ba = THTensor_(sizeDesc)(a);
        THDescBuff bb = THTensor_(sizeDesc)(b);
        THError("inconsistent tensor sizes %s, %s", ba.str, bb.str);
    }
  }

  if(dimension < 0)
  {
    for(i = 0; i < THTensor_(nDimensionLegacyNoScalars)(a); i++)
    {
      if(THTensor_(size)(a, i) == 3)
      {
        dimension = i;
        break;
      }
    }
    if(dimension < 0) {
      THDescBuff ba = THTensor_(sizeDesc)(a);
      THError("no dimension of size 3 in a: %s", ba.str);
    }
  }

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyNoScalars)(a), 3, "dimension %d out of range",
      dimension + TH_INDEX_BASE);
  THArgCheck(THTensor_(size)(a, dimension) == 3, 3, "dimension %d does not have size 3",
      dimension + TH_INDEX_BASE);

  THTensor_(resizeAs)(r_, a);

  TH_TENSOR_DIM_APPLY3(real, a, real, b, real, r_, dimension,
                       TH_TENSOR_DIM_APPLY3_SIZE_EQ_EXCEPT_DIM,
                       r__data[0*r__stride] = a_data[1*a_stride]*b_data[2*b_stride] - a_data[2*a_stride]*b_data[1*b_stride];
                       r__data[1*r__stride] = a_data[2*a_stride]*b_data[0*b_stride] - a_data[0*a_stride]*b_data[2*b_stride];
                       r__data[2*r__stride] = a_data[0*a_stride]*b_data[1*b_stride] - a_data[1*a_stride]*b_data[0*b_stride];);
}

void THTensor_(cmax)(THTensor *r, THTensor *t, THTensor *src) {
  THTensor_(resizeAs)(r, t);
  TH_TENSOR_APPLY3(real, r, real, t, real, src,
                   *r_data = *t_data > *src_data ? *t_data : *src_data;);
}

void THTensor_(cmin)(THTensor *r, THTensor *t, THTensor *src) {
  THTensor_(resizeAs)(r, t);
  TH_TENSOR_APPLY3(real, r, real, t, real, src,
                   *r_data = *t_data < *src_data ? *t_data : *src_data;);
}

void THTensor_(cmaxValue)(THTensor *r, THTensor *t, real value) {
  THTensor_(resizeAs)(r, t);
  TH_TENSOR_APPLY2(real, r, real, t,
                   *r_data = *t_data < value ? value : *t_data;);  // this order propagates NaN
}

void THTensor_(cminValue)(THTensor *r, THTensor *t, real value) {
  THTensor_(resizeAs)(r, t);
  TH_TENSOR_APPLY2(real, r, real, t,
                   *r_data = *t_data > value ? value : *t_data;);  // this order propagates NaN
}

void THTensor_(zerosLike)(THTensor *r_, THTensor *input)
{
  THTensor_(resizeAs)(r_, input);
  THTensor_(zero)(r_);
}

void THTensor_(onesLike)(THTensor *r_, THTensor *input)
{
  THTensor_(resizeAs)(r_, input);
  THTensor_(fill)(r_, 1);
}

void THTensor_(diag)(THTensor *r_, THTensor *t, int k)
{
#ifndef USE_TH_SIZE_ZERO_DIM
  AT_ASSERT(!t->is_empty())
#endif
  THArgCheck(THTensor_(nDimensionLegacyNoScalars)(t) == 1 || THTensor_(nDimensionLegacyNoScalars)(t) == 2, 1, "matrix or a vector expected");

  if(THTensor_(nDimensionLegacyNoScalars)(t) == 1)
  {
    real *t_data = THTensor_(data)(t);
    int64_t t_stride_0 = THTensor_(stride)(t, 0);
    int64_t t_size = THTensor_(size)(t, 0);
    int64_t sz = t_size + (k >= 0 ? k : -k);
    real *r__data;
    int64_t r__stride_0;
    int64_t r__stride_1;
    int64_t i;

    THTensor_(resize2d)(r_, sz, sz);
    THTensor_(zero)(r_);
    r__data = THTensor_(data)(r_);
    r__stride_0 = THTensor_(stride)(r_, 0);
    r__stride_1 = THTensor_(stride)(r_, 1);
    r__data += (k >= 0 ? k*r__stride_1 : -k*r__stride_0);

    for(i = 0; i < t_size; i++)
      r__data[i*(r__stride_0+r__stride_1)] = t_data[i*t_stride_0];
  }
  else
  {
    real *t_data = THTensor_(data)(t);
    int64_t t_stride_0 = THTensor_(stride)(t, 0);
    int64_t t_stride_1 = THTensor_(stride)(t, 1);
    int64_t sz;
    real *r__data;
    int64_t r__stride_0;
    int64_t i;

    if(k >= 0)
      sz = THMin(THTensor_(size)(t, 0), THTensor_(size)(t, 1)-k);
    else
      sz = THMin(THTensor_(size)(t, 0)+k, THTensor_(size)(t, 1));
    THTensor_(resize1d)(r_, sz);
    r__data = THTensor_(data)(r_);
    r__stride_0 = THTensor_(stride)(r_, 0);

    t_data += (k >= 0 ? k*t_stride_1 : -k*t_stride_0);
    for(i = 0; i < sz; i++)
      r__data[i*r__stride_0] = t_data[i*(t_stride_0+t_stride_1)];
  }
}

void THTensor_(eye)(THTensor *r_, int64_t n, int64_t m)
{
  real *r__data;
  int64_t i, sz;

  THArgCheck(n > 0, 1, "invalid argument");

  if(m <= 0)
    m = n;

  THTensor_(resize2d)(r_, n, m);
  THTensor_(zero)(r_);

  i = 0;
  r__data = THTensor_(data)(r_);
  sz = THMin(THTensor_(size)(r_, 0), THTensor_(size)(r_, 1));
  for(i = 0; i < sz; i++)
    r__data[i*(r_->stride(0)+r_->stride(1))] = 1;
}


void THTensor_(range)(THTensor *r_, accreal xmin, accreal xmax, accreal step)
{
  ptrdiff_t size;
  real i = 0;

  THArgCheck(step > 0 || step < 0, 3, "step must be nonzero");
  THArgCheck(((step > 0) && (xmax >= xmin)) || ((step < 0) && (xmax <= xmin))
              , 2, "upper bound and larger bound inconsistent with step sign");

  size = (ptrdiff_t) (((xmax - xmin) / step) + 1);

  if (THTensor_(nElement)(r_) != size) {
    THTensor_(resize1d)(r_, size);
  }

  TH_TENSOR_APPLY(real, r_, *r__data = xmin + (i++)*step;);
}

void THTensor_(arange)(THTensor *r_, accreal xmin, accreal xmax, accreal step) {
  ptrdiff_t size;
  real i = 0;

  THArgCheck(step > 0 || step < 0, 3, "step must be nonzero");
  THArgCheck(((step > 0) && (xmax >= xmin)) || ((step < 0) && (xmax <= xmin))
              , 2, "upper bound and larger bound inconsistent with step sign");

  size = (ptrdiff_t) ceil((double)(xmax - xmin) / step);

  if (THTensor_(nElement)(r_) != size) {
    THTensor_(resize1d)(r_, size);
  }

  TH_TENSOR_APPLY(real, r_, *r__data = xmin + (i++)*step;);
}

void THTensor_(randperm)(THTensor *r_, THGenerator *_generator, int64_t n)
{
  real *r__data;
  int64_t r__stride_0;
  int64_t i;

  THArgCheck(n > 0, 1, "must be strictly positive");

  THTensor_(resize1d)(r_, n);
  r__data = THTensor_(data)(r_);
  r__stride_0 = THTensor_(stride)(r_,0);

  for(i = 0; i < n; i++)
    r__data[i*r__stride_0] = (real)(i);

  for(i = 0; i < n-1; i++)
  {
    int64_t z = THRandom_random(_generator) % (n-i);
    real sav = r__data[i*r__stride_0];
    r__data[i*r__stride_0] = r__data[(z+i)*r__stride_0];
    r__data[(z+i)*r__stride_0] = sav;
  }
}

/* I cut and pasted (slightly adapted) the quicksort code from
   Sedgewick's 1978 "Implementing Quicksort Programs" article
   http://www.csie.ntu.edu.tw/~b93076/p847-sedgewick.pdf

   It is the state of the art existing implementation. The macros
   are here to make as close a match as possible to the pseudocode of
   Program 2 p.851

   Note that other partition schemes exist, and are typically presented
   in textbook, but those are less efficient. See e.g.
   http://cs.stackexchange.com/questions/11458/quicksort-partitioning-hoare-vs-lomuto

   Julien, November 12th 2013
*/
#define MAX_LEVELS  300
#define M_SMALL 10 /* Limit for small subfiles */

#define ARR(III) arr[(III)*stride]
#define IDX(III) idx[(III)*stride]

#define LONG_SWAP(AAA, BBB) swap = AAA; AAA = BBB; BBB = swap
#define REAL_SWAP(AAA, BBB) rswap = AAA; AAA = BBB; BBB = rswap

#define ARR_SWAP(III, JJJ) \
  REAL_SWAP(ARR(III), ARR(JJJ));

#define BOTH_SWAP(III, JJJ) \
  REAL_SWAP(ARR(III), ARR(JJJ)); \
  LONG_SWAP(IDX(III), IDX(JJJ))

static void THTensor_(quicksortascend)(real *arr, int64_t *idx, int64_t elements, int64_t stride)
{
  int64_t beg[MAX_LEVELS], end[MAX_LEVELS], i, j, L, R, P, swap, pid, stack = 0, sz_right, sz_left;
  real rswap, piv;
  unsigned char done = 0;

  /* beg[0]=0; end[0]=elements; */
  stack = 0;
  L = 0; R = elements-1;
  done = elements-1 <= M_SMALL;

  while(!done) {
      /* Use median of three for pivot choice */
    P=(L+R)>>1;
    BOTH_SWAP(P, L+1);
    if (ARR(L+1) > ARR(R)) { BOTH_SWAP(L+1, R); }
    if (ARR(L) > ARR(R)) { BOTH_SWAP(L, R); }
    if (ARR(L+1) > ARR(L)) { BOTH_SWAP(L+1, L); }

    i = L+1; j = R; piv = ARR(L); pid = IDX(L);

    do {
      do { i = i+1; } while(ARR(i) < piv);
      do { j = j-1; } while(ARR(j) > piv);
      if (j < i)
          break;
      BOTH_SWAP(i, j);
    } while(1);
    BOTH_SWAP(L, j);
    /* Left subfile is (L, j-1) */
    /* Right subfile is (i, R) */
    sz_left = j-L;
    sz_right = R-i+1;
    if (sz_left <= M_SMALL && sz_right <= M_SMALL) {
      /* both subfiles are small */
      /* if stack empty */
      if (stack == 0) {
        done = 1;
      } else {
        stack--;
        L = beg[stack];
        R = end[stack];
      }
    } else if (sz_left <= M_SMALL || sz_right <= M_SMALL) {
      /* exactly one of the subfiles is small */
      /* (L,R) = large subfile */
      if (sz_left > sz_right) {
        /* Implicit: L = L; */
        R = j-1;
      } else {
        L = i;
        /* Implicit: R = R; */
      }
    } else {
      /* none of the subfiles is small */
      /* push large subfile */
      /* (L,R) = small subfile */
      if (sz_left > sz_right) {
        beg[stack] = L;
        end[stack] = j-1;
        stack++;
        L = i;
        /* Implicit: R = R */
      } else {
        beg[stack] = i;
        end[stack] = R;
        stack++;
        /* Implicit: L = L; */
        R = j-1;
      }
    }
  } /* while not done */
  /* Now insertion sort on the concatenation of subfiles */
  for(i=elements-2; i>=0; i--) {
    if (ARR(i) > ARR(i+1)) {
      piv = ARR(i);
      pid = IDX(i);
      j = i+1;
      do {
        ARR(j-1) = ARR(j);
        IDX(j-1) = IDX(j);
        j = j+1;
      } while(j < elements && ARR(j) < piv);
      ARR(j-1) = piv;
      IDX(j-1) = pid;
     }
  }
}

static void THTensor_(quicksortdescend)(real *arr, int64_t *idx, int64_t elements, int64_t stride)
{
  int64_t beg[MAX_LEVELS], end[MAX_LEVELS], i, j, L, R, P, swap, pid, stack = 0, sz_right, sz_left;
  real rswap, piv;
  unsigned char done = 0;

  /* beg[0]=0; end[0]=elements; */
  stack = 0;
  L = 0; R = elements-1;
  done = elements-1 <= M_SMALL;

  while(!done) {
      /* Use median of three for pivot choice */
    P=(L+R)>>1;
    BOTH_SWAP(P, L+1);
    if (ARR(L+1) < ARR(R)) { BOTH_SWAP(L+1, R); }
    if (ARR(L) < ARR(R)) { BOTH_SWAP(L, R); }
    if (ARR(L+1) < ARR(L)) { BOTH_SWAP(L+1, L); }

    i = L+1; j = R; piv = ARR(L); pid = IDX(L);

    do {
      do { i = i+1; } while(ARR(i) > piv);
      do { j = j-1; } while(ARR(j) < piv);
      if (j < i)
          break;
      BOTH_SWAP(i, j);
    } while(1);
    BOTH_SWAP(L, j);
    /* Left subfile is (L, j-1) */
    /* Right subfile is (i, R) */
    sz_left = j-L;
    sz_right = R-i+1;
    if (sz_left <= M_SMALL && sz_right <= M_SMALL) {
      /* both subfiles are small */
      /* if stack empty */
      if (stack == 0) {
        done = 1;
      } else {
        stack--;
        L = beg[stack];
        R = end[stack];
      }
    } else if (sz_left <= M_SMALL || sz_right <= M_SMALL) {
      /* exactly one of the subfiles is small */
      /* (L,R) = large subfile */
      if (sz_left > sz_right) {
        /* Implicit: L = L; */
        R = j-1;
      } else {
        L = i;
        /* Implicit: R = R; */
      }
    } else {
      /* none of the subfiles is small */
      /* push large subfile */
      /* (L,R) = small subfile */
      if (sz_left > sz_right) {
        beg[stack] = L;
        end[stack] = j-1;
        stack++;
        L = i;
        /* Implicit: R = R */
      } else {
        beg[stack] = i;
        end[stack] = R;
        stack++;
        /* Implicit: L = L; */
        R = j-1;
      }
    }
  } /* while not done */
  /* Now insertion sort on the concatenation of subfiles */
  for(i=elements-2; i>=0; i--) {
    if (ARR(i) < ARR(i+1)) {
      piv = ARR(i);
      pid = IDX(i);
      j = i+1;
      do {
        ARR(j-1) = ARR(j);
        IDX(j-1) = IDX(j);
        j = j+1;
      } while(j < elements && ARR(j) > piv);
      ARR(j-1) = piv;
      IDX(j-1) = pid;
     }
  }
}

#undef MAX_LEVELS
#undef M_SMALL

void THTensor_(sort)(THTensor *rt_, THLongTensor *ri_, THTensor *t, int dimension, int descendingOrder)
{
  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyNoScalars)(t), 2, "invalid dimension %d",
      dimension + TH_INDEX_BASE);

  THTensor_(resizeAs)(rt_, t);
  THTensor_(copy)(rt_, t);

  {
    THLongStorage *size = THTensor_(newSizeOf)(t);
    THLongTensor_resize(ri_, size, NULL);
    THLongStorage_free(size);
  }

  if(descendingOrder)
  {
    TH_TENSOR_DIM_APPLY2(real, rt_, int64_t, ri_, dimension,
                         int64_t i;
                         for(i = 0; i < ri__size; i++)
                           ri__data[i*ri__stride] = i;
                         THTensor_(quicksortdescend)(rt__data, ri__data, rt__size, rt__stride);)
      }
  else
  {
    TH_TENSOR_DIM_APPLY2(real, rt_, int64_t, ri_, dimension,
                         int64_t i;
                         for(i = 0; i < ri__size; i++)
                           ri__data[i*ri__stride] = i;
                         THTensor_(quicksortascend)(rt__data, ri__data, rt__size, rt__stride);)
      }
}

/* Implementation of the Quickselect algorithm, based on Nicolas Devillard's
public domain implementation at http://ndevilla.free.fr/median/median/
Adapted similarly to the above Quicksort algorithm.
This version does not produce indices along with values. */
static void THTensor_(quickselectnoidx)(real *arr, int64_t k, int64_t elements, int64_t stride)
{
  int64_t P, L, R, i, j;
  real rswap, piv;
  L = 0;
  R = elements-1;

  do {
    if (R <= L) /* One element only */
      return;

    if (R == L+1) {  /* Two elements only */
      if (ARR(L) > ARR(R)) {
        ARR_SWAP(L, R);
      }
      return;
    }

    /* Use median of three for pivot choice */
    P=(L+R)>>1;
    ARR_SWAP(P, L+1);
    if (ARR(L+1) > ARR(R)) { ARR_SWAP(L+1, R); }
    if (ARR(L) > ARR(R)) { ARR_SWAP(L, R); }
    if (ARR(L+1) > ARR(L)) { ARR_SWAP(L+1, L); }

    i = L+1;
    j = R;
    piv = ARR(L);
    do {
      do i++; while(ARR(i) < piv);
      do j--; while(ARR(j) > piv);
      if (j < i)
        break;
      ARR_SWAP(i, j);
    } while(1);
    ARR_SWAP(L, j);

    /* Re-set active partition */
    if (j <= k) L=i;
    if (j >= k) R=j-1;
  } while(1);
}

/* Implementation of the Quickselect algorithm, based on Nicolas Devillard's
public domain implementation at http://ndevilla.free.fr/median/median/
Adapted similarly to the above Quicksort algorithm. */
static void THTensor_(quickselect)(real *arr, int64_t *idx, int64_t k, int64_t elements, int64_t stride)
{
  int64_t P, L, R, i, j, swap;
  real rswap, piv;
  L = 0;
  R = elements-1;

  do {
    if (R <= L) /* One element only */
      return;

    if (R == L+1) {  /* Two elements only */
      if (ARR(L) > ARR(R)) {
        BOTH_SWAP(L, R);
      }
      return;
    }

    /* Use median of three for pivot choice */
    P=(L+R)>>1;
    BOTH_SWAP(P, L+1);
    if (ARR(L+1) > ARR(R)) { BOTH_SWAP(L+1, R); }
    if (ARR(L) > ARR(R)) { BOTH_SWAP(L, R); }
    if (ARR(L+1) > ARR(L)) { BOTH_SWAP(L+1, L); }

    i = L+1;
    j = R;
    piv = ARR(L);
    do {
      do i++; while(ARR(i) < piv);
      do j--; while(ARR(j) > piv);
      if (j < i)
        break;
      BOTH_SWAP(i, j);
    } while(1);
    BOTH_SWAP(L, j);

    /* Re-set active partition */
    if (j <= k) L=i;
    if (j >= k) R=j-1;
  } while(1);
}

#undef ARR
#undef IDX
#undef LONG_SWAP
#undef REAL_SWAP
#undef BOTH_SWAP

real THTensor_(medianall)(THTensor *tensor)
{
  THArgCheck(THTensor_nDimensionLegacyAll(tensor) > 0, 1, "tensor must have one dimension");

  real theMedian;
  ptrdiff_t numel;
  int64_t k;
  THTensor *temp_;
  real *temp__data;

  numel = THTensor_(nElement)(tensor);
  k = (numel-1) >> 1;

  temp_ = THTensor_(newClone)(tensor);
  temp__data = THTensor_(data)(temp_);

  THTensor_(quickselectnoidx)(temp__data, k, numel, 1);

  theMedian = temp__data[k];

  THTensor_(free)(temp_);

  return theMedian;
}

void THTensor_(mode)(THTensor *values_, THLongTensor *indices_, THTensor *t, int dimension, int keepdim)
{
  THLongStorage *dim;
  THTensor *temp_;
  THLongTensor *tempi_;
  real *temp__data;
  int64_t *tempi__data;
  int64_t t_size_dim;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 3, "dimension out of range");

  int in_dims = THTensor_(nDimensionLegacyAll)(t);
  THTensor_(preserveReduceDimSemantics)(values_, in_dims, dimension, keepdim);
  THLongTensor_preserveReduceDimSemantics(indices_, in_dims, dimension, keepdim);
  dim = THTensor_(newSizeOf)(t);
  THLongStorage_set(dim, dimension, 1);
  THTensor_(resize)(values_, dim, NULL);
  THLongTensor_resize(indices_, dim, NULL);
  THLongStorage_free(dim);

  t_size_dim = THTensor_(size)(t, dimension);

  temp_ = THTensor_(new)();
  THTensor_(resize1d)(temp_, t_size_dim);
  temp__data = THTensor_(data)(temp_);

  tempi_ = THLongTensor_new();
  THLongTensor_resize1d(tempi_, t_size_dim);
  tempi__data = THLongTensor_data(tempi_);

  TH_TENSOR_DIM_APPLY3(real, t, real, values_, int64_t, indices_, dimension,
                       TH_TENSOR_DIM_APPLY3_SIZE_EQ_EXCEPT_DIM,
                       int64_t i;
                       real mode = 0;
                       int64_t modei = 0;
                       int64_t temp_freq = 0;
                       int64_t max_freq = 0;
                       for(i = 0; i < t_size_dim; i++)
                          temp__data[i] = t_data[i*t_stride];
                       for(i = 0; i < t_size_dim; i++)
                          tempi__data[i] = i;
                       THTensor_(quicksortascend)(temp__data, tempi__data, t_size_dim, 1);

                       for(i = 0; i < t_size_dim; i++)
                       {
                          temp_freq++;
                          if ((i == t_size_dim - 1) || (temp__data[i] != temp__data[i+1]))
                          {
                              if (temp_freq > max_freq)
                              {
                                 mode = temp__data[i];
                                 modei = tempi__data[i];
                                 max_freq = temp_freq;
                              }
                              temp_freq = 0;
                          }
                       }
                       *values__data = mode;
                       *indices__data = modei;);

  THTensor_(free)(temp_);
  THLongTensor_free(tempi_);
  if (!keepdim) {
    THTensor_(squeeze1d)(values_, values_, dimension);
    THLongTensor_squeeze1d(indices_, indices_, dimension);
  }
}

void THTensor_(kthvalue)(THTensor *values_, THLongTensor *indices_, THTensor *t, int64_t k, int dimension, int keepdim)
{
  THLongStorage *dim;
  THTensor *temp_;
  THLongTensor *tempi_;
  real *temp__data;
  int64_t *tempi__data;
  int64_t t_size_dim;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 3, "dimension out of range");
  THArgCheck(k > 0 && k <= t->size(dimension), 2, "selected index out of range");

  int in_dims = THTensor_(nDimensionLegacyAll)(t);
  THTensor_(preserveReduceDimSemantics)(values_, in_dims, dimension, keepdim);
  THLongTensor_preserveReduceDimSemantics(indices_, in_dims, dimension, keepdim);
  dim = THTensor_(newSizeOf)(t);
  THLongStorage_set(dim, dimension, 1);
  THTensor_(resize)(values_, dim, NULL);
  THLongTensor_resize(indices_, dim, NULL);
  THLongStorage_free(dim);

  t_size_dim = THTensor_(size)(t, dimension);

  temp_ = THTensor_(new)();
  THTensor_(resize1d)(temp_, t_size_dim);
  temp__data = THTensor_(data)(temp_);

  tempi_ = THLongTensor_new();
  THLongTensor_resize1d(tempi_, t_size_dim);
  tempi__data = THLongTensor_data(tempi_);

  TH_TENSOR_DIM_APPLY3(real, t, real, values_, int64_t, indices_, dimension,
                       TH_TENSOR_DIM_APPLY3_SIZE_EQ_EXCEPT_DIM,
                       int64_t i;
                       for(i = 0; i < t_size_dim; i++)
                          temp__data[i] = t_data[i*t_stride];
                       for(i = 0; i < t_size_dim; i++)
                          tempi__data[i] = i;
                       THTensor_(quickselect)(temp__data, tempi__data, k - 1, t_size_dim, 1);
                       *values__data = temp__data[k-1];
                       *indices__data = tempi__data[k-1];);

  THTensor_(free)(temp_);
  THLongTensor_free(tempi_);
  if (!keepdim) {
    THTensor_(squeeze1d)(values_, values_, dimension);
    THLongTensor_squeeze1d(indices_, indices_, dimension);
  }
}

void THTensor_(median)(THTensor *values_, THLongTensor *indices_, THTensor *t, int dimension, int keepdim)
{
  int64_t t_size_dim, k;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 3, "dimension out of range");

  t_size_dim = THTensor_(size)(t, dimension);
  k = (t_size_dim-1) >> 1; /* take middle or one-before-middle element */

  THTensor_(kthvalue)(values_, indices_, t, k+1, dimension, keepdim);
}

void THTensor_(topk)(THTensor *rt_, THLongTensor *ri_, THTensor *t, int64_t k, int dim, int dir, int sorted)
{
#ifndef USE_TH_SIZE_ZERO_DIM
  int numDims = THTensor_(nDimensionLegacyAll)(t);
#else
  int numDims = THTensor_(nDimensionLegacyNoScalars)(t);
#endif
  THArgCheck(dim >= 0 && dim < numDims, 3, "dim not in range");

  int64_t sliceSize = THTensor_(size)(t, dim);
#ifndef USE_TH_SIZE_ZERO_DIM
  THArgCheck(k > 0 && k <= sliceSize, 2, "k not in range for dimension");
#else
  THArgCheck(k >= 0 && k <= sliceSize, 2, "k not in range for dimension");
#endif

  THTensor *tmpResults = THTensor_(new)();
  THTensor_(resize1d)(tmpResults, sliceSize);
  real *tmp__data = THTensor_(data)(tmpResults);

  THLongTensor *tmpIndices = THLongTensor_new();
  THLongTensor_resize1d(tmpIndices, sliceSize);
  int64_t *tmpi__data = THLongTensor_data(tmpIndices);

  THLongStorage *topKSize = THTensor_(newSizeOf)(t);
  THLongStorage_set(topKSize, dim, k);
  THTensor_(resize)(rt_, topKSize, NULL);
  THLongTensor_resize(ri_, topKSize, NULL);
  THLongStorage_free(topKSize);

  if (dir) {
    /* k largest elements, descending order (optional: see sorted) */
    int64_t K = sliceSize - k;
    TH_TENSOR_DIM_APPLY3(real, t, real, rt_, int64_t, ri_, dim,
                         TH_TENSOR_DIM_APPLY3_SIZE_EQ_EXCEPT_DIM,
                         int64_t i;
                         for(i = 0; i < sliceSize; i++)
                         {
                           tmp__data[i] = t_data[i*t_stride];
                           tmpi__data[i] = i;
                         }
                         if (K > 0)
                           THTensor_(quickselect)(tmp__data, tmpi__data, K - 1, sliceSize, 1);
                         if (sorted)
                           THTensor_(quicksortdescend)(tmp__data + K, tmpi__data + K, k, 1);
                         for(i = 0; i < k; i++)
                         {
                           rt__data[i*rt__stride] = tmp__data[i + K];
                           ri__data[i*ri__stride] = tmpi__data[i + K];
                         })
  }
  else {
    /* k smallest elements, ascending order (optional: see sorted) */
    TH_TENSOR_DIM_APPLY3(real, t, real, rt_, int64_t, ri_, dim,
                         TH_TENSOR_DIM_APPLY3_SIZE_EQ_EXCEPT_DIM,
                         int64_t i;
                         for(i = 0; i < sliceSize; i++)
                         {
                           tmp__data[i] = t_data[i*t_stride];
                           tmpi__data[i] = i;
                         }
                         THTensor_(quickselect)(tmp__data, tmpi__data, k - 1, sliceSize, 1);
                         if (sorted)
                           THTensor_(quicksortascend)(tmp__data, tmpi__data, k - 1, 1);
                         for(i = 0; i < k; i++)
                         {
                           rt__data[i*rt__stride] = tmp__data[i];
                           ri__data[i*ri__stride] = tmpi__data[i];
                         })
  }

  THTensor_(free)(tmpResults);
  THLongTensor_free(tmpIndices);
}

void THTensor_(tril)(THTensor *r_, THTensor *t, int64_t k)
{
  int64_t t_size_0, t_size_1;
  int64_t t_stride_0, t_stride_1;
  int64_t r__stride_0, r__stride_1;
  real *t_data, *r__data;
  int64_t r, c;

  THArgCheck(THTensor_(nDimensionLegacyAll)(t) == 2, 1, "expected a matrix");

  THTensor_(resizeAs)(r_, t);

  t_size_0 = THTensor_(size)(t, 0);
  t_size_1 = THTensor_(size)(t, 1);
  t_stride_0 = THTensor_(stride)(t, 0);
  t_stride_1 = THTensor_(stride)(t, 1);
  r__stride_0 = THTensor_(stride)(r_, 0);
  r__stride_1 = THTensor_(stride)(r_, 1);
  r__data = THTensor_(data)(r_);
  t_data = THTensor_(data)(t);

  for(r = 0; r < t_size_0; r++)
  {
    int64_t sz = THMin(r+k+1, t_size_1);
    for(c = THMax(0, r+k+1); c < t_size_1; c++)
      r__data[r*r__stride_0+c*r__stride_1] = 0;
    for(c = 0; c < sz; c++)
      r__data[r*r__stride_0+c*r__stride_1] = t_data[r*t_stride_0+c*t_stride_1];
  }
}

void THTensor_(triu)(THTensor *r_, THTensor *t, int64_t k)
{
  int64_t t_size_0, t_size_1;
  int64_t t_stride_0, t_stride_1;
  int64_t r__stride_0, r__stride_1;
  real *t_data, *r__data;
  int64_t r, c;

  THArgCheck(THTensor_(nDimensionLegacyAll)(t) == 2, 1, "expected a matrix");

  THTensor_(resizeAs)(r_, t);

  t_size_0 = THTensor_(size)(t, 0);
  t_size_1 = THTensor_(size)(t, 1);
  t_stride_0 = THTensor_(stride)(t, 0);
  t_stride_1 = THTensor_(stride)(t, 1);
  r__stride_0 = THTensor_(stride)(r_, 0);
  r__stride_1 = THTensor_(stride)(r_, 1);
  r__data = THTensor_(data)(r_);
  t_data = THTensor_(data)(t);

  for(r = 0; r < t_size_0; r++)
  {
    int64_t sz = THMin(r+k, t_size_1);
    for(c = THMax(0, r+k); c < t_size_1; c++)
      r__data[r*r__stride_0+c*r__stride_1] = t_data[r*t_stride_0+c*t_stride_1];
    for(c = 0; c < sz; c++)
      r__data[r*r__stride_0+c*r__stride_1] = 0;
  }
}

void THTensor_(cat)(THTensor *r_, THTensor *ta, THTensor *tb, int dimension)
{
  THTensor* inputs[2];
  inputs[0] = ta;
  inputs[1] = tb;
  THTensor_(catArray)(r_, inputs, 2, dimension);
}

void THTensor_(check_shape_except_dim)(THTensor *first, THTensor *second, int dimension);
inline void THTensor_(check_shape_except_dim)(THTensor *first, THTensor *second, int dimension)
{
  int first_dims = first->dim();
  int second_dims = second->dim();
  THArgCheck(first_dims == second_dims, 0,
      "Tensors must have same number of dimensions: got %d and %d",
      first_dims, second_dims);
  for (int dim = 0; dim < first_dims; dim++) {
    if (dim == dimension) {
      continue;
    }
    int64_t first_dim_size = first->size(dim);
    int64_t second_dim_size = second->size(dim);
    THArgCheck(first_dim_size == second_dim_size, 0,
        "Sizes of tensors must match except in dimension %d. Got %lld and %lld in dimension %d",
        dimension, (long long)first_dim_size, (long long)second_dim_size, dim);
  }
}

void THTensor_(catArray)(THTensor *result, THTensor **inputs, int numInputs, int dimension)
{
  // previously, size [0] tensors were the only possible empty tensors; thus, it wasn't possible
  // to cat empty tensors unless all the other tensors were 1-dimensional, so we allowed these tensors
  // to be "skipped".  We maintain this behavior for backwards compatibility, but only for this specific
  // size (i.e. other empty sizes are not skipped).
  // FIXME: warn if this is the case
  bool allSkipped= true;
  int64_t nDims = 0;
  THTensor *notSkippedTensor;  // non-owning reference
  auto should_skip = [](THTensor *t) { return t->is_empty() && t->dim() == 1; };
  for (int i = 0; i < numInputs; i++) {
    if (should_skip(inputs[i])) {
      continue;
    }
    // We've found a non-empty tensor
    allSkipped = false;
    notSkippedTensor = inputs[i];
    nDims = notSkippedTensor->dim();
    break;
  }
  if (allSkipped) {
    return;
  }

  // Compute cat_dimension based on the non-empty tensor
  THArgCheck(dimension < nDims, 4, "invalid dimension %d", dimension);
  THArgCheck(numInputs > 0, 3, "invalid number of inputs %d", numInputs);

  // Compute size of the result in the cat dimension
  int64_t cat_dim_size = 0;
  for (int i = 0; i < numInputs; i++) {
    THTensor *tensor = inputs[i];
    if (should_skip(tensor)) {
      continue;
    }
    THTensor_(check_shape_except_dim)(notSkippedTensor, tensor, dimension);
    cat_dim_size += tensor->size(dimension);
  }

  // Compute the size of the result
  THLongStorage *size = THLongStorage_newWithSize(nDims);
  for (int dim = 0; dim < nDims; dim++) {
    int64_t result_dim_size = notSkippedTensor->size(dim);
    if (dim == dimension) {
      result_dim_size = cat_dim_size;
    }
    THLongStorage_data(size)[dim] = result_dim_size;
  }
  THTensor_(resize)(result, size, NULL);

  // Check contiguity of all inputs and result
  bool allContiguous = true;
  for (int i = 0; i < numInputs; i++) {
    if(!should_skip(inputs[i])) {
      allContiguous = allContiguous && THTensor_(isContiguous)(inputs[i]);
    }
  }
  allContiguous = allContiguous && THTensor_(isContiguous)(result);

  // First path is for contiguous inputs along dim 0
  // Second path for non-contiguous
  int64_t offset;
  if (dimension == 0 && allContiguous) {
    real* result_data = THStorage_(data)(THTensor_getStoragePtr(result)) + result->storage_offset();
    offset = 0;
    for (int j = 0; j < numInputs; j++) {
      if (!should_skip(inputs[j])) {
        THTensor* input0 = inputs[j];
        real* input0_data = THStorage_(data)(THTensor_getStoragePtr(input0)) + input0->storage_offset();
        int64_t input0_size = THTensor_(nElement)(input0);
        // C standard says you can't pass nullptrs to memcpy, even if the size is 0; ubsan checks this.
        if (input0_size != 0) {
          memcpy(result_data + offset, input0_data, input0_size*sizeof(real));
        }
        offset += input0_size;
      }
    }
  } else {
    offset = 0;
    for (int j = 0; j < numInputs; j++) {
      if (!should_skip(inputs[j])) {
        int64_t dimSize = inputs[j]->size(dimension);
        THTensor *nt = THTensor_(newWithTensor)(result);
        THTensor_(narrow)(nt, NULL, dimension, offset, dimSize);
        THTensor_(copy)(nt, inputs[j]);
        THTensor_(free)(nt);
        offset += dimSize;
      }
    }
  }
  THLongStorage_free(size);
}

int THTensor_(equal)(THTensor *ta, THTensor* tb)
{
  int equal = 1;
  if(!THTensor_(isSameSizeAs)(ta, tb))
    return 0;

  if (THTensor_(isContiguous)(ta) && THTensor_(isContiguous)(tb)) {
    real *tap = THTensor_(data)(ta);
    real *tbp = THTensor_(data)(tb);
    ptrdiff_t sz = THTensor_(nElement)(ta);
    ptrdiff_t i;
    for (i=0; i<sz; ++i){
      if(tap[i] != tbp[i]) return 0;
    }
  } else {
    // Short-circuit the apply function on inequality
    TH_TENSOR_APPLY2(real, ta, real, tb,
                     if (equal && *ta_data != *tb_data) {
                        equal = 0;
                        TH_TENSOR_APPLY_hasFinished = 1; break;
                     })
  }
  return equal;
}

#define TENSOR_IMPLEMENT_LOGICAL(NAME,OP)				\
  void THTensor_(NAME##Value)(THByteTensor *r_, THTensor* t, real value)	\
  {									\
    THByteTensor_resizeNd(r_, t->dim(), THTensor_getSizePtr(t), NULL);		\
    TH_TENSOR_APPLY2(unsigned char, r_, real, t,			\
		     *r__data = (*t_data OP value) ? 1 : 0;); \
  }									\
  void THTensor_(NAME##ValueT)(THTensor* r_, THTensor* t, real value)	\
  {									\
    THTensor_(resizeNd)(r_, t->dim(), THTensor_getSizePtr(t), NULL);		\
    TH_TENSOR_APPLY2(real, r_, real, t,					\
		     *r__data = (*t_data OP value) ? 1 : 0;); \
  }									\
  void THTensor_(NAME##Tensor)(THByteTensor *r_, THTensor *ta, THTensor *tb) \
  {									\
    THByteTensor_resizeNd(r_, ta->dim(), THTensor_getSizePtr(ta), NULL);		\
    TH_TENSOR_APPLY3(unsigned char, r_, real, ta, real, tb,		\
		     *r__data = (*ta_data OP *tb_data) ? 1 : 0;); \
  }									\
  void THTensor_(NAME##TensorT)(THTensor *r_, THTensor *ta, THTensor *tb) \
  {									\
    THTensor_(resizeNd)(r_, ta->dim(), THTensor_getSizePtr(ta), NULL);		\
    TH_TENSOR_APPLY3(real, r_, real, ta, real, tb,			\
		     *r__data = (*ta_data OP *tb_data) ? 1 : 0;); \
  }									\


TENSOR_IMPLEMENT_LOGICAL(lt,<)
TENSOR_IMPLEMENT_LOGICAL(gt,>)
TENSOR_IMPLEMENT_LOGICAL(le,<=)
TENSOR_IMPLEMENT_LOGICAL(ge,>=)
TENSOR_IMPLEMENT_LOGICAL(eq,==)
TENSOR_IMPLEMENT_LOGICAL(ne,!=)


#ifdef _OPENMP

#define LAB_IMPLEMENT_BASIC_FUNCTION_3_ARGS(NAME, CFUNC, OMP_THRESHOLD)             \
  void THTensor_(NAME)(THTensor *r_, THTensor *t)             \
  {                                                           \
    THTensor_(resizeAs)(r_, t);                               \
    ptrdiff_t r_Size = THTensor_(nElement)(r_);               \
    int r_Contig = THTensor_(isContiguous)(r_);               \
    int tContig = THTensor_(isContiguous)(t);                 \
    int inOMP = omp_in_parallel();                            \
    if( !inOMP ){   \
      TH_TENSOR_APPLY2_OMP(r_Size, r_Contig, tContig, real, r_, real, t, *r__data = CFUNC(*t_data);, OMP_THRESHOLD);        \
    } else {                                                                                                   \
      TH_TENSOR_APPLY2(real, r_, real, t, *r__data = CFUNC(*t_data););                                       \
    }                                                                                                        \
  }

#define LAB_IMPLEMENT_BASIC_FUNCTION_2_ARGS(NAME, CFUNC)      \
  LAB_IMPLEMENT_BASIC_FUNCTION_3_ARGS(NAME, CFUNC, UNCERTAIN_TH_OMP_OVERHEAD_THRESHOLD)

#define LAB_IMPLEMENT_VECTORIZED_FUNCTION_3_ARGS(NAME, CFUNC, OMP_THRESHOLD)             \
  void THTensor_(NAME)(THTensor *r_, THTensor *t)             \
  {                                                           \
    THTensor_(resizeAs)(r_, t);                               \
    ptrdiff_t r_Size = THTensor_(nElement)(r_);               \
    int r_Contig = THTensor_(isContiguous)(r_);               \
    int tContig = THTensor_(isContiguous)(t);                 \
    if (r_Contig && tContig) {                                \
      TH_TENSOR_APPLY2_CONTIG(real, r_, real, t, THVector_(NAME)(r__data, t_data, r__len););                   \
    } else {                                                                                                   \
      int inOMP = omp_in_parallel();                            \
      if( !inOMP ){   \
        TH_TENSOR_APPLY2_OMP(r_Size, r_Contig, tContig, real, r_, real, t, *r__data = CFUNC(*t_data);, OMP_THRESHOLD);        \
      }                                                                                                        \
      else {                                                                                                   \
        TH_TENSOR_APPLY2(real, r_, real, t, *r__data = CFUNC(*t_data););                                       \
      }                                                                                                        \
    }                                                                                                          \
  }

#define LAB_IMPLEMENT_VECTORIZED_FUNCTION_2_ARGS(NAME, CFUNC) \
  LAB_IMPLEMENT_VECTORIZED_FUNCTION_3_ARGS(NAME, CFUNC, UNCERTAIN_TH_OMP_OVERHEAD_THRESHOLD)

#else

#define LAB_IMPLEMENT_BASIC_FUNCTION_2_ARGS(NAME, CFUNC)             \
  void THTensor_(NAME)(THTensor *r_, THTensor *t)                \
  {                                                           \
    THTensor_(resizeAs)(r_, t);                               \
    TH_TENSOR_APPLY2(real, t, real, r_, *r__data = CFUNC(*t_data);); \
  }                                                           \

#define LAB_IMPLEMENT_BASIC_FUNCTION_3_ARGS(NAME, CFUNC, PSEUDO_OMP_THRESHOLD) \
  LAB_IMPLEMENT_BASIC_FUNCTION_2_ARGS(NAME, CFUNC)

#define LAB_IMPLEMENT_VECTORIZED_FUNCTION_2_ARGS(NAME, CFUNC)             \
  void THTensor_(NAME)(THTensor *r_, THTensor *t)                \
  {                                                           \
    THTensor_(resizeAs)(r_, t);                               \
    int r_Contig = THTensor_(isContiguous)(r_);               \
    int tContig = THTensor_(isContiguous)(t);                 \
    if (r_Contig && tContig) {                                \
      TH_TENSOR_APPLY2_CONTIG(real, r_, real, t, THVector_(NAME)(r__data, t_data, r__len);); \
    } else {                                                           \
      TH_TENSOR_APPLY2(real, t, real, r_, *r__data = CFUNC(*t_data);); \
    }                                                           \
  }                                                             \

#define LAB_IMPLEMENT_VECTORIZED_FUNCTION_3_ARGS(NAME, CFUNC, PSEUDO_OMP_THRESHOLD) \
  LAB_IMPLEMENT_VECTORIZED_FUNCTION_2_ARGS(NAME, CFUNC)

#endif

#define EXPAND(...) __VA_ARGS__

#define GET_4TH_ARG(ARG0, ARG1, ARG2, ARG3, ...) ARG3

#define LAB_IMPLEMENT_BASIC_FUNCTION_CHOOSE(...) \
  EXPAND(GET_4TH_ARG(__VA_ARGS__, LAB_IMPLEMENT_BASIC_FUNCTION_3_ARGS, LAB_IMPLEMENT_BASIC_FUNCTION_2_ARGS, ))

#define LAB_IMPLEMENT_VECTORIZED_FUNCTION_CHOOSE(...) \
  EXPAND(GET_4TH_ARG(__VA_ARGS__, LAB_IMPLEMENT_VECTORIZED_FUNCTION_3_ARGS, LAB_IMPLEMENT_VECTORIZED_FUNCTION_2_ARGS, ))

#define LAB_IMPLEMENT_BASIC_FUNCTION(...) EXPAND(LAB_IMPLEMENT_BASIC_FUNCTION_CHOOSE(__VA_ARGS__)(__VA_ARGS__))

#define LAB_IMPLEMENT_VECTORIZED_FUNCTION(...) EXPAND(LAB_IMPLEMENT_VECTORIZED_FUNCTION_CHOOSE(__VA_ARGS__)(__VA_ARGS__))

/*
 * LAB_IMPLEMENT_BASIC_FUNCTION is a macro with optional parameters, you can use it flexibly.
 * The macro will discard the invalid openmp threshold if openmp is unavailable. The macro will give a default threshold even if you forget to pass one.
 * In other word,
 * (A), If openmp is UNavailable, the two usage below is both right.
 *      (1) LAB_IMPLEMENT_BASIC_FUNCTION(type_func, func_entity, OMP_OVERHEAD_THRESHOLD) // discard the invalid openmp threshold
 *      (2) LAB_IMPLEMENT_BASIC_FUNCTION(type_func, func_entity)
 * (B), If openmp is available, the two usage below is also both right.
 *      (1) LAB_IMPLEMENT_BASIC_FUNCTION(type_func, func_entity, OMP_OVERHEAD_THRESHOLD)
 *      (2) LAB_IMPLEMENT_BASIC_FUNCTION(type_func, func_entity) // pass the default openmp threshold
 * So do LAB_IMPLEMENT_VECTORIZED_FUNCTION.
*/

LAB_IMPLEMENT_BASIC_FUNCTION(neg,-)

#if defined(TH_REAL_IS_LONG)
LAB_IMPLEMENT_BASIC_FUNCTION(abs,labs)
#endif /* int64_t only part */

#if defined(TH_REAL_IS_SHORT) || defined(TH_REAL_IS_INT)
LAB_IMPLEMENT_BASIC_FUNCTION(abs,abs)
#endif /* int only part */

#if defined(TH_REAL_IS_BYTE) /* Byte only part */

int THTensor_(logicalAndAll)(THTensor *tensor)
{
  real prod = 1;
  int serial_path = 0;
#ifdef _OPENMP
  int inOMP = omp_in_parallel();
  if(inOMP) {
    serial_path = 1;
  } else {
    TH_TENSOR_APPLY_REDUCTION_OMP(real, tensor, &&:prod, prod = prod && *tensor_data;, UNCERTAIN_TH_OMP_OVERHEAD_THRESHOLD);
  }
#else
    serial_path = 1;
#endif
  if (serial_path) {
    TH_TENSOR_APPLY(real, tensor, prod = prod && *tensor_data;);
  }
  return prod;
}

int THTensor_(logicalAnyAll)(THTensor *tensor)
{
  real sum = 0;
  int serial_path = 0;
#ifdef _OPENMP
  int inOMP = omp_in_parallel();
  if(inOMP) {
    serial_path = 1;
  } else {
    TH_TENSOR_APPLY_REDUCTION_OMP(real, tensor, ||:sum, sum = sum || *tensor_data;, UNCERTAIN_TH_OMP_OVERHEAD_THRESHOLD);
  }
#else
    serial_path = 1;
#endif
  if (serial_path) {
    TH_TENSOR_APPLY(real, tensor, sum = sum || *tensor_data;);
  }
  return (bool)sum;
}

void THTensor_(logicalAnd)(THTensor *r_, THTensor *t, int dimension, int keepdim)
{
  THLongStorage *dim;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 2, "dimension %d out of range",
      dimension + TH_INDEX_BASE);

  THTensor_(preserveReduceDimSemantics)(r_, THTensor_(nDimensionLegacyAll)(t), dimension, keepdim);
  dim = THTensor_(newSizeOf)(t);
  THLongStorage_set(dim, dimension, 1);
  THTensor_(resize)(r_, dim, NULL);
  THLongStorage_free(dim);

  int serial_path = 0;
#ifdef _OPENMP
  int inOMP = omp_in_parallel();
  if (inOMP) {
    serial_path = 1;
  } else {
    int r_Contig = THTensor_(isContiguous)(r_);
    real *tp = THTensor_(data)(t);
    real *rp = THTensor_(data)(r_);
    if(r_Contig && (tp != rp)){
      ptrdiff_t iter = 0;
      ptrdiff_t r_Size = THTensor_(nElement)(r_);
      int r_Dim = THTensor_nDimensionLegacyAll(r_);
      #pragma omp parallel for if ( r_Size > TH_OMP_OVERHEAD_THRESHOLD)
      for (iter = 0; iter < r_Size; iter++) {
        int j;
        int64_t quot;
        int64_t rem = iter;
        ptrdiff_t tBasicIndex = 0;

        for(j = 0; j < r_Dim; ++j) {
          if(j != dimension){
            quot = rem/r_->stride(j);
            rem = rem%r_->stride(j);
            tBasicIndex += quot*t->stride(j);
          }
        }
        real *t_data = tp+tBasicIndex;
        real *r__data = rp+iter;
        *r__data = 1;
        for(j=0; j < t->size(dimension); ++j) {
          *r__data = *r__data && *(t_data + j*t->stride(dimension));
        }
      }
    } else {
      serial_path = 1;
    }
  }
#else
  serial_path = 1;
#endif

  if(serial_path) {
    // two implementations optimized for data locality
    if (t->stride(dimension) == 1) {
      TH_TENSOR_DIM_APPLY2(real, t, real, r_, dimension,
                           accreal prod = 1;
                           int64_t i;
                           for(i = 0; i < t_size; i++)
                             prod = prod && t_data[i*t_stride];
                           *r__data = (real)prod;);
    } else {
      THTensor_(fill)(r_, 1);
      THTensor *temp_ = THTensor_(newWithTensor)(r_);
      // r_.expand_as(t)
      THTensor_setSizeAtDim(temp_, dimension, t->size(dimension));
      THTensor_setStrideAtDim(temp_, dimension, 0);

      TH_TENSOR_APPLY2(real, temp_, real, t, *temp__data = *temp__data && *t_data;);
      THTensor_(free)(temp_);
    }
  }
  if (!keepdim) {
    THTensor_(squeeze1d)(r_, r_, dimension);
  }
}

void THTensor_(logicalAny)(THTensor *r_, THTensor *t, int dimension, int keepdim)
{
  THLongStorage *dim;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 2, "dimension %d out of range",
      dimension + TH_INDEX_BASE);

  THTensor_(preserveReduceDimSemantics)(r_, THTensor_(nDimensionLegacyAll)(t), dimension, keepdim);
  dim = THTensor_(newSizeOf)(t);
  THLongStorage_set(dim, dimension, 1);
  THTensor_(resize)(r_, dim, NULL);
  THLongStorage_free(dim);

  int serial_path = 0;
#ifdef _OPENMP
  int inOMP = omp_in_parallel();
  if (inOMP) {
    serial_path = 1;
  } else {
    int r_Contig = THTensor_(isContiguous)(r_);
    real *tp = THTensor_(data)(t);
    real *rp = THTensor_(data)(r_);
    if(r_Contig && (tp != rp)){
      ptrdiff_t iter = 0;
      ptrdiff_t r_Size = THTensor_(nElement)(r_);
      int r_Dim = THTensor_nDimensionLegacyAll(r_);
      #pragma omp parallel for if ( r_Size > TH_OMP_OVERHEAD_THRESHOLD)
      for (iter = 0; iter < r_Size; iter++) {
        int j;
        int64_t quot;
        int64_t rem = iter;
        ptrdiff_t tBasicIndex = 0;

        for(j = 0; j < r_Dim; ++j) {
          if(j != dimension){
            quot = rem/r_->stride(j);
            rem = rem%r_->stride(j);
            tBasicIndex += quot*t->stride(j);
          }
        }
        real *t_data = tp+tBasicIndex;
        real *r__data = rp+iter;
        *r__data = 0;
        for(j=0; j < t->size(dimension); ++j) {
          *r__data = *r__data || *(t_data + j*t->stride(dimension));
        }
      }
    } else {
      serial_path = 1;
    }
  }
#else
  serial_path = 1;
#endif
  if (serial_path) {
    // two implementations optimized for data locality
    if (t->stride(dimension) == 1) {
      TH_TENSOR_DIM_APPLY2(real, t, real, r_, dimension,
                           accreal sum = 0;
                           int64_t i;
                           for(i = 0; i < t_size; i++)
                             sum = sum || t_data[i*t_stride];
                           *r__data = (real)sum;);
    } else {
      THTensor_(zero)(r_);
      THTensor *temp_ = THTensor_(newWithTensor)(r_);
      // r_.expand_as(t)
      THTensor_setSizeAtDim(temp_, dimension, t->size(dimension));
      THTensor_setStrideAtDim(temp_, dimension, 0);

      TH_TENSOR_APPLY2(real, temp_, real, t, *temp__data = *temp__data || *t_data;);
      THTensor_(free)(temp_);
    }
  }

  if (!keepdim) {
    THTensor_(squeeze1d)(r_, r_, dimension);
  }
}

#endif /* Byte only part */

/* floating point only now */
#if defined(TH_REAL_IS_FLOAT) || defined(TH_REAL_IS_DOUBLE)

#if defined (TH_REAL_IS_FLOAT)
#define TH_MATH_NAME(fn) fn##f
#else
#define TH_MATH_NAME(fn) fn
#endif

LAB_IMPLEMENT_BASIC_FUNCTION(log,TH_MATH_NAME(log))
LAB_IMPLEMENT_BASIC_FUNCTION(lgamma,TH_MATH_NAME(lgamma))
LAB_IMPLEMENT_BASIC_FUNCTION(digamma,TH_MATH_NAME(TH_digamma))
LAB_IMPLEMENT_BASIC_FUNCTION(trigamma,TH_MATH_NAME(TH_trigamma))
LAB_IMPLEMENT_BASIC_FUNCTION(log10,TH_MATH_NAME(log10))
LAB_IMPLEMENT_BASIC_FUNCTION(log1p,TH_MATH_NAME(log1p))
LAB_IMPLEMENT_BASIC_FUNCTION(log2,TH_MATH_NAME(log2))
LAB_IMPLEMENT_BASIC_FUNCTION(erf,TH_MATH_NAME(erf))
LAB_IMPLEMENT_BASIC_FUNCTION(erfc,TH_MATH_NAME(erfc))
LAB_IMPLEMENT_BASIC_FUNCTION(erfinv,TH_erfinv)
LAB_IMPLEMENT_BASIC_FUNCTION(ceil,TH_MATH_NAME(ceil))
LAB_IMPLEMENT_BASIC_FUNCTION(floor,TH_MATH_NAME(floor))
LAB_IMPLEMENT_BASIC_FUNCTION(round,TH_MATH_NAME(round))
LAB_IMPLEMENT_BASIC_FUNCTION(abs,TH_MATH_NAME(fabs))
LAB_IMPLEMENT_BASIC_FUNCTION(trunc,TH_MATH_NAME(trunc))
LAB_IMPLEMENT_BASIC_FUNCTION(frac,TH_MATH_NAME(TH_frac))
LAB_IMPLEMENT_BASIC_FUNCTION(cinv, TH_MATH_NAME(1.0) / )

LAB_IMPLEMENT_BASIC_FUNCTION(exp,TH_MATH_NAME(exp),HYPER_TH_OMP_OVERHEAD_THRESHOLD)
LAB_IMPLEMENT_BASIC_FUNCTION(expm1,TH_MATH_NAME(expm1),HYPER_TH_OMP_OVERHEAD_THRESHOLD)
LAB_IMPLEMENT_BASIC_FUNCTION(cos,TH_MATH_NAME(cos),HYPER_TH_OMP_OVERHEAD_THRESHOLD)
LAB_IMPLEMENT_BASIC_FUNCTION(acos,TH_MATH_NAME(acos),HYPER_TH_OMP_OVERHEAD_THRESHOLD)
LAB_IMPLEMENT_BASIC_FUNCTION(cosh,TH_MATH_NAME(cosh),HYPER_TH_OMP_OVERHEAD_THRESHOLD)
LAB_IMPLEMENT_BASIC_FUNCTION(sin,TH_MATH_NAME(sin),HYPER_TH_OMP_OVERHEAD_THRESHOLD)
LAB_IMPLEMENT_BASIC_FUNCTION(asin,TH_MATH_NAME(asin),HYPER_TH_OMP_OVERHEAD_THRESHOLD)
LAB_IMPLEMENT_BASIC_FUNCTION(sinh,TH_MATH_NAME(sinh),HYPER_TH_OMP_OVERHEAD_THRESHOLD)
LAB_IMPLEMENT_BASIC_FUNCTION(tan,TH_MATH_NAME(tan),HYPER_TH_OMP_OVERHEAD_THRESHOLD)
LAB_IMPLEMENT_BASIC_FUNCTION(atan,TH_MATH_NAME(atan),HYPER_TH_OMP_OVERHEAD_THRESHOLD)
LAB_IMPLEMENT_BASIC_FUNCTION(tanh,TH_MATH_NAME(tanh),HYPER_TH_OMP_OVERHEAD_THRESHOLD)
LAB_IMPLEMENT_BASIC_FUNCTION(sqrt,TH_MATH_NAME(sqrt),HYPER_TH_OMP_OVERHEAD_THRESHOLD)
LAB_IMPLEMENT_BASIC_FUNCTION(rsqrt,TH_MATH_NAME(TH_rsqrt),HYPER_TH_OMP_OVERHEAD_THRESHOLD)

LAB_IMPLEMENT_VECTORIZED_FUNCTION(sigmoid,TH_MATH_NAME(TH_sigmoid),HYPER_TH_OMP_OVERHEAD_THRESHOLD)

void THTensor_(atan2)(THTensor *r_, THTensor *tx, THTensor *ty)
{
  THTensor_(resizeAs)(r_, tx);
  TH_TENSOR_APPLY3(real, r_, real, tx, real, ty, *r__data = TH_MATH_NAME(atan2)(*tx_data,*ty_data););
}

void THTensor_(polygamma)(THTensor *r_, int64_t n, THTensor *t) {
  switch (n) {
    case 0: THTensor_(digamma)(r_, t); return;
    case 1: THTensor_(trigamma)(r_, t); return;
    default: THError("polygamma(n,x) is not implemented for n>=2");
  }
}

void THTensor_(lerp)(THTensor *r_, THTensor *a, THTensor *b, real weight)
{
  THArgCheck(THTensor_(nElement)(a) == THTensor_(nElement)(b), 2, "sizes do not match");
  THTensor_(resizeAs)(r_, a);
  TH_TENSOR_APPLY3(real, r_, real, a, real, b, *r__data = TH_MATH_NAME(TH_lerp)(*a_data, *b_data, weight););
}

void THTensor_(mean)(THTensor *r_, THTensor *t, int dimension, int keepdim)
{
  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 2, "invalid dimension %d",
      dimension + TH_INDEX_BASE);

  THTensor_(sum)(r_, t, dimension, keepdim);
  THTensor_(div)(r_, r_, t->size(dimension));
}

void THTensor_(std)(THTensor *r_, THTensor *t, int dimension, int biased, int keepdim)
{
  THLongStorage *dim;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 3, "invalid dimension %d",
      dimension + TH_INDEX_BASE);

  THTensor_(preserveReduceDimSemantics)(r_, THTensor_(nDimensionLegacyAll)(t), dimension, keepdim);
  dim = THTensor_(newSizeOf)(t);
  THLongStorage_set(dim, dimension, 1);
  THTensor_(resize)(r_, dim, NULL);
  THLongStorage_free(dim);

  TH_TENSOR_DIM_APPLY2(real, t, real, r_, dimension,
                       // Uses Welford's algorithm for numeric stability
                       accreal mean = 0;
                       accreal M2 = 0;

                       int64_t i;
                       for (i = 0; i < t_size; i++)
                       {
                         real z = t_data[i*t_stride];
                         real delta = z - mean;
                         mean += delta / (i + 1);
                         real delta2 = z - mean;
                         M2 += delta * delta2;
                       }

                       if (biased && t_size >= 2)
                       {
                         *r__data = TH_MATH_NAME(sqrt)(M2 / t_size);
                       } else if (!biased && t_size >= 2) {
                         *r__data = TH_MATH_NAME(sqrt)(M2 / (t_size - 1));
                       } else if (biased && t_size == 1) {
                         *r__data = 0;
                       } else {
                         *r__data = NAN;
                       });

  if (!keepdim) {
    THTensor_(squeeze1d)(r_, r_, dimension);
  }
}

void THTensor_(var)(THTensor *r_, THTensor *t, int dimension, int biased, int keepdim)
{
  THLongStorage *dim;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 3, "invalid dimension %d",
      dimension + TH_INDEX_BASE);

  THTensor_(preserveReduceDimSemantics)(r_, THTensor_(nDimensionLegacyAll)(t), dimension, keepdim);
  dim = THTensor_(newSizeOf)(t);
  THLongStorage_set(dim, dimension, 1);
  THTensor_(resize)(r_, dim, NULL);
  THLongStorage_free(dim);

  TH_TENSOR_DIM_APPLY2(real, t, real, r_, dimension,
                       // Uses Welford's algorithm for numeric stability
                       accreal mean = 0;
                       accreal M2 = 0;

                       int64_t i;
                       for (i = 0; i < t_size; i++)
                       {
                         real z = t_data[i*t_stride];
                         real delta = z - mean;
                         mean += delta / (i + 1);
                         real delta2 = z - mean;
                         M2 += delta * delta2;
                       }

                       if (biased && t_size >= 2)
                       {
                         *r__data = M2 / t_size;
                       } else if (!biased && t_size >= 2) {
                         *r__data = M2 / (t_size - 1);
                       } else if (biased && t_size == 1) {
                         *r__data = 0;
                       } else {
                         *r__data = NAN;
                       });

  if (!keepdim) {
    THTensor_(squeeze1d)(r_, r_, dimension);
  }
}

void THTensor_(norm)(THTensor *r_, THTensor *t, real value, int dimension, int keepdim)
{
  THLongStorage *dim;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(t), 3, "invalid dimension %d",
      dimension + TH_INDEX_BASE);

  THTensor_(preserveReduceDimSemantics)(r_, THTensor_(nDimensionLegacyAll)(t), dimension, keepdim);
  dim = THTensor_(newSizeOf)(t);
  THLongStorage_set(dim, dimension, 1);
  THTensor_(resize)(r_, dim, NULL);
  THLongStorage_free(dim);

  #define DIM_REDUCE(reduce, transform) \
    TH_TENSOR_DIM_APPLY2(real, t, real, r_, dimension,      \
                         accreal sum = 0;                   \
                         int64_t i;                         \
                         for(i = 0; i < t_size; i++) {      \
                           (reduce);                        \
                         }                                  \
                         (transform);)                      \

  if(value == 0) {
    DIM_REDUCE(sum += t_data[i*t_stride] != 0.0,
               *r__data = sum);
  } else if (value == 1) {
    DIM_REDUCE(sum += TH_MATH_NAME(fabs)(t_data[i*t_stride]),
               *r__data = sum);
  } else if (value == 2) {
    DIM_REDUCE(sum += t_data[i*t_stride] * t_data[i*t_stride],
               *r__data = TH_MATH_NAME(sqrt)(sum));
  } else if (value == 3) {
    DIM_REDUCE(sum += TH_MATH_NAME(fabs)(t_data[i*t_stride] * t_data[i*t_stride] * t_data[i*t_stride]),
               *r__data = TH_MATH_NAME(pow)(sum, 1.0/3));
  } else if (value == INFINITY) {
    DIM_REDUCE(sum = THMax(sum, TH_MATH_NAME(fabs)(t_data[i*t_stride])),
	       *r__data = sum);
  } else {
    DIM_REDUCE(sum += TH_MATH_NAME(pow)(TH_MATH_NAME(fabs)(t_data[i*t_stride]), value),
               *r__data = TH_MATH_NAME(pow)(sum, 1.0/value));
  }

  if (!keepdim) {
    THTensor_(squeeze1d)(r_, r_, dimension);
  }
  #undef DIM_REDUCE
}

accreal THTensor_(normall)(THTensor *tensor, real value)
{
  accreal sum = 0;
  if(value == 0) {
    TH_TENSOR_APPLY(real, tensor, sum += *tensor_data != 0.0;);
    return sum;
  } else if(value == 1) {
    TH_TENSOR_APPLY(real, tensor, sum += TH_MATH_NAME(fabs)(*tensor_data););
    return sum;
  } else if(value == 2) {
    TH_TENSOR_APPLY(real, tensor, accreal z = *tensor_data; sum += z*z;);
    return sqrt(sum);
  } else if(value == 3) {
    TH_TENSOR_APPLY(real, tensor, accreal z = *tensor_data; sum += std::abs(z*z*z););
    return TH_MATH_NAME(pow)(sum, 1.0/3);
  } else if(value == INFINITY) {
    TH_TENSOR_APPLY(real, tensor, sum = THMax(sum, TH_MATH_NAME(fabs)(*tensor_data)););
    return sum;
  } else {
    TH_TENSOR_APPLY(real, tensor, sum += TH_MATH_NAME(pow)(TH_MATH_NAME(fabs)(*tensor_data), value););
    return TH_MATH_NAME(pow)(sum, 1.0/value);
  }
}

void THTensor_(renorm)(THTensor *res, THTensor *src, real value, int dimension, real maxnorm)
{
  THTensor *rowR, *rowS;

  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyNoScalars)(src), 3, "invalid dimension %d",
      dimension + TH_INDEX_BASE);
  THArgCheck(value > 0, 2, "non-positive-norm not supported");
  THArgCheck(THTensor_(nDimensionLegacyNoScalars)(src) > 1, 1, "need at least 2 dimensions, got %d dimensions",
      THTensor_(nDimensionLegacyNoScalars)(src));

  rowR = THTensor_(new)();
  rowS = THTensor_(new)();

  THTensor_(resizeAs)(res, src);

  for (int64_t i = 0; i < src->size(dimension); i++)
  {
    real norm = 0;
    real new_norm;

    THTensor_(select)(rowS, src, dimension, i);
    THTensor_(select)(rowR, res, dimension, i);
    if (value == 1) {
      TH_TENSOR_APPLY(real, rowS, norm += fabs(*rowS_data););
    } else if (value == 2) {
      TH_TENSOR_APPLY(real, rowS, accreal z = *rowS_data; norm += z*z;);
    } else if (value == INFINITY) {
      TH_TENSOR_APPLY(real, rowS, norm = THMax(norm, TH_MATH_NAME(fabs)(*rowS_data)););
    } else {
      TH_TENSOR_APPLY(real, rowS, norm += TH_MATH_NAME(pow)(TH_MATH_NAME(fabs)(*rowS_data), value););
    }

    if (value != INFINITY) {
      norm = pow(norm, 1/value);
    }

    if (norm > maxnorm)
    {
      new_norm = maxnorm / (norm + 1e-7);

      TH_TENSOR_APPLY2(
        real, rowR, real, rowS,
        *rowR_data = (*rowS_data) * new_norm;
      )
    }
    else
      THTensor_(copy)(rowR, rowS);
  }

  THTensor_(free)(rowR);
  THTensor_(free)(rowS);
}

accreal THTensor_(dist)(THTensor *tensor, THTensor *src, real value)
{
  real sum = 0;
  TH_TENSOR_APPLY2(real, tensor, real, src,
                   sum += TH_MATH_NAME(pow)(
                     TH_MATH_NAME(fabs)(*tensor_data - *src_data), value););
  return TH_MATH_NAME(pow)(sum, 1.0/value);
}

accreal THTensor_(meanall)(THTensor *tensor)
{
  return THTensor_(sumall)(tensor)/THTensor_(nElement)(tensor);
}

accreal THTensor_(varall)(THTensor *tensor, int biased)
{
  accreal mean = THTensor_(meanall)(tensor);
  accreal sum = 0;
  TH_TENSOR_APPLY(real, tensor, sum += (*tensor_data - mean)*(*tensor_data - mean););
  sum /= std::max<int64_t>(0, THTensor_(nElement)(tensor) - (biased ? 0 : 1));
  return sum;
}

accreal THTensor_(stdall)(THTensor *tensor, int biased)
{
  return sqrt(THTensor_(varall)(tensor, biased));
}

void THTensor_(linspace)(THTensor *r_, real a, real b, int64_t n)
{
  real i = 0;

  // NumPy allows you to pass different points even if n <= 1 -- should we?
  THArgCheck(n > 1 || ((n == 0 || n == 1) && (a == b)), 3, "invalid number of points");

  if (THTensor_(nElement)(r_) != n) {
    THTensor_(resize1d)(r_, n);
  }

  if (n == 0) {
  } else if (n == 1) {
    THTensor_(set1d)(r_, 0, a);
  } else {
     TH_TENSOR_APPLY(real, r_,
             *r__data = a + (b-a)/((real)(n-1))*i;
             i++;
           );
  }
}

void THTensor_(logspace)(THTensor *r_, real a, real b, int64_t n)
{
  real i = 0;

  // NumPy allows you to pass different points even if n <= 1 -- should we?
  THArgCheck(n > 1 || ((n == 0 || n == 1) && (a == b)), 3, "invalid number of points");

  if (THTensor_(nElement)(r_) != n) {
    THTensor_(resize1d)(r_, n);
  }

  if (n == 0) {
  } else if (n == 1) {
    THTensor_(set1d)(r_, 0, TH_MATH_NAME(pow)(10.0, a));
  } else {
    TH_TENSOR_APPLY(real, r_,
        *r__data = TH_MATH_NAME(pow)(10.0, a + i*(b-a)/((real)(n-1)));
        i++;
        );
  }
}

void THTensor_(histc)(THTensor *hist, THTensor *tensor, int64_t nbins, real minvalue, real maxvalue)
{
  real minval;
  real maxval;
  real *h_data;

  THTensor_(resize1d)(hist, nbins);
  THTensor_(zero)(hist);
  minval = minvalue;
  maxval = maxvalue;
  if (minval == maxval)
  {
    minval = THTensor_(minall)(tensor);
    maxval = THTensor_(maxall)(tensor);
  }
  if (minval == maxval)
  {
    minval = minval - 1;
    maxval = maxval + 1;
  }

  h_data = THTensor_(data)(hist);

  TH_TENSOR_APPLY(real, tensor,
    if (*tensor_data >= minval && *tensor_data <= maxval) {
      const int bin = (int)((*tensor_data-minval) / (maxval-minval) * nbins);
      h_data[THMin(bin, nbins-1)] += 1;
    }
  );
}

void THTensor_(bhistc)(THTensor *hist, THTensor *tensor, int64_t nbins, real minvalue, real maxvalue)
{
  THArgCheck(THTensor_(nDimensionLegacyAll)(tensor) < 3, 2, "invalid dimension %d, the input must be a 2d tensor", THTensor_(nDimensionLegacyAll)(tensor));

  int dimension = 1;
  THArgCheck(dimension >= 0 && dimension < THTensor_(nDimensionLegacyAll)(tensor), 2, "invalid dimension %d",
      dimension + TH_INDEX_BASE);

  real minval;
  real maxval;

  THTensor_(resize2d)(hist, tensor->size(0), nbins);
  THTensor_(zero)(hist);

  minval = minvalue;
  maxval = maxvalue;
  if (minval == maxval)
  {
    minval = THTensor_(minall)(tensor);
    maxval = THTensor_(maxall)(tensor);
  }
  if (minval == maxval)
  {
    minval = minval - 1;
    maxval = maxval + 1;
  }

  TH_TENSOR_DIM_APPLY2(real, tensor, real, hist, dimension, int64_t i;
                        for(i = 0; i < tensor_size; i++)
                        {
                          if(tensor_data[i*tensor_stride] >= minval && tensor_data[i*tensor_stride] <= maxval) {
                            const int bin = (int)((tensor_data[i*tensor_stride]-minval) / (maxval-minval) * nbins);
                            hist_data[THMin(bin, nbins-1)] += 1;
                          }
                        }
  );
}

// Approximate reparameterized gradient of Beta(x,alpha,beta) wrt alpha.
// Assumes x is close to zero and uses a Taylor expansion.
static inline real THTensor_(beta_grad_alpha_small)(real x, real alpha, real beta) {
  const real factor = TH_MATH_NAME(TH_digamma)(alpha) - TH_MATH_NAME(TH_digamma)(alpha + beta) - TH_MATH_NAME(log)(x);
  real numer = 1;
  real series = numer / alpha * (factor + 1 / alpha);
  for (int i = 1; i <= 10; ++i) {
    numer *= (i - beta) * x / i;
    const real denom = alpha + i;
    series += numer / denom * (factor + 1 / denom);
  }
  const real result = x * TH_MATH_NAME(pow)(1 - x, -beta) * series;
  return th_isnan(result) ? 0.0 : result;
}

// Approximate reparameterized gradient of Beta(x,alpha,beta) wrt beta.
// Assumes x is close to zero and uses a Taylor expansion.
static inline real THTensor_(beta_grad_beta_small)(real x, real alpha, real beta) {
  const real factor = TH_MATH_NAME(TH_digamma)(alpha+beta) - TH_MATH_NAME(TH_digamma)(beta);
  real numer = 1;
  real betas = 1;
  real dbetas = 0;
  real series = factor / alpha;
  for (int i = 1; i <= 8; ++i) {
    numer *= -x / i;
    dbetas = dbetas * (beta - i) + betas;
    betas = betas * (beta - i);
    series += numer / (alpha + i) * (dbetas + factor * betas);
  }
  const real result = -TH_MATH_NAME(pow)(1 - x, 1 - beta) * series;
  return th_isnan(result) ? 0.0 : result;
}

// Approximate reparameterized gradient of Beta(x,alpha,beta) wrt alpha.
// Assumes alpha and beta are both large and uses a Rice saddle point expansion.
// To ensure numerical stability, this computation is performed at higher precision.
static inline real THTensor_(beta_grad_alpha_mid)(double x, double alpha, double beta) {
  const double total = alpha + beta;
  const double mean = alpha / total;
  const double std = sqrt(alpha * beta / (total + 1)) / total;
  if (mean - 0.1 * std <= x && x <= mean + 0.1 * std) {
    // Avoid the singularity at x = mean.
    const double poly = 47 * x * (beta*beta)*(beta*beta) + alpha * (
                      (43 + 20 * (16 + 27 * beta) * x) * (beta*beta)*beta + alpha * (
                      3 * (59 + 180 * beta - 90 * x) * (beta*beta) + alpha * (
                      (453 + 1620 * beta * (1 - x) - 455 * x) * beta + alpha * (
                      8 * (1 - x) * (135 * beta - 11)))));
    const double prefactor_num = (1 + 12 * alpha) * (1 + 12 * beta) / (total * total);
    const double prefactor_den = 12960 * alpha * alpha * alpha * beta * beta * (1 + 12 * total);
    return prefactor_num / (1 - x) * poly / prefactor_den;
  }
  const double prefactor = -x / sqrt(2 * alpha * beta / total);
  const double stirling = (1 + 1 / (12 * alpha) + 1 / (288 * alpha*alpha))
                        * (1 + 1 / (12 * beta) + 1 / (288 * beta*beta))
                        / (1 + 1 / (12 * total) + 1 / (288 * total*total));
  const double term1_num = 2 * (alpha*alpha) * (x - 1) + alpha * beta * (x - 1) - x * (beta*beta);
  const double axbx = alpha * (x-1) + beta * x;
  const double term1_den = sqrt(2 * alpha / beta) * pow(total, 1.5f) * axbx*axbx;
  const double term1 = term1_num / term1_den;
  const double term2 = 0.5f * log(alpha / (total * x));
  const double term3_num = sqrt(8 * alpha * beta / total);
  const double term3_den = beta * x + alpha * (x - 1);
  const double term3 = term3_num / term3_den;
  const double term4_base = beta * log(beta / (total * (1 - x))) +
                          alpha * log(alpha / (total * x));
  const double term4 = pow(term4_base, -1.5f);
  const double term1234 = term1 + term2 * (term3 + (x < mean ? term4 : -term4));
  return stirling * prefactor * term1234;
}

// Computes a scaled reparameterized gradient
//   -(d/dalpha cdf(x;alpha,beta)) / pdf(x;alpha,beta) / (1-x)
// for random number x drawn from a Beta distribution Beta(alpha,beta).
// This function inputs total=alpha+beta to make it easy to implement
// Dirichlet reparameterized gradients in terms of Betas.
static inline real THTensor_(dirichlet_grad_one)(real x, real alpha, real total) {
  const real beta = total - alpha;
  const real boundary = total * x * (1 - x);

  // Use an asymptotic approximation for x close to 0.
  if (x <= 0.5f && boundary < 2.5f) {
    return THTensor_(beta_grad_alpha_small)(x, alpha, beta);
  }

  // Use an asymptotic approximation for x close to 1.
  if (x >= 0.5f && boundary < 0.75f) {
    return -THTensor_(beta_grad_beta_small)(1 - x, beta, alpha);
  }

  // Use an asymptotic approximation when alpha and (total - alpha) are both large.
  if (alpha > 6 && beta > 6) {
    return THTensor_(beta_grad_alpha_mid)(x, alpha, beta);
  }

  // Use a rational correction to an analytic approximation.
  static const real c[2][3][3][4] = {
    {{{1.003668233, -0.01061107488, -0.0657888334, 0.01201642863},
      {0.6336835991, -0.3557432599, 0.05486251648, -0.001465281033},
      {-0.03276231906, 0.004474107445, 0.002429354597, -0.0001557569013}},
     {{0.221950385, -0.3187676331, 0.01799915743, 0.01074823814},
      {-0.2951249643, 0.06219954479, 0.01535556598, 0.001550077057},
      {0.02155310298, 0.004170831599, 0.001292462449, 6.976601077e-05}},
     {{-0.05980841433, 0.008441916499, 0.01085618172, 0.002319392565},
      {0.02911413504, 0.01400243777, -0.002721828457, 0.000751041181},
      {0.005900514878, -0.001936558688, -9.495446725e-06, 5.385558597e-05}}},
    {{{1, -0.02924021934, -0.04438342661, 0.007285809825},
      {0.6357567472, -0.3473456711, 0.05454656494, -0.002407477521},
      {-0.03301322327, 0.004845219414, 0.00231480583, -0.0002307248149}},
     {{0.5925320577, -0.1757678135, 0.01505928619, 0.000564515273},
      {0.1014815858, -0.06589186703, 0.01272886114, -0.0007316646956},
      {-0.007258481865, 0.001096195486, 0.0003934994223, -4.12701925e-05}},
     {{0.06469649321, -0.0236701437, 0.002902096474, -5.896963079e-05},
      {0.001925008108, -0.002869809258, 0.0008000589141, -6.063713228e-05},
      {-0.0003477407336, 6.959756487e-05, 1.097287507e-05, -1.650964693e-06}}},
  };
  const real u = TH_MATH_NAME(log)(x);
  const real a = TH_MATH_NAME(log)(alpha) - u;
  const real b = TH_MATH_NAME(log)(total) - a;
  const real pow_u[3] = {1, u, u * u};
  const real pow_a[3] = {1, a, a * a};
  real p = 0.0;
  real q = 0.0;
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      const real ua = pow_u[i] * pow_a[j];
      p += ua * (c[0][i][j][0] + b * (c[0][i][j][1] + b * (c[0][i][j][2] + b * c[0][i][j][3])));
      q += ua * (c[1][i][j][0] + b * (c[1][i][j][1] + b * (c[1][i][j][2] + b * c[1][i][j][3])));
    }
  }
  const real approx = x * (TH_MATH_NAME(TH_digamma)(total) - TH_MATH_NAME(TH_digamma)(alpha)) / beta;
  return p / q * approx;
}

void THTensor_(dirichlet_grad)(THTensor *self, THTensor *x, THTensor *alpha, THTensor *total)
{
  x = THTensor_(newContiguous)(x);
  alpha = THTensor_(newContiguous)(alpha);
  total = THTensor_(newContiguous)(total);
  TH_CHECK_SAME_SIZE(alpha, x);
  TH_CHECK_SAME_SIZE(total, x);
  THTensor_(resizeAs)(self, x);
  THTensor* grad = THTensor_(newContiguous)(self);

  real*const grad_data = THTensor_(data)(grad);
  real*const x_data = THTensor_(data)(x);
  real*const alpha_data = THTensor_(data)(alpha);
  real*const total_data = THTensor_(data)(total);
  const int64_t numel = THTensor_(nElement)(x);
  int64_t i;
  #pragma omp parallel for if(numel > TH_OMP_OVERHEAD_THRESHOLD) private(i)
  for(i = 0; i < numel; ++i) {
    grad_data[i] = THTensor_(dirichlet_grad_one)(x_data[i], alpha_data[i], total_data[i]);
  }

  THTensor_(freeCopyTo)(grad, self);
}

#undef TH_MATH_NAME
#endif /* floating point only part */
#undef IS_NONZERO

#endif /* TH_GENERIC_FILE */
