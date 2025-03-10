#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <chrono>
#include <omp.h>

#define threadsPerBlock 512

// Kernel for fast unfold+copy on volumes
template <typename T>
void vol2col_kernel(
    const T* data_vol,
    const int channels,
    const int depth,
    const int height,
    const int width,
    const int ksize_t,
    const int ksize_h,
    const int ksize_w,
    const int pad_t,
    const int pad_h,
    const int pad_w,
    const int stride_t,
    const int stride_h,
    const int stride_w,
    const int dilation_t,
    const int dilation_h,
    const int dilation_w,
    const int depth_col,
    const int height_col,
    const int width_col,
    T* data_col)
{
  #pragma omp target teams distribute parallel for collapse(4) \
  num_threads(threadsPerBlock)
  for (int channel_in = 0; channel_in < channels; channel_in++) {
  for (int t_out = 0; t_out < depth_col; t_out++) {
  for (int h_out = 0; h_out < height_col; h_out++) {
  for (int w_out = 0; w_out < width_col; w_out++) {
    int channel_out = channel_in * ksize_t * ksize_h * ksize_w;
    int t_in = t_out * stride_t - pad_t;
    int h_in = h_out * stride_h - pad_h;
    int w_in = w_out * stride_w - pad_w;
    data_vol += ((channel_in * depth + t_in) * height + h_in) * width + w_in;
    data_col += ((channel_out * depth_col + t_out) * height_col + h_out) * width_col + w_out;

    for (int i = 0; i < ksize_t; ++i) {
      for (int j = 0; j < ksize_h; ++j) {
        for (int k = 0; k < ksize_w; ++k) {
          int t = t_in + i * dilation_t;
          int h = h_in + j * dilation_h;
          int w = w_in + k * dilation_w;
          *data_col = (t >= 0 && h >= 0 && w >= 0 && t < depth && h < height && w < width)
              ? data_vol[i * dilation_t * height * width +
                         j * dilation_h * width + k * dilation_w]
              : static_cast<T>(0);
          data_col += depth_col * height_col * width_col;
        }
      }
    }
  } } } }
}

template <typename T, typename accT>
void col2vol_kernel(
    const T* data_col,
    const uint64_t n,
    const unsigned depth,
    const unsigned height,
    const unsigned width,
    const unsigned kernel_t,
    const unsigned kernel_h,
    const unsigned kernel_w,
    const unsigned pad_t,
    const unsigned pad_h,
    const unsigned pad_w,
    const unsigned stride_t,
    const unsigned stride_h,
    const unsigned stride_w,
    const unsigned dilation_t,
    const unsigned dilation_h,
    const unsigned dilation_w,
    const unsigned depth_col,
    const unsigned height_col,
    const unsigned width_col,
    T* data_vol)
{
  #pragma omp target teams distribute parallel for num_threads(threadsPerBlock)
  for (uint64_t index = 0; index < n; index ++) {
    accT val = static_cast<accT>(0);
    const unsigned w_im = index % width + pad_w;
    const unsigned h_im = (index / width) % height + pad_h;
    const unsigned t_im = (index / width / height) % depth + pad_t;
    const unsigned c_im = index / (width * height * depth);
    auto kernel_extent_w = (kernel_w - 1) * dilation_w + 1;
    auto kernel_extent_h = (kernel_h - 1) * dilation_h + 1;
    auto kernel_extent_t = (kernel_t - 1) * dilation_t + 1;
    // compute the start and end of the output
    const auto w_col_start =
        (w_im < kernel_extent_w) ? 0 : (w_im - kernel_extent_w) / stride_w + 1;
    const auto w_col_end = std::min(w_im / stride_w + 1, width_col);
    const auto h_col_start =
        (h_im < kernel_extent_h) ? 0 : (h_im - kernel_extent_h) / stride_h + 1;
    const auto h_col_end = std::min(h_im / stride_h + 1, height_col);
    const auto t_col_start =
        (t_im < kernel_extent_t) ? 0 : (t_im - kernel_extent_t) / stride_t + 1;
    const auto t_col_end = std::min(t_im / stride_t + 1, depth_col);
    // TODO: use LCM of stride and dilation to avoid unnecessary loops
    for (unsigned t_col = t_col_start; t_col < t_col_end; t_col += 1) {
      for (unsigned h_col = h_col_start; h_col < h_col_end; h_col += 1) {
        for (unsigned w_col = w_col_start; w_col < w_col_end; w_col += 1) {
          uint64_t t_k = (t_im - t_col * stride_t);
          uint64_t h_k = (h_im - h_col * stride_h);
          uint64_t w_k = (w_im - w_col * stride_w);
          if (t_k % dilation_t == 0 && h_k % dilation_h == 0 &&
              w_k % dilation_w == 0) {
            t_k /= dilation_t;
            h_k /= dilation_h;
            w_k /= dilation_w;
            const uint64_t idx_k =
                ((c_im * kernel_t + t_k) * kernel_h + h_k) * kernel_w + w_k;
            const uint64_t data_col_index =
                ((idx_k * depth_col + t_col) *
                    height_col + h_col) *
                  width_col + w_col;
            val += data_col[data_col_index];
          }
        }
      }
    }
    data_vol[index] = static_cast<T>(val);
  }
}

