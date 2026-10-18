The key ones before A2 data arrives:

gaussian_std: [3, 6, 10, 15] DN — calibrate from A2 by measuring std of a static bg region
animal.gray_value: [30, 55, 80, 110] — at 110 the contrast (72 DN) is below the binary threshold (80), which intentionally stresses the pipeline
pipeline.binary_threshold: [50, 80, 110, 140] — the primary detection sensitivity knob to sweep in step C
trajectory.speed_px: [3, 7, 14, 22] — fast movement causes MOG2 mask smearing, interesting stress test