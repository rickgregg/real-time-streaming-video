# real-time-streaming-video
Real-Time Streaming Video and Image Processing on Inexpensive Hardware with Low Latency

The use of resource constrained inexpensive hardware places restrictions on the design of streaming video and image processing system performance. In order to achieve acceptable frame-per-second (fps) performance with low latency, it is important to understand the response time requirements that the system needs to meet. For humans to be able to process and react to an image there should not be more than a 100ms delay between the time a camera captures an image and subsequently displays that image to the user.

In order to accomplish this goal, several design considerations need to be taken into account that limit the use of high level abstractions in favor of techniques that optimize performance. The reference design shown in this work uses embedded Linux on commercially available hardware costing $150. Performance is optimized by employing Linux user-space to kernel level functions including Video for Linux 2 (V4L2) and the Linux frame buffer. Optimized algorithms for color space conversion and image processing using a Haar Discrete Wavelet Transform (DWT) are also presented.

The results of this work show that real-time streaming video and image processing performance of 10 fps to 15 fps with 67ms to 100ms of latency can be achieved on embedded Linux using low cost hardware, kernel level abstractions and optimized algorithms.