template <typename T>
void eval (
    const int repeat,
    const int channels,
    const int depth,
    const int height,
    const int width,
    const int depth_col,
    const int height_col,
    const int width_col,
    const int ksize_t,
    const int ksize_h,
    const int ksize_w,
    const int pad_t,
    const int pad_h,
    const int pad_w,
    const int stride_t,
    const int stride_h,
    const int stride_w,
    const int dilation_t,
    const int dilation_h,
    const int dilation_w)
{
  uint64_t vol_size = (uint64_t) channels * (2*pad_t+depth) * (2*pad_h+height) * (2*pad_w+width);
  uint64_t col_size = ((uint64_t) channels * ksize_t * ksize_h * ksize_w + 1) * 
                    (depth_col+pad_t) * (height_col+pad_h) * (width_col+pad_w);
                         
  uint64_t vol_size_bytes = sizeof(T) * vol_size;
  uint64_t col_size_bytes = sizeof(T) * col_size;
  
  T *data_vol = (T*) malloc (vol_size_bytes);
  T *data_col = (T*) malloc (col_size_bytes);

  for (uint64_t i = 0; i < vol_size; i++) {
    data_vol[i] = (T)1; 
  }

  memset(data_col, 0, col_size_bytes);

  uint64_t n = static_cast<uint64_t>(channels) * depth_col * height_col * width_col;

  #pragma omp target data map(to: data_vol[0:vol_size]) \
                          map(to: data_col[0:col_size])
  {
    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < repeat; i++) {
      vol2col_kernel<T>(
        data_vol,
        channels, depth, height, width,
        ksize_t, ksize_h, ksize_w,
        pad_t, pad_h, pad_w,
        stride_t, stride_h, stride_w,
        dilation_t, dilation_h, dilation_w,
        depth_col, height_col, width_col,
        data_col);
    }

    auto end = std::chrono::steady_clock::now();
    auto time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    printf("Average execution time of vol2col kernel: %f (us)\n", (time * 1e-3f) / repeat);

    #pragma omp target update from (data_col[0:col_size])
    float checksum = 0;
    for (uint64_t i = 0; i < col_size; i++) {
      checksum += data_col[i];
    }
    printf("Checksum = %f\n", checksum / col_size);

    start = std::chrono::steady_clock::now();

    for (int i = 0; i < repeat; i++) {
      col2vol_kernel<T, T>(
        data_col,
        n, depth, height, width,
        ksize_t, ksize_h, ksize_w,
        pad_t, pad_h, pad_w,
        stride_t, stride_h, stride_w,
        dilation_t, dilation_h, dilation_w,
        depth_col, height_col, width_col,
        data_vol);
    }

    end = std::chrono::steady_clock::now();
    time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    printf("Average execution time of col2vol kernel: %f (us)\n", (time * 1e-3f) / repeat);

    #pragma omp target update from (data_vol[0:vol_size])

    checksum = 0;
    for (uint64_t i = 0; i < vol_size; i++) {
      checksum += data_vol[i];
    }
    printf("Checksum = %f\n", checksum / vol_size);
  }

  free(data_vol);
  free(data_col);
}

int main(int argc, char* argv[])
{
  if (argc != 2) {
    printf("Usage: %s <repeat>\n", argv[0]);
    return 1;
  }
  const int repeat = atoi(argv[1]);
  
  int channels = 4;
  int depth = 3;
  int height = 255;
  int width = 255;
  int pad_t = 1;
  int pad_h = 1;
  int pad_w = 1;
  int stride_t = 2;
  int stride_h = 2;
  int stride_w = 2;
  int dilation_t = 2;
  int dilation_h = 2;
  int dilation_w = 2;
  int depth_col = 3;
  int height_col = 255;
  int width_col = 255;

  for (int k = 1; k <= 9; k = k + 2) {
    printf("\nkernel size: %d\n", k);
    int ksize_t = k;
    int ksize_h = k;
    int ksize_w = k;

    eval<float> (repeat,
                 channels, depth, height, width,
                 depth_col, height_col, width_col,
                 ksize_t, ksize_h, ksize_w,
                 pad_t, pad_h, pad_w,
                 stride_t, stride_h, stride_w,
                 dilation_t, dilation_h, dilation_w);
  }

  return 0;
}
