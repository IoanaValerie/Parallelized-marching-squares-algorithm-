# Parallelized-marching-squares-algorithm

Homework1 -> Parallel and Distributed Algorithms

I parallelized the rescale, sample_grid and mark functions from the initial code into
a new function that I call when creating threads (thread_function) to speed up image processing.

I used the ThreadData structure to pass the necessary data to the function
thread_function, not being allowed to have global variables.

Parallelized functions:
1) Rescale: I parallelized the bicubic interpolation used in the algorithm
sequentially dividing the image into segments, each thread being responsible for
a part of the final image.

2) Sample_grid: the resized image is divided into smaller segments,
each being processed by a thread.

3) March: each thread works on distinct regions of the grid to
execute this step of the algorithm faster.

Barriers:
I used barriers after each done parallelization to make sure that
all threads have completed the given task before proceeding to the next step,
in order not to change the same part of the image at the same time (synchronization).
