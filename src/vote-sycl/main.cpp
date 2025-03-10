#include <assert.h>
#include <stdio.h>
#include <chrono>
#include <sycl/sycl.hpp>
#include "reference.h"
#include "kernels.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Usage: %s <repeat>\n", argv[0]);
    return 1;
  }

  const int repeat = atoi(argv[1]);

  bool *dinfo = NULL, *hinfo = NULL;
  int error_count[3] = {0, 0, 0};

  unsigned int *h_input, *h_result;
  unsigned int *d_input, *d_result;

#ifdef USE_GPU
  sycl::queue q(sycl::gpu_selector_v, sycl::property::queue::in_order());
#else
  sycl::queue q(sycl::cpu_selector_v, sycl::property::queue::in_order());
#endif
  auto sg_sizes = q.get_device().get_info<sycl::info::device::sub_group_sizes>();
  auto r = std::max_element(sg_sizes.begin(), sg_sizes.end());
  int warp_size = *r;

  h_input = (unsigned int *)malloc(VOTE_DATA_GROUP * warp_size *
                                   sizeof(unsigned int));
  h_result = (unsigned int *)malloc(VOTE_DATA_GROUP * warp_size *
                                    sizeof(unsigned int));
  genVoteTestPattern(h_input, VOTE_DATA_GROUP * warp_size);

  d_input = sycl::malloc_device<unsigned int>(
      VOTE_DATA_GROUP * warp_size, q);

  d_result = sycl::malloc_device<unsigned int>(
      VOTE_DATA_GROUP * warp_size, q);

  q.memcpy(d_input, h_input,
           VOTE_DATA_GROUP * warp_size * sizeof(unsigned int)).wait();

  sycl::range<1> gws (VOTE_DATA_GROUP * warp_size);
  sycl::range<1> lws (VOTE_DATA_GROUP * warp_size);

  // Start of Vote Any Test Kernel #1
  printf("\tRunning <<Vote.Any>> kernel1 ...\n");
  auto kernel1 = [&](sycl::handler& cgh) {
    cgh.parallel_for(sycl::nd_range<1>(gws, lws), [=](sycl::nd_item<1> item) {
      VoteAnyKernel1(d_input, d_result, repeat, item);
    });
  };

  // Warmup
  q.submit(kernel1).wait();

  auto start = std::chrono::steady_clock::now();

  q.submit(kernel1).wait();

  auto end = std::chrono::steady_clock::now();
  auto time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  printf("\tkernel execution time: %f (s)\n", time * 1e-9f);

  q.memcpy(h_result, d_result,
           VOTE_DATA_GROUP * warp_size * sizeof(unsigned int)).wait();

  error_count[0] += checkResultsVoteAnyKernel1(
      h_result, VOTE_DATA_GROUP * warp_size, warp_size);

  // Start of Vote All Test Kernel #2
  printf("\tRunning <<Vote.All>> kernel2 ...\n");
  auto kernel2 = [&](sycl::handler& cgh) {
    cgh.parallel_for(sycl::nd_range<1>(gws, lws), [=](sycl::nd_item<1> item) {
      VoteAllKernel2(d_input, d_result, repeat, item);
    });
  };

  // Warmup
  q.submit(kernel2).wait();

  start = std::chrono::steady_clock::now();

  q.submit(kernel2).wait();

  end = std::chrono::steady_clock::now();
  time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  printf("\tkernel execution time: %f (s)\n", time * 1e-9f);

  q.memcpy(h_result, d_result,
           VOTE_DATA_GROUP * warp_size * sizeof(unsigned int)).wait();

  error_count[1] += checkResultsVoteAllKernel2(
      h_result, VOTE_DATA_GROUP * warp_size, warp_size);

  // Second Vote Kernel Test #3 (both Any/All)
  dinfo = (bool *)sycl::malloc_device(warp_size * 3 * 3 * sizeof(bool), q);

  // Warmup
  auto kernel3 = [&](sycl::handler& cgh) {
    cgh.parallel_for(sycl::nd_range<1>(sycl::range<1>(warp_size * 3),
                                       sycl::range<1>(warp_size * 3)),
      [=](sycl::nd_item<1> item) {
        VoteAnyKernel3(dinfo, warp_size, repeat, item);
    });
  };

  q.submit(kernel3).wait();
  q.memset(dinfo, 0, warp_size * 3 * 3 * sizeof(bool)).wait();

  printf("\tRunning <<Vote.Any>> kernel3 ...\n");

  start = std::chrono::steady_clock::now();

  q.submit(kernel3).wait();

  end = std::chrono::steady_clock::now();
  time = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  printf("\tkernel execution time: %f (s)\n", time * 1e-9f);

  hinfo = (bool*) malloc (warp_size * 3 * 3 * sizeof(bool));
  q.memcpy(hinfo, dinfo, warp_size * 3 * 3 * sizeof(bool)).wait();

  error_count[2] = checkResultsVoteAnyKernel3(hinfo, warp_size * 3);

  sycl::free(d_input, q);
  sycl::free(d_result, q);
  sycl::free(dinfo, q);

  free(h_input);
  free(h_result);
  free(hinfo);

  return (error_count[0] == 0 && error_count[1] == 0 && error_count[2] == 0)
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
