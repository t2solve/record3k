# simulation run

## configure simulation run
* write generator infos; amount records etc.
* define  param_ranges.json 
* define simlation name and parameter
* run anaylse on last run 

## generate data
 ./pipeoptimizer generate \
    --output ../data/run3/synth \
    --n-records 13 \
    --static-records 1 \
    --body-type both \
    --warmup 500 \
    --animal-frames 800 \
    --seed 42
 
## run simulation
 ./pipeoptimizer run \
  --synth-root ../data/run3/synth \
  --param-ranges ../../src/pipeoptimizer/param_ranges.json \
  --n-pipes 25 \
  --n-workers 12 \
  --db ../data/sim_results.sqlite \
  --description "sweep-3"
  
## run anaylse alias after math
  python3 ../../src/pipeoptimizer/analysis/analyse.py \
  --db ../data/sim_results.sqlite \
  --synth-root ../data/run3/synth \
  --out-dir ../data/run3/analysis \
  --param-ranges ../../src/pipeoptimizer/param_ranges.json
  
# delete gen record data, but keep sim_results.sqlite and analysed output, any time 


  