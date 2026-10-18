aim: find a "very good" (local optimum)
configuration for pipeline experimental setup

new sub project:

* A analyse existing records
* A1 draw samples from existing records via python script
* A2 analyse for noise and typical artefacts
  (e.g. reflections; camera noise)

* B  generate artificial records with a known ground truth
* B1 generate a trajectory in round arena
     - correlated random walk with wall repulsion
     - resting bouts: two-state Markov chain (MOVING <-> RESTING)
       rest_prob, resume_prob, rest_jitter_px
* B2 generate an animal body (long body; round body)
* B3 generate record frame for trajectory
* B4 add noise to records like discovered in A
     (gaussian sensor noise, static reflections, temporal flicker)

* C  run Simulation

* C1 generate X records with diverse parameter set (generation set 1)
     last step is always the centroid of moving obj
     parameters vary: body type, size, speed, turn_std,
                      rest_prob, noise levels
    - B4 noise parameters are initialized from A2 measurements; defaults are prior estimates and fixed (no extra vartion here )

* C2 generate Y pipeline configs with diverse parameter set (pipeline set 1)
    - each config is a JSON following the pipeline step format
     + parameters per step
     starting with initial default pipeline "pipe-default-42"
     parameters vary: binary_threshold, morph_ksize,
                      contour_min_area, contour_max_contours,
                      background history
    - how do variate pipeline config ?  
    Random/latin hypercube: sample Y random configs from the ranges → more tractable, better coverage

* C3 run simulation for each combination of C1 x C2
    - use DiskImageFrameSource to read in our record set ( generated in C1 )

     feed synthetic frames into the pipeline
     collect per-frame detected centroid from metadata_log.txt

* C4 calculate error metrics per (generation, pipeline) pair

     alpha:   mean per-frame Euclidean distance
              detected centroid -> ground truth centroid  [px]
              (only frames with a detection)

     alpha_p95 (epsilon):
              95th percentile of per-frame positional error  [px]
              catches outlier spikes (e.g. pipeline locks onto reflection)

     beta:    mean absolute difference in frame-to-frame speed
              detected displacement - ground truth displacement  [px/frame]

     gamma:   Detection Rate  DR = n_detected / n_animal_frames  [0-1]
              split into:
                DR_moving  - during movement bouts  (should approach 1.0)
                DR_resting - during rest bouts       (harder; MOG2 absorbs still animal)

     delta:   False Positive Rate  FPR = n_detections_in_warmup / n_warmup_frames  [0-1]
              warmup frames contain NO animal; any detection = reflection / noise hit

     eta:     Multi-contour rate  η = n_frames_with_area_filter_count > 1 / n_detected_frames  [0-1]
              "centroid smear" diagnostic: when both animal AND a reflection survive
              ContourAreaFilter, MassCenterOverlay unions them into one convex hull
              and the centroid lands between them — alpha spikes but δ does NOT catch it.
              Fix: set max_contours=1 in ContourAreaFilter (keep largest contour only).
              η > 0.05 is a strong signal that min_area / reflection_brightness are
              poorly matched and max_contours=1 should be enabled.
              Source metadata key: area_filter_count (set before max_contours selection).

     zeta:    Trajectory Continuity
              TC = n_consecutive_detected_pairs / (n_detected_frames - 1)  [0-1]
              flickering pipelines score low; hurts downstream speed/heading analysis

     SCORE (composite, single scalar for C6 optimisation):
              score = mean( alpha_imputed )   [px]
              where:
                alpha_imputed[frame] = alpha[frame]      if detected
                                     = arena_radius_px   if missed
              -> folds accuracy + detection rate into one metric
              -> missed frame costs one arena-radius of error (worst case)
              -> lower is better; units stay interpretable (pixels)

* C5 store a summary of each simulation run including:
     - complete generation set parameters (generation set 1)
     - complete pipeline set parameters  (pipeline set 1)
     - all seven metrics per combination (alpha, alpha_p95, beta, gamma, delta, eta, zeta)
     - SCORE per combination
     - ranked leaderboard by SCORE

* C6 review simulation run and
     vary pipeline set 1 according to data optimisation
     (use SCORE as the single ranking key)
     iterate until SCORE converges or a threshold is met


