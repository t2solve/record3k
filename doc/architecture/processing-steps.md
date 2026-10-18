# Processing Steps Reference

Each processing step implements the `ProcessStep` interface and receives its configuration via a `FilterConfig` object. Parameters are keyed by string name and stored as `double`, `std::string`, or `cv::Mat` values.

Steps marked **CPU+CUDA** provide two variants that are selected automatically based on hardware availability and pipeline configuration. CUDA steps fall back to a no-op (empty frame) when CUDA is unavailable.

---

## Table of Contents

1. [BackgroundSubtraction](#1-backgroundsubtraction)
2. [BilateralFilter](#2-bilateralfilter)
3. [BinaryThreshold](#3-binarythreshold)
4. [ContourAreaFilter](#4-contourareafilter)
5. [ContourDetection](#5-contourdetection)
6. [DebugLogger](#6-debuglogger)
7. [Denoise](#7-denoise)
8. [EdgeDetection](#8-edgedetection)
9. [FrameWriter](#9-framewriter)
10. [GaussianBlur](#10-gaussianblur)
11. [LensCorrection](#11-lenscorrection)
12. [MassCenterOverlay](#12-masscenteroverlay)
13. [MedianFilter](#13-medianfilter)
14. [MorphologyClose](#14-morphologyclose)
15. [ProjectPointTo2DSurface](#15-projectpointto2dsurface)
16. [ROICircleCrop](#16-roicirclecrop)
17. [Sharpen](#17-sharpen)

---

## 1. BackgroundSubtraction

**FilterType:** `BACKGROUND_SUBTRACTION_MOG2`  
**Availability:** CPU + CUDA  
**Header:** `include/liblbt/steps/background_subtraction.h`

Separates moving foreground objects from a learned background model using OpenCV's MOG2 (Gaussian Mixture) algorithm. The output is a foreground mask (binary image, 8-bit single-channel) where white pixels indicate detected foreground.

The background model is initialized on the first frame and continuously updated on every subsequent frame.

**Note:** The `method` selector (MOG2 vs KNN) is currently hardcoded to MOG2 -- the KNN path in the CPU implementation exists but is unreachable without code changes.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `history` | int | `500` | Number of past frames used to build the background model. Longer history makes the background adapt more slowly to scene changes; shorter history adapts faster but is noisier. |
| `var_threshold` | double | `16.0` | Mahalanobis distance threshold for deciding whether a pixel belongs to the background. Lower values make the detector more sensitive (more foreground pixels); higher values reduce false positives. |
| `detect_shadows` | double (bool) | `1.0` (true) | When `> 0.5`, shadow pixels are detected and marked with a grey value (127) in the mask instead of white (255). Useful to distinguish real objects from their shadows. Set to `0` to treat shadows as foreground. |
| `apply_morphology` | double (bool) | `0.0` (false) | **CPU only.** When `> 0.5`, applies a morphological close followed by open on the foreground mask after subtraction, filling small holes and removing isolated noise. |
| `morph_kernel_size` | int | `3` | **CPU only.** Size of the elliptical structuring element used for the optional morphological operations (see `apply_morphology`). Larger values close/open bigger regions. Only has effect when `apply_morphology > 0.5`. |

### Output

Foreground mask image (single-channel, 8-bit). No frame metadata is added.

---

## 2. BilateralFilter

**FilterType:** `BILATERAL_FILTER`  
**Availability:** CPU + CUDA  
**Header:** `include/liblbt/steps/bilateral_filter.h`

Applies a bilateral filter that smooths the image while preserving edges. Unlike a Gaussian blur, it considers both spatial proximity and color similarity, so edges between differently-colored regions remain sharp.

**Note:** The CPU and CUDA variants use slightly different parameter names for the neighborhood size (`d` vs `kernel_size`).

### Parameters

| Parameter | Type | Default | CPU | CUDA | Description |
|---|---|---|---|---|---|
| `d` | int | `9` | yes | no | Diameter of the pixel neighborhood considered during filtering. Larger values include more distant pixels and produce stronger smoothing. |
| `kernel_size` | int | `5` | no | yes | Equivalent neighborhood size for the CUDA bilateral filter. |
| `sigma_color` | double | `75.0` | yes | yes | Sigma for the color/intensity space. Larger values mean pixels with more different colors are mixed together, reducing color variation but also blurring across edges more. |
| `sigma_space` | double | `75.0` | yes | yes | Sigma for the spatial coordinate space. Larger values consider pixels farther away as similar, increasing the effective blur radius. |

---

## 3. BinaryThreshold

**FilterType:** `BINARY_THRESHOLD`  
**Availability:** CPU + CUDA  
**Header:** `include/liblbt/steps/binary_threshold.h`

Applies a global intensity threshold. Each pixel is set to `max_value` if its intensity is at or above `threshold`, and to 0 otherwise. Converts a grayscale (or foreground mask) image into a clean binary image.

This step is typically placed after BackgroundSubtraction or Denoise and before ContourDetection / ContourAreaFilter.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `threshold` | int | `80` | Intensity cutoff value (0-255). Pixels at or above this value become `max_value`; pixels below become 0. Lower values include more pixels as foreground. |
| `max_value` | int | `255` | Value assigned to pixels that pass the threshold. Usually 255 (white). Changing this affects downstream steps that expect a standard binary image. |

### Output Metadata

| Key | Type | Description |
|---|---|---|
| `threshold_value` | int | The effective threshold value that was applied. |
| `max_value` | int | The effective max value that was applied. |

---

## 4. ContourAreaFilter

**FilterType:** `CONTOUR_AREA_FILTER`  
**Availability:** CPU + CUDA  
**Header:** `include/liblbt/steps/contour_area_filter.h`

Finds all external contours in a binary input image and keeps only those whose pixel area falls within `[min_area, max_area]`. Contours outside this range are discarded. The filtered contours, their bounding rotated rectangles, and center points are forwarded as frame metadata for use by downstream steps (e.g. MassCenterOverlay, DebugLogger).

A warning is logged if all contours are removed by the filter, along with individual contour areas at debug level to help tune the thresholds.

**Note:** Both CPU and CUDA variants default `min_area` to `150`.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `min_area` | double | `150.0` | Minimum contour area in pixels^2. Contours smaller than this are discarded. Increase to filter out noise and tiny objects. |
| `max_area` | double | `300000.0` | Maximum contour area in pixels^2. Contours larger than this are discarded. Decrease to reject large artifacts or image borders that were incorrectly detected as contours. |
| `debug` | double (bool) | `0.0` (false) | When `> 0.5`, the output image is converted to BGR and the kept contours (green), their minimum-area bounding rectangles (blue), and center points (blue dots) are drawn on it for visual inspection. |

### Output Metadata

| Key | Type | Description |
|---|---|---|
| `original_contour_count` | int | Total number of contours found before area filtering. |
| `filtered_contour_count` | int | Number of contours remaining after area filtering. |
| `min_area_threshold` | double | The `min_area` value actually used. |
| `max_area_threshold` | double | The `max_area` value actually used. |
| `filtered_contours` | `vector<vector<Point>>` | The kept contours (required by MassCenterOverlay and DebugLogger). |
| `filtered_contour_rects` | `vector<RotatedRect>` | Minimum-area bounding rectangles for each kept contour. |
| `filtered_contour_centers` | `vector<Point2f>` | Center of each bounding rectangle. |

---

## 5. ContourDetection

**FilterType:** `CONTOUR_DETECTION`  
**Availability:** CPU + CUDA  
**Header:** `include/liblbt/steps/contour_detection.h`

Detects contours in a binary image using OpenCV's `findContours`. Contours are filtered by area and optionally drawn onto a black canvas. Detailed per-contour statistics are attached as frame metadata.

This step is more general than ContourAreaFilter: it exposes the OpenCV retrieval mode and approximation method, and also computes perimeters and aggregate statistics.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `retrieval_mode` | int | `0` (RETR_EXTERNAL) | OpenCV contour retrieval mode. `0`=RETR_EXTERNAL (outermost contours only), `1`=RETR_LIST, `2`=RETR_CCOMP, `3`=RETR_TREE. Affects which contours are returned and how hierarchy is built. |
| `approximation_method` | int | `2` (CHAIN_APPROX_SIMPLE) | OpenCV contour approximation method. `1`=CHAIN_APPROX_NONE (all points), `2`=CHAIN_APPROX_SIMPLE (compressed segments). Affects the number of points per contour and memory usage. |
| `draw_contours` | double (bool) | `0.0` (false) | When `> 0.5`, the output image is a black canvas of the same size as the input with filtered contours drawn on it in the specified color. When false, the input binary image is passed through. |
| `min_area` | double | `10.0` | Minimum contour area in pixels^2 to keep. Filters out noise. |
| `max_area` | double | `100000.0` | Maximum contour area in pixels^2 to keep. Filters out large background artifacts. |
| `contour_color_b` | double | `0.0` | Blue channel of the color used to draw contours when `draw_contours` is active (0-255). |
| `contour_color_g` | double | `255.0` | Green channel of the contour drawing color (0-255). |
| `contour_color_r` | double | `0.0` | Red channel of the contour drawing color (0-255). |
| `thickness` | int | `2` | Line thickness in pixels for drawn contours. Only has effect when `draw_contours > 0.5`. |

### Output Metadata

| Key | Type | Description |
|---|---|---|
| `contours` | `vector<vector<Point>>` | Filtered contour point lists. |
| `hierarchy` | `vector<Vec4i>` | OpenCV hierarchy data for filtered contours. |
| `contour_count` | int | Number of kept contours. |
| `contour_areas` | `vector<double>` | Area of each kept contour in pixels^2. |
| `contour_perimeters` | `vector<double>` | Perimeter (arc length) of each kept contour. |
| `total_contour_area` | double | Sum of all kept contour areas. |
| `mean_contour_area` | double | Average area across kept contours. |
| `max_contour_area` | double | Largest kept contour area. |
| `min_contour_area` | double | Smallest kept contour area. |

---

## 6. DebugLogger

**FilterType:** `DEBUG_LOGGER`  
**Availability:** CPU only  
**Header:** `include/liblbt/steps/debug_logger.h`

A **pass-through** step that collects per-frame contour and detection metadata into an in-RAM buffer. At the end of a recording session, call `flush(dbPath)` to persist all collected data to a SQLite database.

The step reads the `filtered_contours`, `filtered_contour_centers`, and `filtered_contour_rects` metadata fields produced by ContourAreaFilter. It must therefore be placed after ContourAreaFilter in the pipeline.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `enabled` | double (bool) | `1.0` (true) | When `<= 0.5`, the step is skipped entirely and the frame is passed through without any logging. Useful to disable logging without removing the step from the pipeline. |

### SQLite Schema (on `flush`)

**`frames` table**

| Column | Type | Description |
|---|---|---|
| `frame_idx` | INTEGER PK | Sequential frame index within this recording session. |
| `timestamp_ns` | INTEGER | Camera timestamp in nanoseconds from the frame's `FrameMemoryObject`. |
| `contour_count` | INTEGER | Number of detected contours in this frame. |

**`detections` table**

| Column | Type | Description |
|---|---|---|
| `id` | INTEGER PK | Auto-incremented row identifier. |
| `frame_idx` | INTEGER FK | References `frames.frame_idx`. |
| `contour_idx` | INTEGER | Index of the contour within this frame. |
| `center_x/y` | REAL | Center point of the bounding rotated rectangle. |
| `rect_x/y/w/h/angle` | REAL | Full rotated bounding rectangle. |
| `area` | REAL | Contour area in pixels^2. |
| `polygon_json` | TEXT | Contour polygon as a compact JSON array `[[x,y],...]`. |

---

## 7. Denoise

**FilterType:** `DENOISE`  
**Availability:** CPU only  
**Header:** `include/liblbt/steps/denoise.h`

Reduces image noise using OpenCV's Non-Local Means (NLM) algorithm. NLM is a high-quality but computationally expensive denoising method. It finds similar patches across the image and averages them, preserving structure better than local filters.

Grayscale input uses `fastNlMeansDenoising`; color input uses `fastNlMeansDenoisingColored`.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `h` | double | `3.0` | Filter strength for luminance (or the full signal for grayscale images). Higher values remove more noise but also remove fine detail and textures. Values in the range 3-10 are typical; above 10 produces noticeable over-smoothing. |
| `h_color` | double | `3.0` | Filter strength applied to the color (chrominance) channels for color images. Only used when input is multi-channel. Increase to reduce color noise. |
| `template_window_size` | int | `7` | Side length (in pixels) of the square patch used to compare image regions. Must be odd. Larger patches capture more context per comparison but slow down processing significantly. |
| `search_window_size` | int | `21` | Side length (in pixels) of the window searched for similar patches around each pixel. Must be odd. Larger search windows improve quality but dramatically increase computation time. |

---

## 8. EdgeDetection

**FilterType:** `EDGE_DETECTION`  
**Availability:** CPU + CUDA  
**Header:** `include/liblbt/steps/edge_detection.h`

Detects edges using the Canny algorithm. The input is converted to grayscale if needed, then Sobel gradients are computed and edges are extracted via hysteresis thresholding with the two provided thresholds.

Output is a single-channel binary edge image (white pixels = edges).

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `threshold1` | double | `100.0` | Lower hysteresis threshold. Pixels with gradient magnitude below this value are definitely not edges. Decrease to detect more (weaker) edges. |
| `threshold2` | double | `200.0` | Upper hysteresis threshold. Pixels above this are definitely edges. Pixels between `threshold1` and `threshold2` are only kept if connected to a strong edge. Increase to reduce false edges. |
| `aperture_size` | int | `3` | Size of the Sobel kernel used to compute image gradients. Must be `3`, `5`, or `7`. Larger values produce smoother gradient estimates and are better for noisy images, but may miss thin features. |
| `l2_gradient` | double (bool) | `0.0` (false) | When `> 0.5`, uses the more accurate L2 norm (`sqrt(dx^2+dy^2)`) for gradient magnitude. When false, uses the faster L1 approximation (`|dx|+|dy|`). The L2 norm produces slightly sharper edge maps. |

---

## 9. FrameWriter

**FilterType:** `FRAME_WRITER`  
**Availability:** CPU only  
**Header:** `include/liblbt/steps/frame_writer.h`

A **pass-through** step that writes each frame to disk as an image file without modifying the frame. Filenames follow the pattern `frame_XXXXXX_<timestamp_ns>.<format>`, where `XXXXXX` is a zero-padded sequential counter and `<timestamp_ns>` is the camera hardware timestamp in nanoseconds.

The output directory is created automatically if it does not exist. Progress is logged every 100 frames.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `enabled` | double (bool) | `1.0` (true) | When `<= 0.5`, the step is a pure no-op and writes nothing. Allows disabling disk writes at runtime without restructuring the pipeline. |
| `output_dir` | string | `"output_frames"` | Filesystem path (absolute or relative) of the directory to write frame images into. Created automatically if missing. |
| `format` | string | `"png"` | File extension / format for written images (e.g. `"png"`, `"jpg"`, `"tiff"`). Passed directly to `cv::imwrite`; the codec is selected by OpenCV based on the extension. |

---

## 10. GaussianBlur

**FilterType:** `GAUSSIAN_BLUR`  
**Availability:** CPU + CUDA  
**Header:** `include/liblbt/steps/gaussian_blur.h`

Applies a Gaussian blur to reduce high-frequency noise and smooth the image. The kernel is always forced to an odd size.

**Note:** The CUDA variant converts non-grayscale, non-RGBA images to grayscale before blurring. The CPU variant preserves the original number of channels.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `kernel_size` | int | `5` | Width and height of the Gaussian kernel in pixels (always made odd). Larger values increase the blur radius. Values of 3, 5, 7, 11 are common. |
| `sigma_x` | double | `1.0` | Standard deviation of the Gaussian in the horizontal direction. Larger values spread the blur more in X. If `sigma_y` is `0`, this value is also used for Y. |
| `sigma_y` | double | `1.0` (CPU) / `0.0` (CUDA) | Standard deviation of the Gaussian in the vertical direction. `0` means "same as `sigma_x`". Allows anisotropic blurring when set differently from `sigma_x`. |

---

## 11. LensCorrection

**FilterType:** `LENS_CORRECTION`  
**Availability:** CPU + CUDA  
**Header:** `include/liblbt/steps/lens_correction.h`

Corrects lens distortion using the standard Brown-Conrady camera model. Builds the intrinsic camera matrix from `fx`, `fy`, `cx`, `cy` and the distortion vector from `k1`-`k3`, `p1`, `p2`, then calls `cv::undistort` (CPU) or `cv::cuda::remap` with pre-computed maps (CUDA).

Use calibrated values from a camera calibration procedure for best results. Default values produce no correction.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `fx` | double | `cols x 0.8` | Focal length in pixels along the X axis. Affects horizontal scaling of the undistorted image. |
| `fy` | double | `rows x 0.8` | Focal length in pixels along the Y axis. Affects vertical scaling of the undistorted image. |
| `cx` | double | `cols x 0.5` | X coordinate of the principal point (optical center). Typically close to the image center. |
| `cy` | double | `rows x 0.5` | Y coordinate of the principal point. Typically close to the image center. |
| `k1` | double | `0.0` | 1st radial distortion coefficient. Positive values cause barrel distortion; negative values cause pincushion distortion. |
| `k2` | double | `0.0` | 2nd radial distortion coefficient. Corrects higher-order radial distortion at larger distances from the center. |
| `k3` | double | `0.0` | 3rd radial distortion coefficient. Rarely needed; only significant for very wide-angle lenses. |
| `p1` | double | `0.0` | 1st tangential distortion coefficient. Corrects misalignment of the lens and image sensor in the vertical direction. |
| `p2` | double | `0.0` | 2nd tangential distortion coefficient. Corrects misalignment in the horizontal direction. |

---

## 12. MassCenterOverlay

**FilterType:** `MASS_CENTER_OVERLAY`  
**Availability:** CPU only  
**Header:** `include/liblbt/steps/mass_center_overlay.h`

Computes the mass center of the convex hull formed by all points from the `filtered_contours` metadata, then draws a filled red circle (radius 10) at that location on the image.

This step **requires** the `filtered_contours` metadata key produced by ContourAreaFilter to be present in the incoming frame. It must therefore be placed after ContourAreaFilter in the pipeline.

The image is converted to BGR if it is grayscale, so that the colored dot can be drawn.

### Parameters

This step takes no configuration parameters. The `FilterConfig` argument is accepted but not read.

### Input Metadata Required

| Key | Type | Source |
|---|---|---|
| `filtered_contours` | `vector<vector<Point>>` | ContourAreaFilter |

### Output Metadata

| Key | Type | Description |
|---|---|---|
| `mass_center_point` | `cv::Point2f` | Pixel coordinates of the computed mass center. Set to `(-1, -1)` if the center could not be computed (no contours or degenerate convex hull). |

---

## 13. MedianFilter

**FilterType:** `MEDIAN_FILTER`  
**Availability:** CPU only  
**Header:** `include/liblbt/steps/median_filter.h`

Replaces each pixel with the median value within a square neighborhood. Highly effective at removing salt-and-pepper noise while preserving edges. The kernel size is forced to an odd number.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `kernel_size` | int | `5` | Side length of the square neighborhood in pixels (forced odd). Larger values remove more noise but blur edges and fine details more strongly. Values of 3, 5, 7 are most common. |

---

## 14. MorphologyClose

**FilterType:** `MORPHOLOGY_CLOSE`  
**Availability:** CPU + CUDA  
**Header:** `include/liblbt/steps/morphology_close.h`

Applies a morphological close operation (dilation followed by erosion) to a binary or grayscale image. Closing fills small holes and gaps within foreground regions without significantly changing their size, making it useful to connect broken contours after background subtraction or edge detection.

The input is converted to grayscale if it has more than one channel.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `ksize` | int | `15` | Width and height of the structuring element in pixels (forced odd). Larger values close bigger gaps. Values of 3-7 handle small noise; 15+ connect larger broken regions. |
| `shape` | int | `2` (ellipse) | Shape of the structuring element: `0` = rectangle, `1` = cross, `2` = ellipse. Ellipse is the most isotropic and avoids artifacts at corners; rectangle may extend gaps diagonally. |
| `iterations` | int | `1` | Number of times to apply the close operation sequentially. More iterations have a cumulative effect similar to using a larger kernel. |

---

## 15. ProjectPointTo2DSurface

**FilterType:** `PROJECT_POINT_TO_2D_SURFACE`  
**Availability:** CPU only  
**Header:** `include/liblbt/steps/project_point_to_2d_surface.h`

Projects the pixel-space mass center point (from MassCenterOverlay) onto a real-world flat Z=0 plane using a camera calibration-derived inverse homography. The result is a world-coordinate point suitable for physical distance measurements.

On the first frame, it reads the three Mat parameters from the config, extracts the first pose from the extrinsics matrix, builds the projection matrix, and inverts the Z=0 homography. All subsequent frames reuse the precomputed inverse homography without re-reading the config.

This step **requires** the `mass_center_point` metadata key from MassCenterOverlay and must be placed after it in the pipeline.

### Parameters

All three parameters are of type `cv::Mat` and must be set via `FilterConfig::setParameter(key, cv::Mat)`.

| Parameter | Type | Description |
|---|---|---|
| `camera_matrix` | `cv::Mat` (3x3, CV_64F) | Intrinsic camera matrix `[fx 0 cx; 0 fy cy; 0 0 1]` from camera calibration. |
| `distortion_coefficients` | `cv::Mat` (1xN) | Distortion coefficient vector from camera calibration (passed to `initDuringFirstStart` but not used in the homography itself -- undistortion is assumed to have been done upstream). |
| `extrinsics_combined` | `cv::Mat` (Nx6) | Each row is one pose `[rvec_x rvec_y rvec_z tvec_x tvec_y tvec_z]`. Only the first row (index 0) is used. |

### Input Metadata Required

| Key | Type | Source |
|---|---|---|
| `mass_center_point` | `cv::Point2f` | MassCenterOverlay |

### Output Metadata

| Key | Type | Description |
|---|---|---|
| `mass_center_point_pixel` | `cv::Point2f` | Original pixel-space center (carried forward unchanged). |
| `mass_center_point_world` | `cv::Point2d` | Projected world-space coordinate on the Z=0 plane. Contains `NaN` if the projection is degenerate. |

---

## 16. ROICircleCrop

**FilterType:** `ROI_CIRCLE_CROP`  
**Availability:** CPU + CUDA  
**Header:** `include/liblbt/steps/roi_circle_crop.h`

Masks the input image with a filled circle and crops to the bounding rectangle of that circle. Pixels outside the circle are set to black. The output image is smaller than the input (bounding box of the circle, clamped to image bounds).

If `center_x`, `center_y`, or `radius` produce an invalid configuration (center outside image or radius <= 0), an empty `FrameMemoryObject` is returned.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `center_x` | int | `cols / 2` | X pixel coordinate of the circle's center. Values outside `[0, cols)` result in an empty output. |
| `center_y` | int | `rows / 2` | Y pixel coordinate of the circle's center. Values outside `[0, rows)` result in an empty output. |
| `radius` | int | `min(cols, rows) / 4` | Radius of the crop circle in pixels. The output image will be at most `2xradius` wide and `2xradius` tall. Larger values retain more of the image; smaller values crop tighter. |

---

## 17. Sharpen

**FilterType:** `SHARPEN`  
**Availability:** CPU + CUDA  
**Header:** `include/liblbt/steps/sharpen.h`

Enhances edges and fine details using a Laplacian-based 3x3 sharpening kernel. The kernel is scaled by `strength`, which allows smooth control from no effect (`0`) to aggressive sharpening (`> 1`).

Kernel structure (scaled by `strength`, center = `1 + 4xstrength`):

```
 0     -s    0
-s   1+4s   -s
 0     -s    0
```

High `strength` values can introduce ringing artefacts and amplify noise.

### Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `strength` | double | `1.0` | Controls the intensity of the sharpening effect. `0` produces no change (identity). `1` applies standard sharpening. Values above `1` progressively over-sharpen, potentially producing halos around edges. Values below `1` but above `0` apply subtle sharpening. |

---

## Typical Pipeline Order

```
BackgroundSubtraction  ->  BinaryThreshold  ->  MorphologyClose
  ->  ContourAreaFilter  ->  ContourDetection  ->  MassCenterOverlay
  ->  ProjectPointTo2DSurface  ->  DebugLogger  ->  FrameWriter
```

Pre-processing filters (GaussianBlur, MedianFilter, BilateralFilter, Denoise, Sharpen, LensCorrection, ROICircleCrop, EdgeDetection) are applied before the subtraction / detection stages as needed.
