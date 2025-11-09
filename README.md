# Record3k
aim: real time bug tracker with GPU support 

## Parts
* image processing lib based on opencv (cpp) 
* mini client app (html;js)
* mini tool to parameterizse the processing pipeline

## Summary
features: 
* moving object tracking on cameras with 10 FPS and more
* configurable data processing pipeline
* GPU support of Processing steps
* API Endpoints for controlling

## Build

do a local build (deps needed)
```bash
/localBuild.sh --cuda -vimbax --api  -j 8    
```

do a docker build:

step 1: build config/DockerCustomBase 

step 2: build config/DockerBuildOpenCV

step 3: build config/Dockerfile


## Run 

```bash
cd build/bin
# - copy default data folder if not exists
# cp -r ../../data . 

#  - mabye add lib to path
# export LD_LIBRARY_PATH=../lib:$LD_LIBRARY_PATH
# - run api server
# ./apiserver
```

## View

```bash
cd client 
caddy run --config Caddyfile
# open via
firefox http://127.0.0.1:3000/index.html
```
## License

Licensed under the MIT License. See the `LICENSE` file for the full text.

Copyright (c) 2025 record3k contributors
